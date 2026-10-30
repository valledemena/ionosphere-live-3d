#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Data/FetchGate.h"
#include "Data/OrbitProp.h"

// Catalogo de satelites activos. Cadena de fuentes, en orden:
//
// 1) CelesTrak (primaria, viva), grupo `active` por defecto:
//    https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=csv
//    Desde 2026-05-09 FORMAT vale CSV por defecto y el TLE clasico de 5
//    digitos se agoto el 2026-07-11, asi que FORMAT=csv se pide EXPLICITO y se
//    parsea por cabecera (GP CSV / OMM, RFC 4180). Verificado en vivo el
//    2026-10-27: 2 671 704 bytes en FORMAT=tle = 15 903 objetos; el CSV trae
//    las mismas filas con EPOCH ISO-8601 y admiten NORAD de 6 digitos.
//
// 2) Cache de disco de CelesTrak, "satellites.tle" en el directorio de datos
//    de usuario (C2a: absoluta, no relativa al CWD). Primera linea
//    "# tle-cache v2 epoch=<segundos UTC> count=<n> source=<origen> format=<csv|tle>"
//    y despues el CUERPO CRUDO de la respuesta (CSV tal cual se descargo). Se
//    sirve etiquetada como TleSource::Cache y su edad va en el panel. Admite
//    hasta kMaxFallbackAgeSec (72 h) cuando la fuente viva falla; por debajo de
//    kLiveTtlSec (6 h) NI SE PIDE RED. El fichero se escribe con tmp + rename
//    (nunca a medias). Los ficheros v1 de builds anteriores siguen leyendose
//    (su cuerpo era TLE de 3 lineas).
//
// 3) SatNOGS (ultimo recurso, vivo):
//    https://db.satnogs.org/api/tle/?format=json
//    Verificado en vivo el 2026-10-27: HTTP 200, "application/json",
//    537 295 bytes, 1 735 filas, SIN paginacion (Content-Length completo y
//    sin cabecera Link). Array de objetos con los campos tle0 ("0 NOMBRE"),
//    tle1, tle2, tle_source, sat_id, norad_cat_id, updated (ISO-8601).
//    SatNOGS solo publica TLE clasico, asi que aqui se sigue usando parseTle.
//    Solo cubre satelites con emisor registrado: es un subconjunto, no el
//    catalogo activo completo. Tiene su PROPIA cache
//    ("satellites-satnogs.tle") y su propia puerta en el sidecar: guardar el
//    subconjunto en la cache de CelesTrak lo haria pasar por catalogo completo
//    durante 6 h. Orden: cache CelesTrak (<=72 h) -> cache SatNOGS (<=72 h) ->
//    red SatNOGS.
//
// HONESTIDAD: solo elementos reales parseados. Si las tres fuentes fallan el
// snapshot queda invalido (valid == false) con el motivo en
// getErrorMessage() y NADA se dibuja. Nunca se inventan satelites ni
// posiciones. Las filas con checksum roto, campos ilegibles o EPOCH invalido
// se DESCARTAN y se cuentan en la nota (no se auto-corrigen). Techo de 20 000
// satelites.
//
// CIUDADANIA (C2a): la decision de tocar red la toma fetchgate::decide con el
// estado persistido en un sidecar (last_attempt / last_success /
// blocked_until). Cualquier respuesta distinta de 200 bloquea 2 h (429 respeta
// Retry-After); los errores de transporte usan backoff 5 -> 60 min. Sin
// FOLLOWLOCATION (los 3xx son aviso de URL obsoleta, no algo que ocultar).
// User-Agent propio (la URL de contacto entra con C1). Timeouts con tope
// amplio (120 s CelesTrak, 90 s SatNOGS) mas LOW_SPEED_LIMIT/TIME: si el
// enlace solo se arrastra, se espera en vez de abortar una descarga que el
// servidor ya conto (un reintento daria 403). La cache viva se reutiliza 6 h;
// un reinicio ya no pide de nuevo. Cada proveedor tiene su propia puerta en el
// sidecar (claves celestrak.active / celestrak.satnogs).
//
// GRUPOS (C2b): CelesTrak publica el catalogo por grupos (active, stations,
// weather, gps-ops, science). `setGroups` elige cuales se piden; cada grupo
// tiene su URL, su cache ("satellites-<grupo>.tle"; `active` sigue usando
// "satellites.tle") y SU PROPIA puerta en el sidecar (celestrak.<grupo>), asi
// que un 403 en `weather` no silencia `active`. `acquireGroup` consigue el
// snapshot de un grupo sin instalarlo; `fetch` fusiona los que respondan con
// mergeSnapshots (dedup por NORAD) e instala el resultado. El fallback SatNOGS
// es UNO solo, y solo si la fusion no dejo nada (vacio, no "incompleto": un
// grupo caido no descarta los datos reales de los demas).
//
// La posicion NO se calcula aqui: este modulo solo entrega elementos medios
// (orbitprop::OrbitElements). Ver OrbitProp.h para el modelo y sus limites.

