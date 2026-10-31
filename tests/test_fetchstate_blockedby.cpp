// =============================================================================
// test_fetchstate_blockedby - ida/vuelta de la clave tolerante `blocked_by`
// (FetchState::save/load) + tolerancia a sidecar antiguo y valor invalido.
// Sin red, sin GL.
// =============================================================================
#include "Data/FetchState.h"
#include "Data/FetchGate.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* w, bool c) { (c ? g_ok : g_fail)++; printf("[%s] %s\n", c ? "OK":"FAIL", w); }
static void check_str(const char* w, const std::string& a, const std::string& b) {
    bool c = (a == b); (c ? g_ok : g_fail)++; printf("[%s] %s: %s == %s (%s)\n", c ? "OK":"FAIL", w, a.c_str(), b.c_str(), c?"":"MISS");
}
static std::string fresh() {
    static int n=0;
    char b[256]; snprintf(b, sizeof(b), "fs_bb_%d_%d", (int)(std::time(nullptr) & 0xffff), n++);
    std::filesystem::path d = std::filesystem::temp_directory_path() / b;
    std::filesystem::remove_all(d); std::filesystem::create_directories(d);
    return (d / "state").string();
}

int main() {
    std::filesystem::path sp;
    std::string side = fresh();
    sp = side;

    // Ida/vuelta "host".
    {
        fetchgate::State st;
        st.lastAttemptUtc = 1793349002.0;
        st.blockedUntilUtc = 1793349002.0 + 7200.0;
        st.lastHttp = 500;
        st.blockedBy = "host";
        fetchstate::save(sp, "celestrak.amateur", st);
        fetchgate::State out;
        bool ok = fetchstate::load(sp, "celestrak.amateur", out);
        check_bool("carga OK", ok);
        check_str("ida/vuelta blockedBy", out.blockedBy, "host");
        check_bool("ida/vuelta lastAttempt", out.lastAttemptUtc == st.lastAttemptUtc);
    }

    // Id ida/vuelta "own".
    {
        fetchgate::State st;
        st.lastAttemptUtc = 1793349002.0;
        st.lastCurl = 28;
        st.blockedBy = "own";
        fetchstate::save(sp, "celestrak.stations", st);
        fetchgate::State out;
        fetchstate::load(sp, "celestrak.stations", out);
        check_str("own", out.blockedBy, "own");
        check_bool("curl propagado", out.lastCurl == 28);
    }

    // Sidecar antiguo sin clave => default "own".
    {
        std::ofstream f(sp, std::ios::binary|std::ios::trunc);
        f << "# fetch-state v2\n";
        f << "celestrak.amateur.last_attempt=1793349002.0\n";
        f << "celestrak.amateur.last_success=1793349002.0\n";
        f << "celestrak.amateur.blocked_until=" << 1793349002.0 + 7200.0 << '\n';
        f << "celestrak.amateur.last_http=500\n";
        f << "celestrak.amateur.last_curl=0\n";
        f << "celestrak.amateur.retry_fails=0\n";
        f.close();
        fetchgate::State out;
        bool ok = fetchstate::load(sp, "celestrak.amateur", out);
        check_bool("carga antigua OK", ok);
        check_str("antiguo sin clave -> own", out.blockedBy, "own");
    }

    // Valor invalido -> "own".
    {
        std::ofstream f(sp, std::ios::binary|std::ios::trunc);
        f << "# fetch-state v2\n";
        f << "celestrak.amateur.last_attempt=1793349002.0\n";
        f << "celestrak.amateur.blocked_until=1793349002.0\n";
        f << "celestrak.amateur.blocked_by=origin\n"; // invalid
        f << "celestrak.amateur.last_success=0\n";
        f << "celestrak.amateur.last_http=0\n";
        f << "celestrak.amateur.last_curl=0\n";
        f << "celestrak.amateur.retry_fails=0\n";
        f.close();
        fetchgate::State out;
        fetchstate::load(sp, "celestrak.amateur", out);
        check_str("invalido -> own", out.blockedBy, "own");
    }

    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);
    std::filesystem::remove_all(sp.parent_path());
    return g_fail == 0 ? 0 : 1;
}
