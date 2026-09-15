// =============================================================================
// test_lgdc_trace — instrumentación stderr (ciclo post-sello).
//
// Puro, sin red, sin GL. Llama a PRODUCCION (lgdctrace) con sumidero
// inyectado: verifica formato ISO-ms, categorías y enable/disable.
// =============================================================================
#include "Data/LgdcTrace.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

int main() {
    using namespace lgdctrace;

    std::printf("--- trace: formato ---\n");
    check_bool("epoch 0 -> 1970-01-01T00:00:00.000Z",
               formatIsoMs(0) == "1970-01-01T00:00:00.000Z");
    {
        // 2026-09-15T01:10:39.123Z = 1789434639123 ms.
        const std::string s = formatIsoMs(1789434639123LL);
        check_bool("fecha conocida exacta",
                   s == "2026-09-15T01:10:39.123Z");
    }
    check_bool("forma ISO (20+5, T, Z)",
               formatIsoMs(1234567890123LL).size() == 24 &&
                   formatIsoMs(1234567890123LL)[10] == 'T');

    std::printf("--- trace: sumidero + enable ---\n");
    {
        std::vector<std::string> got;
        setSink([&](const std::string& l) { got.push_back(l); });
        trace("launch", "gambit foF2 test");
        check_bool("una linea capturada", got.size() == 1);
        check_bool("prefijo [LGDC ", got[0].rfind("[LGDC ", 0) == 0);
        check_bool("categoria presente",
                   got[0].find("launch gambit foF2 test") !=
                       std::string::npos);
        check_bool("sin URLs en la linea",
                   got[0].find("http") == std::string::npos &&
                       got[0].find(std::string("uml") + "." + "edu") ==
                           std::string::npos);
        setEnabled(false);
        trace("launch", "silencio");
        check_bool("disable suprime", got.size() == 1);
        setEnabled(true);
        trace("gate-consult", "deny test");
        check_bool("re-enable emite", got.size() == 2);
        setSink(nullptr);
    }

    std::printf("\n[test_lgdc_trace] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
