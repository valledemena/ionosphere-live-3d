// =============================================================================
// test_fetch_gate - C2a: puerta de red y persistencia de su estado.
//
// Test de logica pura: sin GL, sin GLFW, sin red, sin curl. Llama a
// PRODUCCION (namespace fetchgate en src/Data/FetchGate.{h,cpp} y
// namespace fetchstate en src/Data/FetchState.{h,cpp}), no replica.
//
// Cubre los casos que fallaron en la revision: cache joven que no debe pedir,
// reinicio (estado persistido), bloqueo tras no-200, backoff de transporte,
// recorte de reloj, clasificacion de 3xx/403/404/429/5xx, jitter anclado al
// cruce del TTL (con propiedad y simulacion de 80 h), 200 ilegible como
// bloqueo, intento en vuelo persistido (write-ahead), claves por endpoint y
// rutas con caracteres no ASCII.
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

// Jitter determinista para la simulacion: la produccion lo deriva del epoch de
// la cache (TleAdapter.cpp); aqui basta con que sea estable por cache y caiga
// en 0..kJitterMaxSec, que es lo unico que la puerta necesita.
static double simJitter(double cacheEpoch) {
    const unsigned long long h = (unsigned long long)(cacheEpoch / 60.0);
    return (double)((h * 2654435761ULL) % 1201ULL);
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

    std::printf("--- C2a-3: jitter ANCLADO al cruce del TTL (6..72 h) ---\n");
    {
        // Regla: eligibleAt = epoch + 6 h + jitter, NO "ahora + jitter". Con
        // "ahora + jitter" cada reevaluacion posponia otra vez y el TTL
        // efectivo pasaba a 72 h (bug de la primera version de C2a).
        fetchgate::State st;
        const double age = fetchgate::kLiveTtlSec + 60.0;   // 6 h y 1 min

        const auto d600 = d_now(now, st, age, 600.0);
        check_bool("6 h 1 min + jitter 600 -> FetchJittered",
                   d600.action == fetchgate::Action::FetchJittered);
        check_near("se ancla al TTL: fetchAt = now - 60 + 600", d600.fetchAtUtc, now + 540.0);

        check_bool("6 h 1 min + jitter 0 -> ya vencido, FetchNow",
                   d_now(now, st, age, 0.0).action == fetchgate::Action::FetchNow);

        // Una cache de 7 h se refresca YA, sin esperar el jitter: el ancla
        // quedo atras. Este es el caso que la version anterior nunca cumplia.
        check_bool("7 h -> FetchNow (no se pospone)",
                   d_now(now, st, 7.0 * 3600.0, fetchgate::kJitterMaxSec).action ==
                       fetchgate::Action::FetchNow);
        check_bool("71.9 h -> FetchNow",
                   d_now(now, st, 71.9 * 3600.0).action == fetchgate::Action::FetchNow);
        check_bool("72.1 h -> FetchNow",
                   d_now(now, st, 72.1 * 3600.0).action == fetchgate::Action::FetchNow);
    }

    std::printf("--- C2a-4: jitter acotado ---\n");
    {
        fetchgate::State st;
        const double age = fetchgate::kLiveTtlSec + 60.0;
        check_bool("jitter negativo: ya vencido -> FetchNow",
                   d_now(now, st, age, -500.0).action == fetchgate::Action::FetchNow);
        check_near("jitter enorme se acota a 1200 s",
                   d_now(now, st, age, 99999.0).fetchAtUtc,
                   now - 60.0 + fetchgate::kJitterMaxSec);
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
        // La cache tiene 8 h: su ancla de jitter ya vencio, asi que se pide ya.
        check_bool("pasado el backoff -> FetchNow",
                   d_now(now, st, 8.0 * 3600.0).action == fetchgate::Action::FetchNow);

        st.retryFails = 0;
        st.lastAttemptUtc = now - 120.0;
        check_bool("sin fallos, el intento previo no bloquea",
                   d_now(now, st, 8.0 * 3600.0).action == fetchgate::Action::FetchNow);
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

    std::printf("--- C2a-11b: 200 con cuerpo ilegible -> bloqueo ---\n");
    {
        const auto bad = fetchgate::unreadableBody();
        check_bool("respuesta ilegible bloquea", bad.block);
        check_bool("respuesta ilegible no es transporte", !bad.transport);
        check_bool("se rotula como ilegible",
                   std::string(bad.label).find("ilegible") != std::string::npos);
        check_near("bloqueo de 2 h", fetchgate::blockDurationSec(bad), fetchgate::kBlockSec);
    }

    std::printf("--- C2a-12: sidecar de estado (round-trip con clave) ---\n");
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

        check_bool("save escribe el sidecar",
                   fetchstate::save(path, "celestrak.active", in));

        fetchgate::State out;
        check_bool("load relee el sidecar",
                   fetchstate::load(path, "celestrak.active", out));
        check_near("last_attempt", out.lastAttemptUtc, in.lastAttemptUtc);
        check_near("last_success", out.lastSuccessUtc, in.lastSuccessUtc);
        check_near("blocked_until", out.blockedUntilUtc, in.blockedUntilUtc);
        check_bool("last_http", out.lastHttp == 403);
        check_bool("last_curl", out.lastCurl == 0);
        check_bool("retry_fails", out.retryFails == 2);

        // Segunda puerta en el MISMO fichero: no debe borrar la primera.
        fetchgate::State other;
        other.lastAttemptUtc = now - 30.0;
        other.lastHttp = 429;
        other.retryFails = 1;
        check_bool("save de otra clave",
                   fetchstate::save(path, "celestrak.satnogs", other));

        fetchgate::State again;
        check_bool("la primera puerta sobrevive al save de la segunda",
                   fetchstate::load(path, "celestrak.active", again));
        check_bool("la primera conserva su 403", again.lastHttp == 403);
        check_bool("la primera conserva su retry_fails", again.retryFails == 2);

        fetchgate::State sat;
        check_bool("la segunda se lee con su clave",
                   fetchstate::load(path, "celestrak.satnogs", sat));
        check_bool("la segunda conserva su 429", sat.lastHttp == 429);

        fetchgate::State absent;
        check_bool("clave inexistente -> false",
                   !fetchstate::load(path, "celestrak.stations", absent));
        check_bool("prefijo parcial ('celestrak') no lee claves ajenas",
                   !fetchstate::load(path, "celestrak", absent));

        std::filesystem::remove(path);
        fetchgate::State missing;
        check_bool("load de fichero ausente -> false",
                   !fetchstate::load(path, "celestrak.active", missing));
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
        // Clave vacia = formato antiguo sin prefijo (migracion).
        fetchgate::State out;
        check_bool("basura: load no falla", fetchstate::load(path, "", out));
        check_near("basura: conserva lo legible", out.lastAttemptUtc, 123.5);
        check_bool("basura: retry_fails negativo se acota a 0", out.retryFails == 0);
        std::filesystem::remove(path);

        const std::string empty = tmpPath("empty", 0);
        {
            std::ofstream f(empty, std::ios::binary | std::ios::trunc);
            f << "# fetch-state v1\n";
        }
        fetchgate::State none;
        check_bool("sin claves conocidas -> false", !fetchstate::load(empty, "", none));
        std::filesystem::remove(empty);
    }

    std::printf("--- C2a-14: PROPIEDAD - reevaluar en fetchAt permite red ---\n");
    {
        // La invariante que rompio la primera version: si decide() devuelve un
        // instante de reintento, al llegar a ese instante la misma cache ya no
        // puede volver a posponer. Se recorre una malla de edades y jitters.
        int cases = 0, badAction = 0, badBound = 0, badAnchor = 0;
        const double ages[] = {6.0 * 3600.0,       6.0 * 3600.0 + 1.0,
                               6.0 * 3600.0 + 600, 7.0 * 3600.0,
                               12.0 * 3600.0,      24.0 * 3600.0,
                               48.0 * 3600.0,      71.9 * 3600.0};
        const double jitters[] = {0.0, 1.0, 300.0, 900.0, 1200.0};
        fetchgate::State st;
        for (double age0 : ages) {
            for (double j : jitters) {
                const auto d = fetchgate::decide(now, st, age0, j);
                ++cases;
                if (d.action == fetchgate::Action::FetchJittered) {
                    // El reintento nunca debe caer mas alla del jitter maximo.
                    if (d.fetchAtUtc > now + fetchgate::kJitterMaxSec + 1e-6) ++badBound;
                    const double age1 = age0 + (d.fetchAtUtc - now);
                    const auto at = fetchgate::decide(d.fetchAtUtc, st, age1, j);
                    if (at.action != fetchgate::Action::FetchNow) ++badAction;
                } else if (d.action == fetchgate::Action::FetchNow) {
                    // Y si ya permite red, el ancla quedo atras de verdad.
                    const double eligible = (now - age0) + fetchgate::kLiveTtlSec + j;
                    if (now < eligible - 1e-6) ++badAnchor;
                }
            }
        }
        check_bool("malla de 40 casos evaluada", cases == 40);
        check_bool("fetchAt nunca supera now+jitter_max", badBound == 0);
        check_bool("en fetchAt la accion permite red", badAction == 0);
        check_bool("FetchNow solo cuando el ancla ya vencio", badAnchor == 0);
    }

    std::printf("--- C2a-15: simulacion 80 h con red OK (1 peticion / 2 h) ---\n");
    {
        fetchgate::State st;
        double cacheEpoch = 0.0;      // sin cache
        double prevReq = -1.0;
        double minGap = 1e18, maxGap = 0.0;
        int requests = 0;
        int perWindow[48] = {0};
        const double t0 = now;
        for (double t = t0; t <= t0 + 80.0 * 3600.0; t += 60.0) {
            const bool haveCache = cacheEpoch > 0.0;
            const double age = haveCache ? (t - cacheEpoch) : -1.0;
            const double jitter = haveCache ? simJitter(cacheEpoch) : 0.0;
            const auto d = fetchgate::decide(t, st, age, jitter);
            const bool fire = (d.action == fetchgate::Action::FetchNow) ||
                              (d.action == fetchgate::Action::FetchJittered &&
                               d.fetchAtUtc <= t);
            if (!fire) continue;
            ++requests;
            if (prevReq >= 0.0) {
                const double gap = t - prevReq;
                if (gap < minGap) minGap = gap;
                if (gap > maxGap) maxGap = gap;
            }
            prevReq = t;
            const int w = (int)((t - t0) / 7200.0);
            if (w >= 0 && w < 48) ++perWindow[w];
            // Red OK: se instala cache nueva con epoch = t.
            cacheEpoch = t;
            st.lastAttemptUtc = t;
            st.lastSuccessUtc = t;
            st.retryFails = 0;
            st.blockedUntilUtc = 0.0;
        }
        bool onePerWindow = true;
        for (int w = 0; w < 41; ++w) {
            if (perWindow[w] > 1) onePerWindow = false;
        }
        std::printf("    peticiones=%d  gap min=%.0f s  max=%.0f s\n", requests, minGap, maxGap);
        check_bool("80 h: entre 12 y 15 peticiones", requests >= 12 && requests <= 15);
        check_bool("maximo 1 peticion por ventana de 2 h", onePerWindow);
        check_bool("ningun ciclo se salta el TTL de 6 h",
                   minGap >= fetchgate::kLiveTtlSec - 1.0);
        check_bool("el jitter no retrasa mas de 20 min",
                   maxGap <= fetchgate::kLiveTtlSec + fetchgate::kJitterMaxSec + 60.0);
    }

    std::printf("--- C2a-16: simulacion con 403 (bloqueo de 2 h) ---\n");
    {
        fetchgate::State st;
        // t0 sin cache -> FetchNow; la respuesta es 403.
        check_bool("sin cache se pide", fetchgate::decide(now, st, -1.0, 0.0).action ==
                                            fetchgate::Action::FetchNow);
        st.lastAttemptUtc = now;
        st.lastHttp = 403;
        st.blockedUntilUtc = now + fetchgate::blockDurationSec(fetchgate::classify(403, 0, 0.0));

        int askedDuringBlock = 0;
        for (double u = now + 60.0; u < now + fetchgate::kBlockSec; u += 60.0) {
            if (fetchgate::decide(u, st, -1.0, 0.0).action != fetchgate::Action::Blocked) {
                ++askedDuringBlock;
            }
        }
        check_bool("403: cero peticiones durante 2 h", askedDuringBlock == 0);
        check_bool("403: a las 2 h vuelve a permitir",
                   fetchgate::decide(now + fetchgate::kBlockSec, st, -1.0, 0.0).action ==
                       fetchgate::Action::FetchNow);
    }

    std::printf("--- C2a-17: write-ahead: intento en vuelo y reinicio ---\n");
    {
        // Antes de la descarga se persiste un bloqueo PROVISIONAL (2 h,
        // lastHttp = -1). Si la app muere a mitad, el reinicio no debe repetir
        // la peticion. Con retryFails == 0, lastAttempt solo no basta.
        fetchgate::State st;
        st.lastAttemptUtc = now;
        st.blockedUntilUtc = now + fetchgate::kBlockSec;
        st.lastHttp = -1;
        st.retryFails = 0;
        check_bool("en vuelo -> Blocked aunque retryFails == 0",
                   d_now(now, st, -1.0).action == fetchgate::Action::Blocked);

        const std::string path = tmpPath("inflight", 0);
        check_bool("estado en vuelo persistido",
                   fetchstate::save(path, "celestrak.active", st));
        fetchgate::State restored;
        check_bool("estado en vuelo restaurado",
                   fetchstate::load(path, "celestrak.active", restored));
        restored.blockedUntilUtc =
            fetchgate::sanitizeBlockedUntil(restored.blockedUntilUtc, now + 60.0);
        check_bool("tras reinicio sigue bloqueado",
                   d_now(now + 60.0, restored, -1.0).action == fetchgate::Action::Blocked);
        check_bool("el provisional caduca a las 2 h",
                   d_now(now + fetchgate::kBlockSec + 1.0, restored, -1.0).action ==
                       fetchgate::Action::FetchNow);
        std::filesystem::remove(path);

        // Exito: se retira el provisional y queda libre.
        fetchgate::State ok = st;
        ok.blockedUntilUtc = 0.0;
        ok.lastHttp = 200;
        ok.lastSuccessUtc = now;
        ok.retryFails = 0;
        check_bool("tras exito se libera (FetchNow sin cache)",
                   d_now(now + 1.0, ok, -1.0).action == fetchgate::Action::FetchNow);

        // Fallo blando: se retira el provisional y manda el backoff.
        fetchgate::State soft = st;
        soft.blockedUntilUtc = 0.0;
        soft.retryFails = 1;
        soft.lastHttp = 0;
        check_bool("tras fallo blando -> backoff, no bloqueo de 2 h",
                   d_now(now + 1.0, soft, -1.0).action == fetchgate::Action::CacheOnly);
    }

    std::printf("--- C2a-18: ruta de datos con caracteres no ASCII ---\n");
    {
        // Regresion del caso que el arreglo de rutas pretendia cubrir: si la
        // ruta viaja como std::string UTF-8, el .tmp se crea con otro nombre y
        // el rename falla. Con fs::path el nombre va en wchar_t en Windows.
        namespace fs = std::filesystem;
#if defined(_WIN32)
        const fs::path sub = L"fetch_gate_Caf\u00e9_\u00d1andu";
#else
        const fs::path sub = "fetch_gate_Caf\u00e9_\u00d1andu";
#endif
        const fs::path dir = fs::temp_directory_path() / sub;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        check_bool("se crea el directorio no ASCII", fs::is_directory(dir, ec));

        const fs::path state = dir / "estado";
        fetchgate::State in;
        in.lastAttemptUtc = now - 10.0;
        in.lastHttp = 403;
        in.retryFails = 3;
        check_bool("save en ruta no ASCII", fetchstate::save(state, "celestrak.active", in));

        fetchgate::State out;
        check_bool("load en ruta no ASCII", fetchstate::load(state, "celestrak.active", out));
        check_bool("conserva last_http", out.lastHttp == 403);
        check_near("conserva last_attempt", out.lastAttemptUtc, in.lastAttemptUtc);

        // El tmp debe haber desaparecido: si el rename hubiera fallado por el
        // nombre, seguiria ahi y el estado no se habria persistido.
        fs::path leftover = state;
        leftover += ".tmp";
        check_bool("el .tmp no queda huerfano", !fs::exists(leftover));

#if defined(_WIN32)
        const std::wstring native = state.native();
        check_bool("la ruta de prueba no es ASCII",
                   native.find(L'\u00e9') != std::wstring::npos);
#endif
        fs::remove_all(dir, ec);
    }

    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
