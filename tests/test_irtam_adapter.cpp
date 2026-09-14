// =============================================================================
// test_irtam_adapter — planificacion del backfill gambit (M-IRTAM-F2 P4).
//
// Test de logica pura: sin GL, sin red, reloj EXPLICITO. Llama a PRODUCCION
// (namespace irtamfetch en src/Data/IrtamCoeffAdapter.{h,cpp}), no replica.
// Cubre F2 G10 (ventana/cadencia/plan incremental) y F2-R2 cond. 1 (bundle
// honesto: valid=false + grids vacios, jamas 46x45 de ceros).
//
// La red (fetchOnce/fetchURL) NO se testea aqui (G3): el gate que la regula
// esta cubierto en test_irtamc_gate con reloj simulado.
// =============================================================================
#include "Data/IrtamCoeffAdapter.h"
#include "Data/IrtamCoeffParse.h"  // solo parseIsoUtc (ancla del formato time=)

#include <cmath>
#include <cstdio>
#include <string>

static int g_ok = 0, g_fail = 0;

static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

int main() {
    using namespace irtamfetch;

    // Ancla: 2026-09-10T12:00:00Z via el parse de produccion (P1a).
    double t0 = 0.0;
    check_bool("parseIsoUtc ancla", irtamcoeff::parseIsoUtc("2026-09-10T12:00:00Z", t0));

    std::printf("--- F2 P4: slots + ventana + formato ---\n");
    check_bool("slot ya cuantizado queda igual", quantizeSlot(t0) == t0);
    check_bool("slot redondea a piso (+300 s)", quantizeSlot(t0 + 300.0) == t0);
    check_bool("slot redondea a piso (+899 s)", quantizeSlot(t0 + 899.0) == t0);
    check_bool("ventana = ahora - 3 d (lag E11)",
               windowEndFor(t0) == t0 - 259200.0);
    check_bool("formato time= del endpoint",
               formatGambitTime(t0) == "2026.09.10T12:00");
    // La URL completa vive SOLO en el adapter (G6): aqui se verifica el
    // cableado (time= cuantizado + charName) sin repetir el endpoint.
    const std::string u = IrtamCoeffAdapter::urlFor("foF2", t0);
    check_bool("url: lleva time= cuantizado",
               u.find("time=2026.09.10T12:00") != std::string::npos);
    check_bool("url: lleva charName",
               u.find("charName=foF2") != std::string::npos);

    std::printf("--- F2 P4: planMissing incremental ---\n");
    const double wend = windowEndFor(t0 + 86400.0);  // un dia despues: ventana corrida
    {
        CacheHave vacio;
        const std::vector<FetchTarget> p = planMissing(vacio, wend);
        check_bool("cache vacio: 192 targets (96 x 2)",
                   p.size() == 192);
        check_bool("orden: foF2 primero (param-major)",
                   p.front().param == "foF2" && p[96].param == "hmF2");
        check_bool("oldest-first dentro de foF2",
                   p[0].epoch == wend - 95.0 * 900.0 && p[95].epoch == wend);
    }
    {
        // Lleno: 96 slots x 2 params -> 0 faltantes (steady: nada que hacer).
        CacheHave lleno;
        for (int s = 0; s < 96; s++) {
            lleno.epochs["foF2"].push_back(wend - (double)s * 900.0);
            lleno.epochs["hmF2"].push_back(wend - (double)s * 900.0);
        }
        check_bool("cache lleno: 0 targets",
                   planMissing(lleno, wend).empty());
    }
    {
        // Parcial: solo faltan 3 (2 foF2 + 1 hmF2); el resto se respeta.
        CacheHave parcial;
        for (int s = 0; s < 96; s++) {
            const double slot = wend - (double)s * 900.0;
            if (s != 3 && s != 40) parcial.epochs["foF2"].push_back(slot);
            if (s != 90) parcial.epochs["hmF2"].push_back(slot);
        }
        const std::vector<FetchTarget> p = planMissing(parcial, wend);
        check_bool("parcial: exactamente los 3 faltantes", p.size() == 3);
    }

    std::printf("--- F2 P4: DATA-age + bundle honesto ---\n");
    check_bool("edad del dato: 3 d en minutos",
               std::fabs(dataAgeMin(t0 - 259200.0, t0) - 4320.0) < 1e-6);
    check_bool("sin buckets: edad -1 (indefinida, no 0)",
               dataAgeMin(0.0, t0) == -1.0);
    {
        const IrtamBundle b;  // P4 publica esto: F2-R2 cond. 1
        check_bool("bundle P4: valid=false", !b.valid);
        check_bool("bundle P4: foF2 vacio (jamas 46x45 de ceros)",
                   b.foF2.values.empty() && b.foF2.width == 0);
        check_bool("bundle P4: hmF2 vacio",
                   b.hmF2.values.empty() && b.hmF2.width == 0);
        check_bool("bundle P4: sin epoca de dato", b.dataEpoch == 0.0);
    }

    std::printf("\n[test_irtam_adapter] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