enum class TleSource {
    None,
    Celestrak,
    SatNogs,       // red de SatNOGS
    SatNogsCache,  // cache propia de SatNOGS en disco (subconjunto, no el activo)
    Cache          // cache de CelesTrak en disco
};

struct TleSat {
    std::string name;
    int noradId = 0;
    // Lineas crudas del TLE (69 columnas, ya sin CR) cuando la fuente es TLE
    // (SatNOGS o ficheros v1). En el camino CSV quedan VACIAS: los elementos ya
    // vienen resueltos en `el` y la cache guarda el CSV crudo, no un TLE
    // reconstruido. No sirven para auditar el registro CSV; para eso esta el
    // propio fichero de cache.
    std::string line1;
    std::string line2;
    orbitprop::OrbitElements el;
};

struct TleSnapshot {
    std::vector<TleSat> sats;
    double timeUtc = 0.0;     // instante de referencia del fetch (o del cache)
    bool valid = false;       // true solo si hay >= 1 TLE real
    TleSource source = TleSource::None;
    std::string note;         // "15903 sats" / "sin datos" / motivo
};

class TleAdapter {
public:
    // Resultado de una peticion HTTP. Publico para que los tests puedan
    // inyectar un transporte sin curl (ver setTransport).
    struct HttpResult {
        bool ok = false;          // 200 con cuerpo no vacio
        long http = 0;
        int curl = 0;
        double retryAfter = 0.0;
        std::string err;
    };
    // Transporte inyectable: (url, body de salida) -> resultado. En produccion
    // es curl; en tests, una funcion que devuelve respuestas preparadas.
    using Transport = std::function<HttpResult(const std::string& url, std::string& body)>;

    void init();
    // Descarga + parsea en hilo detached. No-op si ya hay una en curso.
    // Respeta la puerta de red: con cache joven no toca la red.
    void fetchAsync(double utc);
    bool isFetching() const { return fetching_; }

    // Sincrono, mismo camino que fetchAsync pero en el hilo llamante. Es la
    // entrada que usan los tests de orquestacion; no lo usa la app.
    bool fetchOnce(double utc) { return fetch(utc); }

    // --- Grupos de CelesTrak (C2b) ---
    // Claves que se pueden pedir. `active` es el catalogo completo y el unico
    // que existia antes de C2b.
    static const std::vector<std::string>& knownGroups();
    // Fija los grupos a pedir. Se descartan claves desconocidas y repetidas; si
    // no queda ninguna, se vuelve a {"active"}. Seguro entre hilos.
    void setGroups(const std::vector<std::string>& keys);
    std::vector<std::string> groups() const;

    // --- Costuras de prueba (no tocar desde la app) ---
    // Sustituye el transporte HTTP. Vaciar el function vuelve a curl.
    void setTransport(Transport t) { transport_ = std::move(t); }
    // Fija cache, cache de SatNOGS y sidecar, y limpia el estado en memoria.
    void setPathsForTests(const std::filesystem::path& cache,
                          const std::filesystem::path& satnogsCache,
                          const std::filesystem::path& state);
    // Relee el sidecar con un reloj explicito (simula un reinicio sin init()).
    void loadStateForTests(double nowUtc);
    // C2b: pausa entre peticiones de red a grupos distintos. Por defecto son
    // 2 s con std::this_thread::sleep_for; en pruebas se inyecta (y se cuenta)
    // para que la suite no pague 2 s reales por escenario multigrupo.
    void setGroupPauseForTests(std::function<void()> fn) {
        std::lock_guard<std::mutex> lk(mutex_);
        groupPause_ = fn;
    }

    // COPIA del snapshot bajo mutex. Son ~16k filas: llamarlo solo cuando
    // cambie getGeneration() (p.ej. una vez cada 6 h), no cada frame.
    TleSnapshot getSnapshot() const;

