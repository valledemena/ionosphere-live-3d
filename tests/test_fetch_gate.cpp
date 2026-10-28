// =============================================================================
// test_fetch_gate - C2a: puerta de red y persistencia de su estado.
//
// Test de logica pura: sin GL, sin GLFW, sin red, sin curl. Llama a
// PRODUCCION (namespace fetchgate en src/Data/FetchGate.{h,cpp} y
// namespace fetchstate en src/Data/FetchState.{h,cpp}), no replica.
//
// Cubre los casos que fallaron en la revision: cache joven que no debe pedir,
// reinicio (estado persistido), bloqueo tras no-200, backoff de transporte,
// recorte de reloj y clasificacion de 3xx/403/404/429/5xx.
// =============================================================================
#include "Data/FetchGate.h"
#include "Data/FetchState.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static int g_ok = 0, g_fail = 0;

static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static void check_near(const char* what, double got, double want, double tol = 1e-6) {
    const bool ok = std::fabs(got - want) <= tol;
    if (ok) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s: got %.3f want %.3f\n", ok ? "OK" : "FAIL", what, got, want);
}

static fetchgate::Decision d_now(double now, const fetchgate::State& st, double age,
                                 double jitter = 0.0) {
    return fetchgate::decide(now, st, age, jitter);
}

static std::string tmpPath(const char* tag, int n) {
    namespace fs = std::filesystem;
    static int seq = 0;
    (void)n;
    return (fs::temp_directory_path() /
            ("fetch_gate_test_" + std::string(tag) + "_" + std::to_string(seq++))).string();
}

