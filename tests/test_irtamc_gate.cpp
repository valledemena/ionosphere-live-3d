// =============================================================================
// test_irtamc_gate — gate lgdc compartido (M-IRTAM-F2 P3, C1, spec-hija).
//
// Test de logica pura: sin GL, sin red, reloj SIMULADO (nowMs explicito).
// Llama a PRODUCCION (namespace lgdcpacing en src/Data/LgdcPacing.{h,cpp}),
// no replica. Cubre F2 G8: primitiva unica, gap >= 15 s desde CUALQUIER
// lanzamiento previo, secuencia catalogo->getbest->gambit, rechazo en burst,
// semantica forward-only, arranque frio.
//
// El pacing M4 de getbest NO se toca aqui (regresion cubierta por los tests
// existentes + ctest global): este test prueba el gate, no el adapter.
// =============================================================================
#include "Data/LgdcPacing.h"

#include <cstdio>

static int g_ok = 0, g_fail = 0;

static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

int main() {
    using lgdcpacing::canLaunchGambit;
    using lgdcpacing::kGambitGapMs;
    using lgdcpacing::lastLaunchMs;
    using lgdcpacing::msSinceLastLaunch;
    using lgdcpacing::recordLaunch;
    using lgdcpacing::resetForTest;

    std::printf("--- F2 P3: arranque frio + borde 15 s ---\n");
    resetForTest();
    check_bool("gap declarado 15000 ms", kGambitGapMs == 15000);
    check_bool("frio: last == 0", lastLaunchMs() == 0);
    check_bool("frio: gambit permitido", canLaunchGambit(1000000));

    resetForTest();
    recordLaunch(1000000);
    check_bool("registra last", lastLaunchMs() == 1000000);
    check_bool("gap 0 recien registrado", msSinceLastLaunch(1000000) == 0);
    check_bool("14999 ms: denegado", !canLaunchGambit(1000000 + 14999));
    check_bool("15000 ms: permitido (borde >=)", canLaunchGambit(1000000 + 15000));
    check_bool("15001 ms: permitido", canLaunchGambit(1000000 + 15001));

    std::printf("--- F2 P3: secuencia catalogo->getbest->gambit ---\n");
    resetForTest();
    recordLaunch(2000000);          // catalogo (fetchCatalog instrumentado)
    check_bool("tras catalogo: gambit denegado a +1 s",
               !canLaunchGambit(2000000 + 1000));
    recordLaunch(2000000 + 5000);   // getbest (punto CAS instrumentado)
    check_bool("last = getbest (el mas reciente)", lastLaunchMs() == 2005000);
    // El gap se mide desde CUALQUIER previo: el mas reciente manda.
    check_bool("gambit a +14 s del catalogo pero +9 s del getbest: denegado",
               !canLaunchGambit(2000000 + 14000));
    check_bool("gambit a +15 s del getbest: permitido",
               canLaunchGambit(2005000 + 15000));
    check_bool("gambit a +15 s - 1 ms del getbest: denegado",
               !canLaunchGambit(2005000 + 14999));

    std::printf("--- F2 P3: burst + forward-only ---\n");
    resetForTest();
    recordLaunch(3000000);
    check_bool("burst a +10 ms: denegado", !canLaunchGambit(3000010));
    check_bool("burst a +1000 ms: denegado", !canLaunchGambit(3001000));
    recordLaunch(3000000 - 5000);   // registro tardio (worker fuera de orden)
    check_bool("stale no retrasa el reloj", lastLaunchMs() == 3000000);
    check_bool("gap sigue midiendose desde el maximo",
               msSinceLastLaunch(3015000) == 15000);
    check_bool("tras ventana: permitido", canLaunchGambit(3015000));
    resetForTest();
    check_bool("reset aisla casos", lastLaunchMs() == 0);

    std::printf("\n[test_irtamc_gate] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