    TleSource source() const;               // fuente del ultimo snapshot
    std::string getSourceLabel() const;     // "celestrak"/"satnogs"/"cache"/"sin datos"
    // Copia bajo mutex: el texto lo escribe el hilo de fetch, asi que devolver
    // una referencia seria una carrera al leerla desde el hilo principal.
    std::string getErrorMessage() const;
    // Motivo informativo (no es un error): "red: cache viva (<6 h)", etc.
    // Va aparte para que la UI no pinte un estado sano como averia.
    std::string getStatusNote() const;

    // Epoch del snapshot instalado (cambia cuando cambia el catalogo, venga de
    // red o de cache). Es lo que usa el hilo de UI para saber si recopiarlo.
    double getLastFetchTime() const;

    // Estado de la puerta (diagnostico y panel de proveedores).
    double getLastAttemptUtc() const;
    double getBlockedUntilUtc() const;

    // Cuando toca volver a pedir, segun el estado actual y la cache. Lo usa el
    // scheduler de App para no despertar al adaptador hasta que sirva de algo.
    // Con varios grupos es el MINIMO de todos: el primero que pueda pedir.
    double nextNetworkUtc(double nowUtc);

    // Sube en cada snapshot valido instalado (cambio de catalogo).
    unsigned long getGeneration() const;
    int satCount() const;

    // Puros (sin red): parsean y dejan el snapshot listo. Devuelven true si
    // hay al menos un TLE valido. `out.source` lo fija el parser.
    static bool parseTleCatalog(const std::string& text, double nowUtc, TleSnapshot& out);
    // GP CSV de CelesTrak (OMM). Se mapea POR CABECERA, no por posicion: si
    // manana anaden una columna, esto sigue funcionando. RFC 4180: comillas,
    // comas y "" dentro de comillas. Admite NORAD de 6 digitos, que el TLE
    // clasico ya no puede representar.
    static bool parseCelesTrakCsv(const std::string& text, double nowUtc, TleSnapshot& out);
    static bool parseSatNogsJson(const std::string& json, double nowUtc, TleSnapshot& out);

    // Puro: fusiona los snapshots de varios grupos de CelesTrak en uno solo.
    //  - Dedup por NORAD: gana el EPOCH mas nuevo, sin importar el orden de
    //    los snapshots ni el nombre del grupo. Si empatan, se queda el primero.
    //  - Las filas sin NORAD (> 0) no se pueden deduplicar: se anaden tal cual.
    //  - timeUtc es el del grupo MAS VIEJO: la edad del catalogo fusionado no
    //    puede aparentar ser mejor que la de su peor componente.
    //  - source es el origen comun; si los grupos no coinciden, se declara
    //    Cache (hay una parte que no es viva). El detalle va en note.
    //  - valid solo si queda >= 1 satelite. Sin snapshots utiles, note dice
    //    "sin datos" y timeUtc = 0.
    // Orden de salida: por primera aparicion. Techo kMaxSats.
    static TleSnapshot mergeSnapshots(const std::vector<TleSnapshot>& parts);

    // Cache de disco (tambien puro: solo ficheros). loadCache valida la edad
    // contra maxAgeSec (por defecto el TTL vivo de 6 h). `path` es fs::path
    // para que un perfil con caracteres no ASCII no rompa el tmp+rename.
    // Acepta v2 (format=csv|tle) y los v1 de builds anteriores (cuerpo TLE).
    static bool loadCache(const std::filesystem::path& path, double nowUtc, TleSnapshot& out,
                          double maxAgeSec = fetchgate::kLiveTtlSec);
    // `rawBody` es el cuerpo crudo de la respuesta (el CSV de CelesTrak). Si se
    // pasa, se guarda tal cual con format=csv: asi la cache no depende de que
    // sepamos reconstruir un TLE de 69 columnas (imposible con NORAD de 6
    // digitos). Sin `rawBody` se escribe format=tle a partir de line1/line2
    // (camino de SatNOGS y de los tests); si falta alguna linea, no se guarda.
    static bool saveCache(const std::filesystem::path& path, const TleSnapshot& snap,
                          const std::string& rawBody = std::string());
    // Lee solo la cabecera de la cache (epoch). 0 si no hay o no se entiende.
    static double cacheEpoch(const std::filesystem::path& path);

