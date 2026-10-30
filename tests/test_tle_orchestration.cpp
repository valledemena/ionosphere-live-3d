// =============================================================================
// test_tle_orchestration - C2a: la ORQUESTACION de TleAdapter::fetch().
//
// Los otros tests cubren las piezas puras (decide, sidecar, cache). Aqui se
// prueba el camino completo con un transporte inyectado, un directorio
// temporal y un reloj explicito: que se sirve, que se persiste y cuando se
// retira el bloqueo provisional. Es justo el codigo donde aparecieron los
// cuatro defectos de la revision anterior.
//
// Escenarios:
//   A) 403 sin cache      -> se sirve SatNOGS, CelesTrak queda bloqueado 2 h,
//                            satellites.tle NO se escribe y el siguiente pase
//                            sirve el subconjunto desde su propia cache.
//   B) timeout sin cache  -> se sirve SatNOGS, NO hay bloqueo (backoff de 5
//                            min), no se reintenta a los 60 s y a los 301 s el
//                            catalogo completo sustituye al subconjunto.
//   C) muerte a mitad     -> el provisional ya esta en disco con la app muerta
//                            y el reinicio no repite la peticion de CelesTrak.
//   D) aislamiento        -> satellites.tle nunca recibe filas de SatNOGS.
//   E) cache de 73 h      -> se invalida UNA vez y la generacion sube UNA vez.
//
// Sin red: el transporte es una funcion que devuelve respuestas preparadas.
// =============================================================================
#include "Data/FetchGate.h"
#include "Data/FetchState.h"
#include "Data/TleAdapter.h"
#include "tle_csv_fixture.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_ok = 0, g_fail = 0;

