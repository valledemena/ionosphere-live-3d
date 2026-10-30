// =============================================================================
// test_tle_group_panel - pure formatter de la fila `tlegroupstatus::formatRow`.
// Sin GL, sin red, sin disco. Se inyectan el estado y el reloj, igual que
// las otras suites puras (PeriodicWait, PeriodicWait no, etc.).
// =============================================================================
#include "Utils/TleGroupStatus.h"

#include <cmath>
#include <cstdio>
#include <string>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* w, bool c) { (c ? g_ok : g_fail)++; printf("[%s] %s\n", c ? "OK":"FAIL", w); }
static void check_str(const char* w, const std::string& s, const char* sub) {
    bool c = s.find(sub) != std::string::npos;
    (c ? g_ok : g_fail)++;
    printf("[%s] %s: \"%s\" contains \"%s\"\n", c ? "OK":"FAIL", w, s.c_str(), sub);
}

int main() {
    using namespace tlegroupstatus;

    std::printf("--- (1) grupo vivo con cache + HHMM UTC en bloqueo ---\n");
    {
        Status s;
        s.key = "stations";
        s.cacheEpoch = 1793349002.0;
        s.blockedUntilUtc = 1793349002.0 + 7200.0;
        s.lastHttp = 500;
        s.blockedBy = "host";
        const double now = 1793349002.0;
        const std::string row = formatRow(s, now);
        check_str("incluye la clave", row, "stations");
        check_str("muestra edad cache", row, "cache 0.0 h");
        check_str("bloqueo hasta HHMM", row, "blocked until 10:30 UTC");
        check_str("marca origen host", row, "(host)");
        check_str("resume HTTP", row, "HTTP 500");
    }

    std::printf("--- (2) grupo sin cache y sin bloqueo ---\n");
    {
        Status s;
        s.key = "amateur";
        s.blockedBy = "none";
        const std::string row = formatRow(s, 1793349002.0);
        check_str("sin cache", row, "no cache");
        check_bool("sin texto de bloqueo", row.find("blocked until") == std::string::npos);
    }

    std::printf("--- (3) blocked propio (origin) ---\n");
    {
        Status s;
        s.key = "active";
        s.cacheEpoch = 1793349002.0 - 6.0 * 3600.0;   // cache de hace 6 h
        s.blockedUntilUtc = 1793349002.0 + 3600.0;
        s.lastHttp = 404;
        s.blockedBy = "own";
        const std::string row = formatRow(s, 1793349002.0);
        check_str("bloqueado propio", row, "(own)");
        check_str("cache mas vieja", row, "cache 6.0 h");
    }

    std::printf("--- (4) edad nunca negativa (clock adelantado) ---\n");
    {
        Status s;
        s.key = "x";
        s.cacheEpoch = 1793349002.0 + 100.0;   // cache "del futuro"
        const std::string row = formatRow(s, 1793349002.0);
        check_str("no dice cache negativa", row, "no cache");
        check_bool("nunca expone edad negativa en el texto", row.find("cache -") == std::string::npos);
    }

    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}