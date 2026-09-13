// =============================================================================
// test_kc2g_history — historia kc2g por acumulacion (FASE B P1, sello GLM).
//
// Test de logica pura: sin GL, sin red. Llama a PRODUCCION
// (Kc2gAdapter::accumulateBatch/sampleHistoryAt), no replica. Las filas
// Kc2gRow se construyen a mano (fixture minimo, campos publicos).
//
// Gates spec: acumulacion multi-batch, dedup por epoch, cap 72, muestreo
// clamp/lerp, EG931 no resucita, ventana evaluada al usar.
// =============================================================================
#include "Data/Kc2gAdapter.h"
#include "Data/Kc2gCache.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

static int g_ok = 0, g_fail = 0;

static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static void check_near(const char* what, double got, double want, double tol) {
    const bool ok = std::fabs(got - want) <= tol;
    if (ok) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s: got %f want %f\n", ok ? "OK" : "FAIL", what, got, want);
}

static Kc2gRow makeRow(const char* code, double epoch, float foF2) {
    Kc2gRow r;
    r.code = code;
    r.name = code;
    r.latDeg = 40.0;
    r.lonDeg = -4.0;
    r.timeUtc = epoch;
    r.fof2 = foF2;
    r.mufd = foF2 * 2.9f;
    r.hmf2 = 300.0f;
    return r;
}

static const StationSample* findByCode(const std::vector<StationSample>& v,
                                       const char* code) {
    for (const auto& s : v)
        if (s.code == code) return &s;
    return nullptr;
}

int main() {
    std::printf("--- FASE B P1: acumulacion multi-batch ---\n");
    {
        Kc2gAdapter a;
        a.accumulateBatch({makeRow("EB040", 1000.0, 5.0f)}, 1500.0);
        a.accumulateBatch({makeRow("EB040", 2000.0, 6.0f)}, 2500.0);
        auto v = a.sampleHistoryAt(2000.0);
        const StationSample* s = findByCode(v, "EB040");
        check_bool("EB040 muestreada", s != nullptr);
        check_near("ultimo valor al final", s ? s->foF2 : -1.0, 6.0, 1.0e-6);
        auto mid = a.sampleHistoryAt(1500.0);
        const StationSample* m = findByCode(mid, "EB040");
        check_near("lerp punto medio", m ? m->foF2 : -1.0, 5.5, 1.0e-6);
        auto pre = a.sampleHistoryAt(500.0);
        const StationSample* p = findByCode(pre, "EB040");
        check_near("clamp al frente", p ? p->foF2 : -1.0, 5.0, 1.0e-6);
    }

    std::printf("--- FASE B P1: dedup mismo epoch ---\n");
    {
        Kc2gAdapter a;
        a.accumulateBatch({makeRow("EB040", 1000.0, 5.0f)}, 1500.0);
        a.accumulateBatch({makeRow("EB040", 1000.0, 7.0f)}, 1500.0);
        auto v = a.sampleHistoryAt(1000.0);
        const StationSample* s = findByCode(v, "EB040");
        check_near("reemplaza, no duplica", s ? s->foF2 : -1.0, 7.0, 1.0e-6);
    }

    std::printf("--- FASE B P1: cap 72 ---\n");
    {
        Kc2gAdapter a;
        for (int i = 0; i < 75; i++) {
            const double ep = 1000.0 + i * 300.0;
            std::vector<Kc2gRow> b = {makeRow("EB040", ep, (float)i)};
            a.accumulateBatch(b, ep + 100.0);  // fresca al fetch
        }
        // 75 puntos capados a 72: el muestreo al final da i=74
        auto v = a.sampleHistoryAt(100000.0);
        const StationSample* s = findByCode(v, "EB040");
        check_near("cap conserva lo nuevo", s ? s->foF2 : -1.0, 74.0, 1.0e-6);
    }

    std::printf("--- FASE B P1: EG931 no resucita ---\n");
    {
        Kc2gAdapter a;
        a.accumulateBatch({makeRow("EG931", 1000.0, 9.0f)}, 1500.0);
        auto v = a.sampleHistoryAt(1000.0);
        check_bool("EG931 ausente", findByCode(v, "EG931") == nullptr);
    }

    std::printf("--- FASE B P1: ventana al usar ---\n");
    {
        Kc2gAdapter a;
        // Fila >24 h al fetch: no se acumula
        a.accumulateBatch({makeRow("EB040", 1000.0, 5.0f)}, 100000.0);
        auto v = a.sampleHistoryAt(100000.0);
        check_bool("vieja al fetch no entra", findByCode(v, "EB040") == nullptr);
        // Fila fresca al fetch, consumida 10 h despues: stale en grids
        a.accumulateBatch({makeRow("EB040", 200000.0, 5.0f)}, 200100.0);
        auto v2 = a.sampleHistoryAt(200000.0 + 10.0 * 3600.0);
        const StationSample* s = findByCode(v2, "EB040");
        check_bool("stale a 10 h del consumidor", s && s->stale);
        auto v3 = a.sampleHistoryAt(200000.0 + 3600.0);
        const StationSample* s3 = findByCode(v3, "EB040");
        check_bool("fresca a 1 h del consumidor", s3 && !s3->stale);
    }

    std::printf("--- FASE B P2: save->restore->sample (pegamento adapter) ---\n");
    {
        namespace fs = std::filesystem;
        std::string d = (fs::temp_directory_path() / "kc2g_hist_restore").string();
        fs::remove_all(d);
        // Dos batches via adapter (acumulan historia P1)...
        Kc2gAdapter src;
        src.accumulateBatch({makeRow("EB040", 100000.0, 5.0f)}, 100100.0);
        src.accumulateBatch({makeRow("EB040", 100600.0, 6.0f)}, 100700.0);
        // ...persistidos a mano como haria fetch() y restaurados en frio.
        kc2gcache::Frame f;
        f.epoch = 100700.0;
        kc2gcache::Row cr;
        cr.code = "EB040";
        cr.name = "EB040";
        cr.latDeg = 40.0;
        cr.lonDeg = -4.0;
        cr.timeUtc = 100600.0;
        cr.foF2 = 6.0f;
        cr.muf = 6.0f * 2.9f;
        cr.hmF2 = 300.0f;
        cr.valid = true;
        f.rows.push_back(cr);
        check_bool("save frame", kc2gcache::saveFrame(d, f));
        Kc2gAdapter cold;
        check_bool("restore 1 batch", cold.restoreFromCache(d, 100800.0) == 1);
        auto v = cold.sampleHistoryAt(100600.0);
        const StationSample* s = findByCode(v, "EB040");
        check_bool("restaurada muestreable", s != nullptr);
        check_near("valor restaurado", s ? s->foF2 : -1.0, 6.0, 1.0e-6);
        int nSt = 0;
        double t0 = 0.0, t1 = 0.0;
        check_bool("historyStats no vacio", cold.historyStats(nSt, t0, t1) && nSt == 1);
        fs::remove_all(d);
    }

    std::printf("\n[test_kc2g_history] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