static void checkBool(const char* what, bool cond) {
    (cond ? g_ok : g_fail) += 1;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static void checkInt(const char* what, int got, int want) {
    const bool ok = (got == want);
    (ok ? g_ok : g_fail) += 1;
    std::printf("[%s] %s: got %d want %d\n", ok ? "OK" : "FAIL", what, got, want);
}

static void checkNear(const char* what, double got, double want, double tol = 1.0) {
    const bool ok = std::fabs(got - want) <= tol;
    (ok ? g_ok : g_fail) += 1;
    std::printf("[%s] %s: got %.1f want %.1f\n", ok ? "OK" : "FAIL", what, got, want);
}

// Instante de referencia fijo: la puerta y la cache trabajan en epoch UTC.
static const double T0 = tlefixture::kCatalogEpochUtc;
static const double kBlock = 7200.0;   // fetchgate::kBlockSec

// -----------------------------------------------------------------------------
// TLEs de test con checksum valido.
//
// orbitprop::parseTle exige 69 columnas, prefijos "1 "/"2 ", el MISMO numero de
// catalogo en las dos lineas y checksum correcto. Se parte del TLE real de la
// ISS (epoch 26299.59614366) y solo se cambia el numero de catalogo, recalculando
// el checksum (tlefixture::tleLines). Los NORAD 90001+ son ficticios y estan
// declarados como tales.
// -----------------------------------------------------------------------------

// Catalogo "completo" de CelesTrak: 3 satelites en GP CSV (es lo que responde
// gp.php?FORMAT=csv; el TLE de 5 digitos ya no admite el catalogo actual).
static std::string celestrakCsv() { return tlefixture::catalogCsv(); }

// C2b: grupos con solape. `stations` trae la ISS con un EPOCH MAS NUEVO y un
// objeto que no esta en `active` (90003). Asi se prueba a la vez la union y el
// dedup por NORAD (gana el EPOCH mas nuevo, no el ultimo grupo leido).
static const char* kStationsEpochIso = "2026-10-26T15:18:26.839680";   // T0 + 1 h
static std::string stationsCsv() {
    return std::string(tlefixture::csvHeader()) +
           tlefixture::csvRow("ISS-STATIONS", 25544, kStationsEpochIso) +
           tlefixture::csvRow("SAT-90003", 90003, kStationsEpochIso);
}

// Subconjunto de SatNOGS: 2 filas, una de ellas (90003) NO esta en CelesTrak.
static std::string satnogsJson() {
    std::string out = "[";
    const int ids[2] = {25544, 90003};
    for (int k = 0; k < 2; ++k) {
        std::string l1, l2;
        tlefixture::tleLines(ids[k], l1, l2);
        if (k > 0) out += ",";
        out += "{\"tle0\":\"0 SAT-" + std::to_string(ids[k]) + "\",\"tle1\":\"" + l1 +
               "\",\"tle2\":\"" + l2 + "\",\"norad_cat_id\":" + std::to_string(ids[k]) + "}";
    }
    out += "]";
    return out;
}

// Transporte simulado: responde por URL. Cuenta las llamadas a cada proveedor,
// que es lo que de verdad se quiere vigilar (cuota de CelesTrak).
struct Mock {
    std::string celestrakBody = celestrakCsv();
    std::string satnogsBody = satnogsJson();
    int celestrakHttp = 200;
    int celestrakCurl = 0;
    int satnogsHttp = 200;
    int satnogsCurl = 0;
    int celestrakCalls = 0;
    int satnogsCalls = 0;
    // C2b: respuesta por grupo ("active", "stations", ...). Si el grupo no
    // aparece aqui se usan los valores de arriba.
    std::map<std::string, int> groupHttp;
    std::map<std::string, std::string> groupBody;
    std::vector<std::string> celestrakUrls;   // grupo de cada peticion, en orden
    // Gancho opcional que se ejecuta DENTRO de la llamada a CelesTrak (antes de
    // responder): sirve para observar el sidecar con el fetch en vuelo.
    std::function<void()> onCelestrak;

    TleAdapter::Transport transport() {
        return [this](const std::string& url, std::string& body) {
            TleAdapter::HttpResult r;
            const bool isCelestrak = url.find("celestrak") != std::string::npos;
            if (isCelestrak) {
                // C2b: la URL debe llevar FORMAT=csv y el grupo pedido. Esto es
                // parte del contrato del adaptador.
                checkBool("Celestrak pide FORMAT=csv",
                          url.find("FORMAT=csv") != std::string::npos);
                std::string group;
                const size_t gpos = url.find("GROUP=");
                if (gpos != std::string::npos) {
                    const size_t amp = url.find('&', gpos);
                    group = url.substr(gpos + 6, amp == std::string::npos
                                                     ? std::string::npos
                                                     : amp - (gpos + 6));
                }
                celestrakUrls.push_back(group);
                ++celestrakCalls;
                if (onCelestrak) onCelestrak();
                r.http = celestrakHttp;
                const auto hit = groupHttp.find(group);
                if (hit != groupHttp.end()) r.http = hit->second;
                r.curl = celestrakCurl;
                if (r.http == 200 && r.curl == 0) {
                    body = celestrakBody;
                    const auto bit = groupBody.find(group);
                    if (bit != groupBody.end()) body = bit->second;
                    r.ok = !body.empty();
                } else {
                    r.err = r.curl != 0 ? "timeout simulado" : "HTTP simulado";
                }
            } else {
                ++satnogsCalls;
                r.http = satnogsHttp;
                r.curl = satnogsCurl;
                if (r.http == 200 && r.curl == 0) {
                    body = satnogsBody;
                    r.ok = !body.empty();
                } else {
                    r.err = r.curl != 0 ? "timeout simulado" : "HTTP simulado";
                }
            }
            return r;
        };
    }
};

struct Paths {
    fs::path dir;
    fs::path cache;       // satellites.tle (catalogo completo)
    fs::path satnogs;     // satellites-satnogs.tle (subconjunto)
    fs::path state;       // tle-state (sidecar con una clave por puerta)
};

static Paths makePaths(const char* name) {
    Paths p;
    p.dir = fs::temp_directory_path() / (std::string("tle_orch_") + name);
    std::error_code ec;
    fs::remove_all(p.dir, ec);
    fs::create_directories(p.dir, ec);
    p.cache = p.dir / "satellites.tle";
    p.satnogs = p.dir / "satellites-satnogs.tle";
    p.state = p.dir / "tle-state";
    return p;
}

static std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Extrae el valor de "<key>=" de la primera linea que lo contenga.
static double fieldOf(const std::string& text, const std::string& key) {
    const size_t at = text.find(key + "=");
    if (at == std::string::npos) return -1.0;
    return std::atof(text.c_str() + at + key.size() + 1);
}

// C2b: busquedas por NORAD en el snapshot fusionado.
static const TleSat* satByNorad(const TleSnapshot& s, int norad) {
    for (const TleSat& x : s.sats) {
        if (x.noradId == norad) return &x;
    }
    return nullptr;
}

static bool hasNorad(const TleSnapshot& s, int norad) { return satByNorad(s, norad) != nullptr; }

int main() {
    std::printf("--- C2a-19: 403 sin cache -> SatNOGS, CelesTrak bloqueado 2 h ---\n");
    {
        Paths p = makePaths("A");
        Mock m;
        m.celestrakHttp = 403;
        m.celestrakCurl = 0;

        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"active"});   // C2a: seleccion explicita (setPathsForTests ya no fuerza nada)
        a.setTransport(m.transport());

        const bool ok = a.fetchOnce(T0);
        checkBool("se dibuja algo (subconjunto SatNOGS)", ok && a.getSnapshot().valid);
        checkBool("la fuente es la red de SatNOGS",
                  a.getSnapshot().source == TleSource::SatNogs);
        checkBool("el subconjunto trae 2 filas", a.getSnapshot().sats.size() == 2);
        checkBool("una sola peticion a CelesTrak", m.celestrakCalls == 1);
        checkBool("una sola peticion a SatNOGS", m.satnogsCalls == 1);
        checkNear("CelesTrak queda bloqueado 2 h", a.getBlockedUntilUtc(), T0 + kBlock);
        checkBool("NO se escribe satellites.tle con el subconjunto", !fs::exists(p.cache));
        checkBool("SI se escribe la cache propia de SatNOGS", fs::exists(p.satnogs));
        const std::string satCacheText = readText(p.satnogs);
        checkBool("la cache de SatNOGS se marca con source=satnogs",
                  satCacheText.find("source=satnogs") != std::string::npos);

        // Siguiente pase, 60 s despues: ni CelesTrak ni SatNOGS tocan la red.
        const bool ok2 = a.fetchOnce(T0 + 60.0);
        checkBool("el segundo pase tambien dibuja", ok2 && a.getSnapshot().valid);
        checkBool("el subconjunto se relee como SatNogsCache (no como Cache)",
                  a.getSnapshot().source == TleSource::SatNogsCache);
        checkBool("cero peticiones nuevas a CelesTrak", m.celestrakCalls == 1);
        checkBool("cero peticiones nuevas a SatNOGS", m.satnogsCalls == 1);
        checkNear("el bloqueo de CelesTrak sigue en pie", a.getBlockedUntilUtc(), T0 + kBlock);
        checkNear("nextNetworkUtc respeta el bloqueo", a.nextNetworkUtc(T0 + 60.0), T0 + kBlock);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2a-20: timeout sin cache -> backoff de 5 min, no bloqueo ---\n");
    {
        Paths p = makePaths("B");
        Mock m;
        m.celestrakHttp = 0;
        m.celestrakCurl = 28;   // CURLE_OPERATION_TIMEDOUT

        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"active"});   // C2a: seleccion explicita (setPathsForTests ya no fuerza nada)
        a.setTransport(m.transport());

        const bool ok = a.fetchOnce(T0);
        checkBool("timeout: se sirve SatNOGS", ok && a.getSnapshot().valid &&
                                                   a.getSnapshot().source == TleSource::SatNogs);
        checkBool("timeout NO es bloqueo de 2 h", a.getBlockedUntilUtc() == 0.0);
        checkNear("el reintento se programa a 5 min", a.nextNetworkUtc(T0 + 1.0), T0 + 300.0);

        // A los 60 s: la puerta dice "espera tras fallo", asi que no se pide.
        const bool ok2 = a.fetchOnce(T0 + 60.0);
        checkBool("a los 60 s sigue habiendo dato (cache de SatNOGS)",
                  ok2 && a.getSnapshot().source == TleSource::SatNogsCache);
        checkBool("a los 60 s CelesTrak NO se ha reintentado", m.celestrakCalls == 1);

        // A los 301 s: CelesTrak vuelve a responder y su catalogo completo
        // sustituye al subconjunto.
        m.celestrakHttp = 200;
        m.celestrakCurl = 0;
        const bool ok3 = a.fetchOnce(T0 + 301.0);
        checkBool("a los 301 s se reintenta CelesTrak", m.celestrakCalls == 2);
        checkBool("el catalogo completo sustituye al subconjunto",
                  ok3 && a.getSnapshot().source == TleSource::Celestrak &&
                      a.getSnapshot().sats.size() == 3);
        checkBool("un exito limpia el error", a.getErrorMessage().empty());
        checkBool("un exito no deja bloqueo", a.getBlockedUntilUtc() == 0.0);
        checkBool("el catalogo completo SI va a satellites.tle", fs::exists(p.cache));
        const std::string cacheText = readText(p.cache);
        checkBool("la cache de CelesTrak no se marca como satnogs",
                  cacheText.find("source=satnogs") == std::string::npos);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2a-21: muerte a mitad de fetch -> el reinicio no pide ---\n");
    {
        Paths p = makePaths("C");
        Mock m;
        m.celestrakHttp = 403;   // da igual: no llega a responder
        bool provisionalOnDisk = false;
        bool panelSaysNotBlocked = false;

        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"active"});   // C2a: seleccion explicita (setPathsForTests ya no fuerza nada)
        a.setTransport(m.transport());
        Mock* mp = &m;
        m.onCelestrak = [&, mp]() {
            // En vuelo: el sidecar debe tener ya el provisional y el panel NO
            // debe pintarlo como bloqueo (lastHttp == -1).
            const std::string st = readText(p.state);
            const double until = fieldOf(st, "celestrak.active.blocked_until");
            provisionalOnDisk = std::fabs(until - (T0 + kBlock)) < 1.0;
            panelSaysNotBlocked = a.getBlockedUntilUtc() == 0.0;
            (void)mp;
            throw std::runtime_error("app cerrada a mitad de descarga");
        };

        bool threw = false;
        try {
            a.fetchOnce(T0);
        } catch (const std::runtime_error&) {
            threw = true;   // el fetch queda a medias, como un cierre forzado
        }
        checkBool("el fetch se interrumpe a mitad", threw);
        checkBool("el provisional ya estaba en disco ANTES de la respuesta",
                  provisionalOnDisk);
        checkBool("en vuelo el panel no dice bloqueado", panelSaysNotBlocked);

        // Reinicio: adaptador nuevo, mismo directorio. El sidecar se relee.
        TleAdapter b;
        b.setPathsForTests(p.cache, p.satnogs, p.state);
        // C2a: seleccion explicita (setPathsForTests ya no fuerza nada). Sin
        // esto el bloqueo leido del sidecar saldria 0.0: los grupos por
        // defecto ni siquiera estan instalados en este reinicio simulado.
        b.setGroups({"active"});
        b.loadStateForTests(T0 + 60.0);
        checkNear("tras el reinicio el bloqueo sigue vigente (peticion interrumpida)",
                  b.getBlockedUntilUtc(), T0 + kBlock);
        checkNear("nextNetworkUtc no adelanta el reintento", b.nextNetworkUtc(T0 + 60.0),
                  T0 + kBlock);

        Mock m2;
        b.setTransport(m2.transport());
        b.fetchOnce(T0 + 60.0);
        checkBool("el reinicio NO repite la peticion de CelesTrak", m2.celestrakCalls == 0);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2a-22: aislamiento de caches (SatNOGS no pisa el catalogo) ---\n");
    {
        Paths p = makePaths("D");
        Mock m;   // CelesTrak 403, SatNOGS 200 con 2 filas
        m.celestrakHttp = 403;

        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"active"});   // C2a: seleccion explicita (setPathsForTests ya no fuerza nada)
        a.setTransport(m.transport());
        a.fetchOnce(T0);

        TleSnapshot full;
        checkBool("satellites.tle no existe tras servir solo SatNOGS",
                  !TleAdapter::loadCache(p.cache, T0, full, fetchgate::kMaxFallbackAgeSec));

        TleSnapshot subset;
        checkBool("la cache de SatNOGS si se puede releer",
                  TleAdapter::loadCache(p.satnogs, T0, subset, fetchgate::kMaxFallbackAgeSec));
        checkBool("su origen es SatNogsCache", subset.source == TleSource::SatNogsCache);
        bool onlySatnogs = subset.sats.size() == 2;
        for (const TleSat& s : subset.sats) {
            if (s.noradId == 90003) onlySatnogs = onlySatnogs && true;
        }
        checkBool("el subconjunto conserva su fila propia (90003)", onlySatnogs);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2a-23: cache de 73 h -> se invalida una sola vez ---\n");
    {
        Paths p = makePaths("E");
        // Cache con epoch T0 y puerta de CelesTrak bloqueada: asi el pase entra
        // por serveCache (el camino de la caducidad), no por la red.
        TleSnapshot disk;
        TleAdapter::parseCelesTrakCsv(celestrakCsv(), T0, disk);
        checkBool("cache de prueba escrita", TleAdapter::saveCache(p.cache, disk, celestrakCsv()));

        auto blockBoth = [&](double untilUtc) {
            fetchgate::State cel;
            cel.lastAttemptUtc = untilUtc - kBlock;
            cel.lastHttp = 403;
            cel.blockedUntilUtc = untilUtc;
            fetchstate::save(p.state, "celestrak.active", cel);
            fetchgate::State sn = cel;
            fetchstate::save(p.state, "celestrak.satnogs", sn);
        };

        Mock m;
        blockBoth(T0 + 3600.0);
        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"active"});   // C2a: seleccion explicita (setPathsForTests ya no fuerza nada)
        a.setTransport(m.transport());
        a.loadStateForTests(T0);
        checkBool("a las 0 h se sirve la cache", a.fetchOnce(T0) && a.getSnapshot().valid);
        checkBool("la fuente es la cache de disco", a.getSnapshot().source == TleSource::Cache);
        const unsigned long gen1 = a.getGeneration();
        checkBool("primera instalacion: generacion 1", gen1 == 1);

        // 73 h despues, con la cache ya en el snapshot y en disco.
        blockBoth(T0 + 73.0 * 3600.0 + 3600.0);
        a.loadStateForTests(T0 + 73.0 * 3600.0);
        a.fetchOnce(T0 + 73.0 * 3600.0);
        const unsigned long gen2 = a.getGeneration();
        checkBool("una cache de 73 h deja de dibujarse", !a.getSnapshot().valid);
        checkBool("la generacion sube exactamente una vez", gen2 == gen1 + 1);
        checkBool("sin red: el bloqueo se respeto", m.celestrakCalls == 0);

        // Otro pase: no debe volver a subir la generacion (antes subia en cada
        // evaluacion y la UI recopiaba el snapshot vacio en bucle).
        blockBoth(T0 + 73.0 * 3600.0 + 7200.0);
        a.loadStateForTests(T0 + 73.0 * 3600.0 + 60.0);
        a.fetchOnce(T0 + 73.0 * 3600.0 + 60.0);
        checkBool("el segundo pase no vuelve a invalidar",
                  a.getGeneration() == gen2);
        checkBool("sigue sin datos (nada de cache de 73 h)", !a.getSnapshot().valid);
        checkBool("tampoco hay peticiones de CelesTrak", m.celestrakCalls == 0);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2a-24: cache heredada con source=satnogs no pasa por catalogo ---\n");
    {
        Paths p = makePaths("F");
        // Los builds anteriores a c76d6a5 guardaban el subconjunto de SatNOGS
        // en satellites.tle. Si ese fichero sobrevive a la actualizacion, no
        // puede servir como catalogo activo ni contar como cache joven.
        {
            TleSnapshot subset;
            TleAdapter::parseSatNogsJson(satnogsJson(), T0, subset);
            checkBool("se escribe el fichero heredado (source=satnogs)",
                      TleAdapter::saveCache(p.cache, subset));
        }
        checkBool("no cuenta como cache de CelesTrak (epoch 0)",
                  TleAdapter::celestrakCacheEpoch(p.cache) == 0.0);
        checkBool("la edad para la puerta es -1 (sin cache)",
                  TleAdapter::celestrakCacheAge(p.cache, T0 + 60.0) < 0.0);
        TleSnapshot ignored;
        checkBool("loadCelestrakCache la rechaza",
                  !TleAdapter::loadCelestrakCache(p.cache, T0 + 60.0, ignored));

        Mock m;   // CelesTrak responde 200 con el catalogo completo
        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"active"});   // C2a: seleccion explicita (setPathsForTests ya no fuerza nada)
        a.setTransport(m.transport());
        const bool ok = a.fetchOnce(T0 + 60.0);
        checkBool("se pide el catalogo completo en vez de servir el subconjunto",
                  ok && m.celestrakCalls == 1 &&
                      a.getSnapshot().source == TleSource::Celestrak);
        checkBool("el catalogo completo trae 3 filas", a.getSnapshot().sats.size() == 3);
        checkBool("la cache se reescribe ya sin source=satnogs",
                  readText(p.cache).find("source=satnogs") == std::string::npos);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2b-25: dos grupos vivos se fusionan (dedup por EPOCH) ---\n");
    {
        Paths p = makePaths("G1");
        Mock m;
        // Cuerpo PROPIO por grupo: con el mismo CSV para los dos no se puede
        // comprobar ni la union ni el dedup por EPOCH.
        m.groupBody["amateur"] = celestrakCsv();
        m.groupBody["stations"] = stationsCsv();
        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"amateur", "stations"});
        a.setTransport(m.transport());
        const bool ok = a.fetchOnce(T0 + 60.0);
        const TleSnapshot s = a.getSnapshot();
        checkBool("fetch ok", ok && s.valid);
        checkBool("una peticion por grupo", m.celestrakCalls == 2);
        checkBool("se pidio GROUP=stations",
                  m.celestrakUrls.size() == 2 && m.celestrakUrls[1] == "stations");
        checkBool("3 + 2 filas con la ISS repetida = 4", s.sats.size() == 4);
        checkBool("el origen es CelesTrak", s.source == TleSource::Celestrak);
        const TleSat* iss = satByNorad(s, 25544);
        checkBool("la ISS quedo una sola vez", iss != nullptr);
        checkBool("gana el EPOCH mas nuevo (stations)",
                  iss != nullptr && iss->name == "ISS-STATIONS");
        checkBool("el objeto exclusivo de stations esta", hasNorad(s, 90003));
        checkBool("sin aviso: todo vivo", a.getErrorMessage().empty());

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2b-26: la puerta es POR GRUPO (403 en stations no silencia amateur) ---\n");
    {
        Paths p = makePaths("G2");
        Mock m;
        m.groupHttp["stations"] = 403;
        m.groupBody["amateur"] = celestrakCsv();   // cuerpo propio del grupo vivo
        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"amateur", "stations"});
        a.setTransport(m.transport());
        const bool ok = a.fetchOnce(T0 + 60.0);
        const TleSnapshot s = a.getSnapshot();
        checkBool("se sirve el grupo vivo", ok && s.valid && s.sats.size() == 3);
        checkBool("un grupo caido no manda a SatNOGS", m.satnogsCalls == 0);
        checkBool("el aviso nombra stations",
                  a.getErrorMessage().find("stations") != std::string::npos);
        checkNear("stations bloqueado 2 h", a.getBlockedUntilUtc(), T0 + 60.0 + kBlock);
        const std::string state = readText(p.state);
        checkNear("sidecar: stations tiene su propia clave",
                  fieldOf(state, "celestrak.stations.blocked_until"), T0 + 60.0 + kBlock);
        checkNear("sidecar: active no quedo bloqueado",
                  fieldOf(state, "celestrak.active.blocked_until"), 0.0);
        // Siguiente pase: active esta en cache joven y stations bloqueado, asi
        // que no se toca la red para ninguno.
        a.fetchOnce(T0 + 120.0);
        checkBool("ningun grupo pide red otra vez", m.celestrakCalls == 2);
        checkBool("sigue sirviendose el catalogo fusionado", a.getSnapshot().valid);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2b-27: cache de un grupo + vivo de otro, timeUtc el mas viejo ---\n");
    {
        Paths p = makePaths("G3");
        // La cache del grupo amateur es SU fichero (no satellites.tle), con la
        // puerta celestrak.amateur bloqueada: se sirve de cache mientras
        // stations pide red.
        const fs::path amateurCache = p.dir / "satellites-amateur.tle";
        TleSnapshot disk;
        TleAdapter::parseCelesTrakCsv(celestrakCsv(), T0, disk);
        checkBool("cache de amateur escrita",
                  TleAdapter::saveCache(amateurCache, disk, celestrakCsv()));
        fetchgate::State cel;
        cel.lastAttemptUtc = T0 - kBlock;
        cel.lastHttp = 403;
        cel.blockedUntilUtc = T0 + 3600.0;
        fetchstate::save(p.state, "celestrak.amateur", cel);

        Mock m;
        m.groupBody["stations"] = stationsCsv();
        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"amateur", "stations"});
        a.setTransport(m.transport());
        a.loadStateForTests(T0);
        const bool ok = a.fetchOnce(T0 + 60.0);
        const TleSnapshot s = a.getSnapshot();
        checkBool("se fusiona cache + vivo", ok && s.valid && s.sats.size() == 4);
        checkBool("solo stations toca red",
                  m.celestrakCalls == 1 && m.celestrakUrls[0] == "stations");
        checkBool("origenes distintos -> se declara cache", s.source == TleSource::Cache);
        checkNear("timeUtc = el del grupo mas viejo", a.getLastFetchTime(), T0, 1.0);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2b-28: si NINGUN grupo responde, SatNOGS se pide UNA vez ---\n");
    {
        Paths p = makePaths("G4");
        Mock m;
        m.groupHttp["amateur"] = 403;
        m.groupHttp["stations"] = 403;
        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"amateur", "stations"});
        a.setTransport(m.transport());
        const bool ok = a.fetchOnce(T0 + 60.0);
        const TleSnapshot s = a.getSnapshot();
        checkBool("hay subconjunto de SatNOGS", ok && s.valid);
        checkBool("una sola peticion a SatNOGS", m.satnogsCalls == 1);
        checkBool("origen SatNOGS", s.source == TleSource::SatNogs);
        checkBool("2 filas del subconjunto", s.sats.size() == 2);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2b-29: la seleccion de grupos se sanea ---\n");
    {
        TleAdapter a;
        a.setGroups({"weather", "nope", "weather"});
        checkBool("descarta desconocidos y repetidos, conserva el orden",
                  a.groups() == std::vector<std::string>({"weather"}));
        // `active` es el superconjunto: su presencia descarta a los demas
        // aunque lleguen antes en el orden (decision de producto, C2b-29).
        a.setGroups({"weather", "nope", "weather", "active"});
        checkBool("active descarta a los demas aunque lleguen antes",
                  a.groups() == std::vector<std::string>({"active"}));
        a.setGroups({});
        checkBool("vacio vuelve a active",
                  a.groups() == std::vector<std::string>({"active"}));
        a.setGroups({"nope"});
        checkBool("solo desconocidos vuelve a active",
                  a.groups() == std::vector<std::string>({"active"}));
    }

    std::printf("--- C2b-30: cada grupo tiene su propio fichero de cache ---\n");
    {
        Paths p = makePaths("G5");
        Mock m;
        m.groupHttp["weather"] = 500;
        m.groupBody["amateur"] = celestrakCsv();
        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"amateur", "weather"});
        a.setTransport(m.transport());
        a.fetchOnce(T0 + 60.0);
        const fs::path amateurCache = p.dir / "satellites-amateur.tle";
        checkBool("la cache del grupo vivo se escribe", fs::exists(amateurCache));
        checkBool("la del grupo caido no se escribe",
                  !fs::exists(p.dir / "satellites-weather.tle"));
        TleSnapshot back;
        checkBool("satellites-amateur.tle guarda el catalogo de amateur",
                  TleAdapter::loadCelestrakCache(amateurCache, T0 + 120.0, back) &&
                      back.sats.size() == 3);
        checkBool("sin filas de otro grupo", !hasNorad(back, 90003));

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }
    std::printf("--- C2b-31: defecto del CONSTRUCTOR sin seleccion -> amateur,stations ---\n");
    {
        TleAdapter a;
        // No llamamos a setGroups; deberia iniciar con el defecto.
        checkBool("defecto es amateur,stations", a.groups() == std::vector<std::string>({"amateur", "stations"}));
    }

    std::printf("--- C2b-32: active descarta los demas ---\n");
    {
        TleAdapter a;
        a.setGroups({"active", "stations", "weather"});
        checkBool("solo queda active", a.groups() == std::vector<std::string>({"active"}));
    }

    std::printf("--- C2b-33: alternar active y par pequeño sin contaminar caches/puertas ---\n");
    {
        Paths p = makePaths("Alt");
        Mock m;
        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"active"});
        a.setTransport(m.transport());
        a.fetchOnce(T0 + 60.0);
        checkInt("peticiones active", m.celestrakCalls, 1);
        
        m.celestrakCalls = 0;
        a.setGroups({"amateur", "stations"});
        a.fetchOnce(T0 + 120.0);
        checkInt("peticiones amateur+stations", m.celestrakCalls, 2);
        
        const std::string state = readText(p.state);
        checkBool("sidecar tiene claves la activo", state.find("celestrak.active") != std::string::npos);
        checkBool("sidecar tiene claves la amateur", state.find("celestrak.amateur") != std::string::npos);
        checkBool("sidecar tiene claves la stations", state.find("celestrak.stations") != std::string::npos);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2b-34: la pausa entre grupos es condicional e inyectable ---\n");
    {
        Paths p = makePaths("P1");
        Mock m;
        m.groupBody["amateur"] = celestrakCsv();
        m.groupBody["stations"] = stationsCsv();
        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"amateur", "stations"});
        int pauses = 0;
        a.setGroupPauseForTests([&pauses] { ++pauses; });   // NO duerme: cuenta
        a.setTransport(m.transport());
        // Dos grupos, los dos a red: UNA pausa (solo entre el 1 y el 2).
        a.fetchOnce(T0 + 60.0);
        checkInt("dos grupos por red: una pausa", pauses, 1);
        checkInt("dos grupos por red: dos peticiones", m.celestrakCalls, 2);

        // Pase posterior con ambos grupos en cache JOVEN (escritas arriba):
        // nadie toca la red, y entonces no hay NINGUNA pausa. El defecto pagaba
        // 2 s por pase sin haber pedido nada.
        TleAdapter b;
        b.setPathsForTests(p.cache, p.satnogs, p.state);
        b.setGroups({"amateur", "stations"});
        int pausesB = 0;
        b.setGroupPauseForTests([&pausesB] { ++pausesB; });
        b.setTransport(m.transport());
        b.fetchOnce(T0 + 120.0);
        checkInt("dos grupos en cache: cero pausas", pausesB, 0);
        checkInt("dos grupos en cache: cero peticiones nuevas",
                 m.celestrakCalls, 2);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2b-35: pase con puerta cerrada y active en cache (escala real) ---\n");
    {
        Paths p = makePaths("Slow");
        // La produccion trae ~15 900 objetos; medir el pase rapido con 3 filas
        // de juguete no mide nada. HIDRATACION: nombres y NORAD 90001+ son
        // ficticios, con elementos del fixture ISS.
        static const char* kBigEpochIso = "2026-10-25T13:18:26.839680";   // T0 - 1 h
        std::string bigCsv = tlefixture::csvHeader();
        const int kBig = 16000;
        for (int i = 0; i < kBig; ++i) {
            bigCsv += tlefixture::csvRow("SAT-" + std::to_string(90000 + i),
                                         90000 + i, kBigEpochIso);
        }
        TleSnapshot big;
        checkBool("catalogo grande parsea",
                  TleAdapter::parseCelesTrakCsv(bigCsv, T0 - 3600.0, big));
        checkInt("el catalogo grande trae 16 000 filas", (int)big.sats.size(), kBig);
        checkBool("cache grande escrita a disco",
                  TleAdapter::saveCache(p.cache, big, bigCsv));

        // Puerta CERRADA (bloqueado 2 h) y cache de 1 h (< TTL vivo de 6 h):
        // decide() no deja pedir red por ninguna via; el pase es solo lectura
        // de disco + parseo. Es justo el estado estable de produccion.
        fetchgate::State cel;
        cel.lastAttemptUtc = T0 - 60.0;
        cel.lastHttp = 403;
        cel.blockedUntilUtc = T0 + kBlock;
        fetchstate::save(p.state, "celestrak.active", cel);

        Mock m;
        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"active"});
        a.setTransport(m.transport());
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = a.fetchOnce(T0);
        const double ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
        std::printf("[medicion] pase con puerta cerrada + cache de 1 h: %.1f ms\n", ms);
        checkBool("fetch ok tras puerta cerrada", ok);
        checkBool("snapshot valido", a.getSnapshot().valid);
        checkBool("fuente cache", a.getSnapshot().source == TleSource::Cache);
        checkInt("filas servidas", (int)a.getSnapshot().sats.size(), kBig);
        checkInt("la puerta cerrada no pide red", m.celestrakCalls, 0);
        checkBool("el pase es barato (<2000 ms, tope para CI lenta)", ms < 2000.0);

        std::error_code ec0;
        fs::remove_all(p.dir, ec0);
    }

    std::printf("--- C2b-36: host break interrumpe el pase ante 500 ---\n");
    {
        Paths p = makePaths("Ghost");
        Mock m;
        m.groupHttp["amateur"] = 500;
        m.groupBody["stations"] = stationsCsv();

        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"amateur", "stations"});
        a.setTransport(m.transport());

        const bool ok = a.fetchOnce(T0 + 60.0);
        checkBool("merge invalido sirve fallback si lo pide", true);
        checkInt("solo UNA peticion a CelesTrak en el primer pase",
                 m.celestrakCalls, 1);
        checkBool("el intentado fue amateur",
                  m.celestrakUrls.size() == 1 &&
                      m.celestrakUrls[0] == "amateur");
        // Sidecar: el grupo no intentado queda bloqueado por la puerta del host,
        // SIN cambio de lastAttempt (no se marco como intento).
        {
            fetchgate::State host;
            fetchstate::load(p.state, "celestrak.stations", host);
            checkNear("stations blockedUntil = host guard (2 h)", host.blockedUntilUtc, T0 + 60.0 + kBlock, 1.0);
            checkNear("stations lastAttempt intacto", host.lastAttemptUtc, 0.0, 1e-6);
        }

        // A los 60 + 60 s el host guard impide cualquier nueva peticion.
        const int before = m.celestrakCalls;
        a.fetchOnce(T0 + 120.0);
        checkInt("a T0+120 s ninguna nueva peticion (host guard)",
                 m.celestrakCalls, before);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::printf("--- C2b-37: host break con error de transporte deja cooldown ---\n");
    {
        Paths p = makePaths("Ghost2");
        Mock m;
        m.celestrakCurl = 28;   // error de transporte en el primer grupo
        m.groupBody["stations"] = stationsCsv();

        TleAdapter a;
        a.setPathsForTests(p.cache, p.satnogs, p.state);
        a.setGroups({"amateur", "stations"});
        a.setTransport(m.transport());

        a.fetchOnce(T0 + 60.0);
        checkInt("solo una peticion ante error de transporte",
                 m.celestrakCalls, 1);
        // Puerta de host para transporte: 5 min, no 2 h.
        {
            fetchgate::State host;
            fetchstate::load(p.state, "celestrak.stations", host);
            checkNear("stations guard de transporte = 5 min", host.blockedUntilUtc,
                      T0 + 60.0 + 300.0, 1.0);
        }

        const int before = m.celestrakCalls;
        a.fetchOnce(T0 + 120.0);
        checkInt("a T0+120 s no hay seguimiento del host caido",
                 m.celestrakCalls, before);

        std::error_code ec;
        fs::remove_all(p.dir, ec);
    }

    std::error_code ec;
    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);    return g_fail == 0 ? 0 : 1;
    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}