int main() {
    const double now = 1.79e9;   // epoch fijo: el test no depende del reloj real

    std::printf("--- C2a-1: sin cache, primer arranque -> FetchNow ---\n");
    {
        fetchgate::State st;
        const auto d = d_now(now, st, -1.0);
        check_bool("accion FetchNow", d.action == fetchgate::Action::FetchNow);
        check_near("fetchAt = now", d.fetchAtUtc, now);
    }

    std::printf("--- C2a-2: cache viva (<6 h) -> CERO red ---\n");
    {
        fetchgate::State st;
        check_bool("cache 0 h -> CacheOnly", d_now(now, st, 0.0).action ==
                                                fetchgate::Action::CacheOnly);
        check_bool("cache 1 h -> CacheOnly", d_now(now, st, 3600.0).action ==
                                                fetchgate::Action::CacheOnly);
        check_bool("cache 5.99 h -> CacheOnly", d_now(now, st, 5.99 * 3600.0).action ==
                                                    fetchgate::Action::CacheOnly);
    }

    std::printf("--- C2a-3: cache 6..72 h -> FetchJittered ---\n");
    {
        fetchgate::State st;
        const auto d0 = d_now(now, st, 6.01 * 3600.0, 0.0);
        check_bool("6.01 h -> FetchJittered", d0.action == fetchgate::Action::FetchJittered);
        check_near("jitter 0 -> fetchAt = now", d0.fetchAtUtc, now);

        const auto d600 = d_now(now, st, 6.01 * 3600.0, 600.0);
        check_near("jitter 600 -> fetchAt = now+600", d600.fetchAtUtc, now + 600.0);

        check_bool("71.9 h -> FetchJittered",
                   d_now(now, st, 71.9 * 3600.0).action == fetchgate::Action::FetchJittered);
        check_bool("72.1 h -> FetchNow",
                   d_now(now, st, 72.1 * 3600.0).action == fetchgate::Action::FetchNow);
    }

    std::printf("--- C2a-4: jitter acotado ---\n");
    {
        fetchgate::State st;
        check_near("jitter negativo se acota a 0",
                   d_now(now, st, 8.0 * 3600.0, -500.0).fetchAtUtc, now);
        check_near("jitter enorme se acota a 1200 s",
                   d_now(now, st, 8.0 * 3600.0, 99999.0).fetchAtUtc,
                   now + fetchgate::kJitterMaxSec);
    }

    std::printf("--- C2a-5: bloqueo persistido ---\n");
    {
        fetchgate::State st;
        st.blockedUntilUtc = now + 3600.0;
        const auto d = d_now(now, st, 60.0);   // cache viva, pero bloqueado
        check_bool("bloqueo gana a cache viva", d.action == fetchgate::Action::Blocked);
        check_near("fetchAt = blockedUntil", d.fetchAtUtc, now + 3600.0);

        st.blockedUntilUtc = now - 10.0;       // ya caducado
        check_bool("bloqueo caducado -> CacheOnly",
                   d_now(now, st, 60.0).action == fetchgate::Action::CacheOnly);
    }

    std::printf("--- C2a-6: recorte de reloj (until absurdo) ---\n");
    {
        check_near("until a +10 h se recorta a +2 h",
                   fetchgate::sanitizeBlockedUntil(now + 10.0 * 3600.0, now),
                   now + fetchgate::kBlockSkewSec);
        check_near("until negativo -> 0", fetchgate::sanitizeBlockedUntil(-5.0, now), 0.0);
        check_near("until razonable se respeta",
                   fetchgate::sanitizeBlockedUntil(now + 600.0, now), now + 600.0);
        fetchgate::State st;
        st.blockedUntilUtc = now + 10.0 * 3600.0;
        check_near("decide tambien lo recorta",
                   d_now(now, st, -1.0).fetchAtUtc, now + fetchgate::kBlockSkewSec);
    }

    std::printf("--- C2a-7: backoff de transporte ---\n");
    {
        fetchgate::State st;
        st.lastAttemptUtc = now - 120.0;   // hace 2 min
        st.retryFails = 1;                 // backoff 5 min
        check_bool("cache vieja + fallo hace 2 min -> CacheOnly",
                   d_now(now, st, 8.0 * 3600.0).action == fetchgate::Action::CacheOnly);

        st.lastAttemptUtc = now - 301.0;
        check_bool("pasado el backoff -> FetchJittered",
                   d_now(now, st, 8.0 * 3600.0).action == fetchgate::Action::FetchJittered);

        st.retryFails = 0;
        st.lastAttemptUtc = now - 120.0;
        check_bool("sin fallos, el intento previo no bloquea",
                   d_now(now, st, 8.0 * 3600.0).action == fetchgate::Action::FetchJittered);
    }

    std::printf("--- C2a-8: progresion del backoff 5 -> 60 min ---\n");
    {
        check_near("1 fallo = 5 min", fetchgate::transportBackoffSec(1), 300.0);
        check_near("2 fallos = 10 min", fetchgate::transportBackoffSec(2), 600.0);
        check_near("3 fallos = 20 min", fetchgate::transportBackoffSec(3), 1200.0);
        check_near("4 fallos = 40 min", fetchgate::transportBackoffSec(4), 2400.0);
        check_near("5 fallos = 60 min", fetchgate::transportBackoffSec(5), 3600.0);
        check_near("20 fallos = 60 min (tope)", fetchgate::transportBackoffSec(20), 3600.0);
        check_near("0 fallos = 0", fetchgate::transportBackoffSec(0), 0.0);
    }

    std::printf("--- C2a-9: determinismo ---\n");
    {
        fetchgate::State st;
        st.lastAttemptUtc = now - 900.0;
        const auto a = d_now(now, st, 8.0 * 3600.0, 321.0);
        const auto b = d_now(now, st, 8.0 * 3600.0, 321.0);
        check_bool("misma entrada -> misma accion", a.action == b.action);
        check_near("misma entrada -> mismo instante", a.fetchAtUtc, b.fetchAtUtc);
    }

    std::printf("--- C2a-10: classify (200 / 3xx / 403 / 404 / 429 / 5xx) ---\n");
    {
        const auto ok200 = fetchgate::classify(200, 0, 0.0);
        check_bool("200 no bloquea", !ok200.block);
        check_bool("200 no es transporte", !ok200.transport);

        const auto r301 = fetchgate::classify(301, 0, 0.0);
        check_bool("301 bloquea", r301.block);
        check_bool("301 se rotula URL obsoleta",
                   std::string(r301.label).find("URL obsoleta") != std::string::npos);
        check_bool("302 bloquea", fetchgate::classify(302, 0, 0.0).block);
        check_bool("308 bloquea", fetchgate::classify(308, 0, 0.0).block);

        check_bool("403 bloquea", fetchgate::classify(403, 0, 0.0).block);
        check_bool("404 bloquea", fetchgate::classify(404, 0, 0.0).block);
        check_bool("204 tambien bloquea (no es 200)", fetchgate::classify(204, 0, 0.0).block);

        const auto r429 = fetchgate::classify(429, 0, 120.0);
        check_bool("429 bloquea", r429.block);
        check_near("429 conserva Retry-After", r429.retryAfterSec, 120.0);

        check_bool("500 bloquea", fetchgate::classify(500, 0, 0.0).block);
        check_bool("503 bloquea", fetchgate::classify(503, 0, 0.0).block);

        const auto t28 = fetchgate::classify(0, 28, 0.0);   // timeout
        check_bool("timeout es transporte", t28.transport);
        check_bool("timeout NO bloquea", !t28.block);

        const auto t6 = fetchgate::classify(0, 6, 0.0);     // DNS
        check_bool("DNS es transporte", t6.transport);

        const auto t0 = fetchgate::classify(0, 0, 0.0);
        check_bool("sin respuesta es transporte", t0.transport);
    }

    std::printf("--- C2a-11: duracion del bloqueo ---\n");
    {
        check_near("sin Retry-After = 2 h",
                   fetchgate::blockDurationSec(fetchgate::classify(403, 0, 0.0)),
                   fetchgate::kBlockSec);
        check_near("Retry-After 120 s manda",
                   fetchgate::blockDurationSec(fetchgate::classify(429, 0, 120.0)), 120.0);
        check_near("Retry-After 30 s se ignora (minimo 1 min)",
                   fetchgate::blockDurationSec(fetchgate::classify(429, 0, 30.0)),
                   fetchgate::kBlockSec);
        check_near("Retry-After absurdo se acota a 24 h",
                   fetchgate::blockDurationSec(fetchgate::classify(429, 0, 1e7)), 86400.0);
        check_near("503 = 2 h",
                   fetchgate::blockDurationSec(fetchgate::classify(503, 0, 0.0)),
                   fetchgate::kBlockSec);
    }

    std::printf("--- C2a-12: sidecar de estado (round-trip) ---\n");
    {
        const std::string path = tmpPath("state", 0);
        std::filesystem::remove(path);

        fetchgate::State in;
        in.lastAttemptUtc = now - 600.0;
        in.lastSuccessUtc = now - 700.0;
        in.blockedUntilUtc = now + 1800.0;
        in.lastHttp = 403;
        in.lastCurl = 0;
        in.retryFails = 2;

        check_bool("save escribe el sidecar", fetchstate::save(path, in));

        fetchgate::State out;
        check_bool("load relee el sidecar", fetchstate::load(path, out));
        check_near("last_attempt", out.lastAttemptUtc, in.lastAttemptUtc);
        check_near("last_success", out.lastSuccessUtc, in.lastSuccessUtc);
        check_near("blocked_until", out.blockedUntilUtc, in.blockedUntilUtc);
        check_bool("last_http", out.lastHttp == 403);
        check_bool("last_curl", out.lastCurl == 0);
        check_bool("retry_fails", out.retryFails == 2);

        std::filesystem::remove(path);
        fetchgate::State missing;
        check_bool("load de fichero ausente -> false", !fetchstate::load(path, missing));
    }

    std::printf("--- C2a-13: sidecar tolerante a basura ---\n");
    {
        const std::string path = tmpPath("junk", 0);
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f << "# fetch-state v1\n";
            f << "last_attempt=123.5\n";
            f << "clave_desconocida=lo_que_sea\n";
            f << "retry_fails=-7\n";
            f << "linea_sin_igual\n";
            f << "\n";
        }
        fetchgate::State out;
        check_bool("basura: load no falla", fetchstate::load(path, out));
        check_near("basura: conserva lo legible", out.lastAttemptUtc, 123.5);
        check_bool("basura: retry_fails negativo se acota a 0", out.retryFails == 0);
        std::filesystem::remove(path);

        const std::string empty = tmpPath("empty", 0);
        {
            std::ofstream f(empty, std::ios::binary | std::ios::trunc);
            f << "# fetch-state v1\n";
        }
        fetchgate::State none;
        check_bool("sin claves conocidas -> false", !fetchstate::load(empty, none));
        std::filesystem::remove(empty);
    }

    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
