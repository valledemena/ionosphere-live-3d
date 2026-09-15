// =============================================================================
// test_irtam_state — estados honestos P5 + replay frozen-declarado (C9/G9).
//
// Puro, sin red, sin GL. Llama a PRODUCCION (irtamstate), no replica.
// Cubre: sin-dato con/sin motivo, badge con edad, sufijo replay, nunca vacio.
// =============================================================================
#include "Data/IrtamState.h"

#include <cstdio>
#include <string>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

int main() {
    using namespace irtamstate;

    std::printf("--- P5: estados honestos ---\n");
    {
        const std::string s = badgeLine("foF2", false, 0.0, "", -1.0, false);
        check_bool("sin dato: menciona backfill",
                   s.find("backfill en curso") != std::string::npos);
        check_bool("sin dato: nunca vacio", !s.empty());
    }
    {
        // 429/504/parse-fail viajan en la linea, no solo en tooltip (P5.1).
        const std::string s =
            badgeLine("hmF2", false, 0.0, "http 429", -1.0, false);
        check_bool("error 429 visible en linea",
                   s.find("429") != std::string::npos);
        check_bool("error: reintento pasivo (sin re-burst)",
                   s.find("reintento pasivo") != std::string::npos);
    }
    {
        const std::string s =
            badgeLine("foF2", true, 1000.0, "", 4320.0, false);
        check_bool("con dato: edad andante",
                   s.find("4320 min") != std::string::npos);
        check_bool("con dato: sin nota replay en live",
                   s.find("fuera de ventana") == std::string::npos);
    }
    std::printf("--- P5: replay frozen-declarado (C9) ---\n");
    {
        // Mismo bundle en live y replay: solo cambia el sufijo (C9: IRTAM
        // NO participa del muestreo temporal; cero solape 24h vs 72h).
        const std::string live =
            badgeLine("foF2", true, 1000.0, "", 4320.0, false);
        const std::string rep =
            badgeLine("foF2", true, 1000.0, "", 4320.0, true);
        check_bool("replay: nota frozen-declarada",
                   rep.find("fuera de ventana de replay") !=
                       std::string::npos);
        check_bool("replay: conserva edad del dato",
                   rep.find("4320 min") != std::string::npos);
        check_bool("replay: determinista (mismo input, mismo output)",
                   badgeLine("foF2", true, 1000.0, "", 4320.0, true) == rep);
        check_bool("live != replay solo por el sufijo",
                   rep.size() > live.size() &&
                       rep.find(live) != std::string::npos);
    }
    {
        // Sin dato en replay: sigue honesto, sin nota de congelado.
        const std::string s = badgeLine("foF2", false, 0.0, "", -1.0, true);
        check_bool("replay sin dato: honesto",
                   s.find("sin datos") != std::string::npos);
    }

    std::printf("\n[test_irtam_state] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