    // Variantes para la cache de CelesTrak (satellites.tle). Rechazan una
    // cabecera source=satnogs: ese fichero lo escribieron los builds anteriores
    // a c76d6a5 con el subconjunto de SatNOGS y no puede pasar por catalogo
    // activo. Con epoch 0 la puerta lo ve como "sin cache" y pide el completo.
    static double celestrakCacheEpoch(const std::filesystem::path& path);
    static double celestrakCacheAge(const std::filesystem::path& path, double utc);
    static bool loadCelestrakCache(const std::filesystem::path& path, double nowUtc,
                                   TleSnapshot& out, double maxAgeSec = fetchgate::kLiveTtlSec);

    static const char* sourceLabel(TleSource s);

private:
    // Un grupo de CelesTrak con su puerta, URL y fichero de cache propios.
    struct GroupSpec {
        std::string key;                    // clave del sidecar ("celestrak.<key>")
        std::string url;                    // gp.php?GROUP=...
        std::filesystem::path cache;        // fichero de cache del grupo
    };
    GroupSpec groupSpec(const std::string& key) const;

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata);
    HttpResult httpGet(const char* url, long timeoutSec, std::string& body);
    bool fetch(double utc);
    // Consigue el snapshot de un grupo (red, o cache de hasta 72 h si la
    // puerta no deja tocar red) SIN instalarlo: fetch() fusiona antes de
    // publicar. Actualiza la puerta del grupo y la persiste.
    struct Acquired {
        TleSnapshot snap;
        bool ok = false;
        bool networkTried = false;  // httpGet llego a llamarse (fallo o cuerpo ilegible)
        bool hostError = false;    // non-200 de CelesTrak en esta peticion (parar pase)
        double blockedUntilUtc = 0.0;  // host guard para los grupos no intentados
        std::string note;           // motivo de la puerta o del fallo
    };
    // `pauseBeforeNetwork`: cortesia de cuota — acquireGroup la invoca justo
    // antes de httpGet, asi la pausa solo separa peticiones de red REALES
    // (un grupo servido de cache no paga espera). fetch() pasa una que la
    // deja pasar solo desde la segunda descarga del pase.
    bool acquireGroup(const GroupSpec& group, double utc, Acquired& out,
                      const std::function<void()>& pauseBeforeNetwork = {});
    // Puerta de un grupo. Si la clave no esta en el mapa, se carga del sidecar
    // (un grupo nuevo tras actualizar no arranca con el estado en blanco).
    fetchgate::State gateFor(const std::string& key);
    // Guarda la puerta en el mapa y en el sidecar (best-effort).
    void storeGate(const std::string& key, const fetchgate::State& st);
    // SatNOGS tiene su propia puerta: un bloqueo de CelesTrak no debe
    // silenciar otro proveedor (revision C2a).
    bool fetchSatnogs(double utc, TleSnapshot& out, std::string& err);
    // Sirve SatNOGS (cache propia o red) e instala el snapshot. Devuelve false
    // y deja el motivo en error_ si tampoco hay nada. Solo se llama UNA vez,
    // cuando la fusion de los grupos no dejo nada.
    bool serveSatnogs(double utc, const std::string& why);

    // Persiste el sidecar de una puerta (best-effort).
    void persistState(const std::string& key, const fetchgate::State& st);
    // Edad de una cache en segundos, o -1 si no hay.
    static double cacheAgeOf(const std::filesystem::path& path, double utc);

    mutable std::mutex mutex_;
    TleSnapshot latest_;
    std::atomic<bool> fetching_{false};
    // Una puerta POR GRUPO: el estado de `weather` no puede pisar el de
    // `active` (antes habia un unico `gate_`).
    // Pausa entre peticiones de red a grupos distintos: 2 s reales por
    // defecto (Cortesia de cuota de CelesTrak), inyectable en pruebas.
    std::function<void()> groupPause_ = [] {
        std::this_thread::sleep_for(std::chrono::seconds(2));
    };
    std::map<std::string, fetchgate::State> gates_;
    // Defecto sin seleccion explicita: par pequeno (C2b-31). `active` es el
    // superconjunto y solo se pide cuando alguien lo elige en la UI.
    std::vector<std::string> groupKeys_ = {"amateur", "stations"};
    fetchgate::State gateSatnogs_; // puerta de SatNOGS (proveedor distinto)
    double snapshotEpoch_ = 0.0;
    std::filesystem::path cachePath_;
    std::filesystem::path satnogsCachePath_;   // cache PROPIA de SatNOGS
    std::filesystem::path statePath_;
    bool dirFallback_ = false;
    unsigned long generation_ = 0;
    std::string error_;
    std::string status_;
    Transport transport_;   // vacio en produccion (curl)
};
