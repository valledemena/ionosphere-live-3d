// =============================================================================
// test_tec_cache — validacion del cache TEC en disco (M10, re-sellado GLM).
//
// Test de logica pura: sin GL, sin GLFW, sin red, sin curl. Llama a
// PRODUCCION (namespace teccache en src/Data/TecCache.{h,cpp}), no replica.
//
// Cubre la aceptacion M10: round-trip, trim/cap 72, heal de corruptos,
// delta (missingSince), staleness (isFresh) y simulated-jamas-en-cache
// (shouldCacheFrame). TDD: este test compila ANTES que el cableado.
// =============================================================================
#include "Data/TecCache.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
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

static std::string freshDir(const char* tag) {
    namespace fs = std::filesystem;
    static int n = 0;
    std::string d = (fs::temp_directory_path() /
        ("tec_cache_test_" + std::string(tag) + "_" + std::to_string(n++))).string();
    fs::remove_all(d);
    return d;
}

static teccache::Frame makeFrame(double epoch, int w, int h) {
    teccache::Frame f;
    f.epoch = epoch;
    f.w = w;
    f.h = h;
    f.v.resize((size_t)w * h);
    for (size_t i = 0; i < f.v.size(); i++) f.v[i] = (float)(epoch * 0.001 + i * 0.25);
    return f;
}

int main() {
    using namespace teccache;
    std::printf("--- M10a: shouldCacheFrame (simulated jamas en cache) ---\n");
    check_bool("real+enabled se cachea", shouldCacheFrame(true, false, true));
    check_bool("simulated NO se cachea", !shouldCacheFrame(true, true, true));
    check_bool("invalido NO se cachea", !shouldCacheFrame(false, false, true));
    check_bool("disabled NO se cachea", !shouldCacheFrame(true, false, false));

    std::printf("--- M10a: round-trip binario exacto ---\n");
    {
        std::string d = freshDir("rt");
        Frame f = makeFrame(1789000000.0, 4, 3);
        check_bool("save ok", saveFrame(d, f));
        Frame g;
        check_bool("load ok", loadFrame(d + "/tec_1789000000.bin", g));
        check_bool("epoch exacto", g.epoch == f.epoch);
        check_bool("w/h exactos", g.w == 4 && g.h == 3);
        bool same = g.v.size() == f.v.size();
        for (size_t i = 0; same && i < f.v.size(); i++)
            if (std::memcmp(&g.v[i], &f.v[i], 4) != 0) same = false;
        check_bool("values bit-exactos", same);
        Frame bad;
        bad.epoch = 1.0;
        bad.w = 2;
        bad.h = 2;
        bad.v.resize(3);  // size mismatch
        check_bool("save rechaza size mismatch", !saveFrame(d, bad));
    }

    std::printf("--- M10a: corruptos (magic/truncado/w-h) ---\n");
    {
        std::string d = freshDir("corrupt");
        ensureDir(d);
        FILE* f = std::fopen((d + "/tec_1.bin").c_str(), "wb");
        std::fputs("NOPE-NOPE-NOPE-NOPE", f);
        std::fclose(f);
        Frame g;
        check_bool("magic mismatch -> false", !loadFrame(d + "/tec_1.bin", g));
        f = std::fopen((d + "/tec_2.bin").c_str(), "wb");
        uint32_t m = kMagic, v = kVersion;
        std::fwrite(&m, 4, 1, f);
        std::fwrite(&v, 4, 1, f);  // header cortado
        std::fclose(f);
        check_bool("truncado -> false", !loadFrame(d + "/tec_2.bin", g));
    }

    std::printf("--- M10a: cap 432 + heal + orden ---\n");
    {
        std::string d = freshDir("cap");
        for (int i = 0; i < 435; i++) saveFrame(d, makeFrame(1000000.0 + i * 300.0, 2, 2));
        FILE* f = std::fopen((d + "/tec_9.bin").c_str(), "wb");
        std::fputs("GARBAGE", f);
        std::fclose(f);
        ScanResult r = loadCached(d);
        check_bool("432 kept", (int)r.frames.size() == 432);
        check_bool("3 podados", r.pruned == 3);
        check_bool("1 corrupto dropeado", r.dropped == 1);
        check_bool("corrupto borrado del disco",
                   !std::filesystem::exists(d + "/tec_9.bin"));
        bool sorted = true;
        for (size_t i = 1; i < r.frames.size(); i++)
            if (r.frames[i].epoch < r.frames[i - 1].epoch) sorted = false;
        check_bool("orden asc por epoch", sorted);
        check_bool("conserva los mas nuevos",
                   r.frames.front().epoch == 1000000.0 + 3 * 300.0);
    }

    std::printf("--- M10a: delta (missingSince) ---\n");
    {
        std::vector<double> cached = {100.0, 200.0, 300.0};
        std::vector<double> index = {150.0, 250.0, 300.0, 350.0, 400.0};
        std::vector<double> miss = missingSince(cached, index);
        check_bool("solo mas nuevos que max(cached)",
                   miss.size() == 2 && miss[0] == 350.0 && miss[1] == 400.0);
        check_bool("cache vacio -> todo el indice",
                   missingSince({}, index).size() == 5);
        check_bool("indice vacio -> nada", missingSince(cached, {}).empty());
    }

    std::printf("--- M10a: staleness (isFresh) ---\n");
    {
        const double now = 10000000.0;
        check_bool("reciente es current-eligible", isFresh(now - 3600.0, now, 24.0));
        check_bool("viejo solo replay", !isFresh(now - 25.0 * 3600.0, now, 24.0));
        check_bool("futuro leve es fresh", isFresh(now + 60.0, now, 24.0));
    }

    std::printf("--- TEC-ext 034: ventana (missingInWindow) ---\n");
    {
        // Ventana 72 h = 259200 s; indice 100..1000 paso 100.
        std::vector<double> index;
        for (int e = 100; e <= 1000; e += 100) index.push_back((double)e);
        const double now = 2000.0;  // ventana [-257200, 2000]: todo dentro
        check_bool("cache vacio -> ventana completa cap-bound",
                   missingInWindow({}, index, now, 432).size() == 10);
        // (1) filtra viejos fuera de ventana.
        {
            const double n2 = 300000.0;  // ventana [40800, 300000]
            std::vector<double> idx2;
            for (int e = 0; e <= 300000; e += 30000) idx2.push_back((double)e);
            auto got = missingInWindow({}, idx2, n2, 432);
            check_bool("filtra viejos fuera de ventana",
                       !got.empty() &&
                           idx2[got.front()] >= n2 - 259200.0);
        }
        // (2) dedup exacto contra cached.
        {
            std::vector<double> cached = {400.0, 600.0, 800.0};
            auto got = missingInWindow(cached, index, now, 432);
            bool ok = true;
            for (std::size_t i : got) {
                if (index[i] == 400.0 || index[i] == 600.0 ||
                    index[i] == 800.0)
                    ok = false;
            }
            check_bool("dedup exacto contra cached",
                       ok && got.size() == 10 - 3);
        }
        // (3) orden asc.
        {
            auto got = missingInWindow({}, index, now, 432);
            bool sorted = true;
            for (std::size_t i = 1; i < got.size(); i++)
                if (got[i] < got[i - 1]) sorted = false;
            check_bool("orden asc", sorted && !got.empty());
        }
        // (4) cap conserva los mas nuevos cuando faltan > cap.
        {
            auto got = missingInWindow({}, index, now, 4);
            check_bool("cap conserva los 4 mas nuevos",
                       got.size() == 4 && index[got.back()] == 1000.0 &&
                           index[got.front()] == 700.0);
        }
    }

    std::printf("--- P1: retencion delete-on-pop (removeFrame) ---\n");
    {
        std::string d = freshDir("ret");
        const int N = 435;
        for (int i = 0; i < N; i++)
            check_bool("save para retencion", saveFrame(d, makeFrame(2000000.0 + i * 600.0, 2, 2)));
        for (int i = 0; i < N - 432; i++)
            check_bool("removeFrame borra existente",
                       removeFrame(d, 2000000.0 + i * 600.0));
        std::size_t left = 0;
        for (const auto& e : std::filesystem::directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            const std::string nm = e.path().filename().string();
            if (nm.compare(0, 4, "tec_") == 0) ++left;
        }
        check_bool("quedan 432 en disco tras pops", left == 432);
        check_bool("removeFrame false sin fichero", !removeFrame(d, 1.0));
    }

    std::printf("--- P1b: fit-check fitsInRing (6 bordes) ---\n");
    {
        const std::size_t C = 432;
        check_bool("hueco aunque epoch<oldest", fitsInRing(100, C, 500.0, 100.0));
        check_bool("lleno+epoch>oldest", fitsInRing(C, C, 500.0, 600.0));
        check_bool("lleno+epoch==oldest false", !fitsInRing(C, C, 500.0, 500.0));
        check_bool("lleno+epoch<oldest false", !fitsInRing(C, C, 500.0, 100.0));
        check_bool("vacio true con oldest 0.0", fitsInRing(0, C, 0.0, 100.0));
        check_bool("viejo rige oldest sin ventana", !fitsInRing(C, C, 500.0, 1.0));
    }

    std::printf("\n[test_tec_cache] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
