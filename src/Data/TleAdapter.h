#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "Data/FetchGate.h"
#include "Data/OrbitProp.h"

// Catalogo TLE de satelites activos. Cadena de fuentes, en orden:
//
// 1) CelesTrak (primaria, viva):
//    https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=tle
//    Verificado en vivo el 2026-10-27: HTTP 200, "text/plain; charset=UTF-8",
//    2 671 704 bytes = 47 709 lineas = 15 903 objetos. Texto plano, 3 lineas
//    por objeto: nombre, linea 1, linea 2 (TLE clasico de 69 columnas).
//
// 2) Cache de disco de CelesTrak, "satellites.tle" en el directorio de datos
//    de usuario (C2a: absoluta, no relativa al CWD). Primera linea
//    "# tle-cache v1 epoch=<segundos UTC> count=<n>" y despues el volcado TLE
//    en el mismo formato de 3 lineas. Se sirve etiquetada como TleSource::Cache
//    y su edad va en la nota. Admite hasta kMaxFallbackAgeSec (72 h) cuando la
//    fuente viva falla; por debajo de kLiveTtlSec (6 h) NI SE PIDE RED.
//    El fichero se escribe con tmp + rename (nunca a medias).
//
// 3) SatNOGS (ultimo recurso, vivo):
//    https://db.satnogs.org/api/tle/?format=json
//    Verificado en vivo el 2026-10-27: HTTP 200, "application/json",
//    537 295 bytes, 1 735 filas, SIN paginacion (Content-Length completo y
//    sin cabecera Link). Array de objetos con los campos tle0 ("0 NOMBRE"),
//    tle1, tle2, tle_source, sat_id, norad_cat_id, updated (ISO-8601).
//    Solo cubre satelites con emisor registrado: es un subconjunto, no el
//    catalogo activo completo. Tiene su PROPIA cache ("satellites-satnogs.tle")
//    y su propia puerta en el sidecar: guardar el subconjunto en la cache de
//    CelesTrak lo haria pasar por catalogo completo durante 6 h. Orden:
//    cache CelesTrak (<=72 h) -> cache SatNOGS (<=72 h) -> red SatNOGS.
//
// HONESTIDAD: solo TLEs reales parseados. Si las tres fuentes fallan el
// snapshot queda invalido (valid == false) con el motivo en
// getErrorMessage() y NADA se dibuja. Nunca se inventan satelites ni
// posiciones. Los TLEs con checksum roto o campos ilegibles se DESCARTAN y
// se cuentan en la nota (no se auto-corrigen). Techo de 20 000 satelites.
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
    // Lineas crudas del TLE (69 columnas, ya sin CR). Permiten auditar el
    // registro y reescribir la cache en el mismo formato de 3 lineas.
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

    // --- Costuras de prueba (no tocar desde la app) ---
    // Sustituye el transporte HTTP. Vaciar el function vuelve a curl.
    void setTransport(Transport t) { transport_ = std::move(t); }
    // Fija cache, cache de SatNOGS y sidecar, y limpia el estado en memoria.
    void setPathsForTests(const std::filesystem::path& cache,
                          const std::filesystem::path& satnogsCache,
                          const std::filesystem::path& state);
    // Relee el sidecar con un reloj explicito (simula un reinicio sin init()).
    void loadStateForTests(double nowUtc);

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
    double nextNetworkUtc(double nowUtc) const;

    // Sube en cada snapshot valido instalado (cambio de catalogo).
    unsigned long getGeneration() const;
    int satCount() const;

    // Puros (sin red): parsean y dejan el snapshot listo. Devuelven true si
    // hay al menos un TLE valido. `out.source` lo fija el parser.
    static bool parseTleCatalog(const std::string& text, double nowUtc, TleSnapshot& out);
    static bool parseSatNogsJson(const std::string& json, double nowUtc, TleSnapshot& out);

    // Cache de disco (tambien puro: solo ficheros). loadCache valida la edad
    // contra maxAgeSec (por defecto el TTL vivo de 6 h). `path` es fs::path
    // para que un perfil con caracteres no ASCII no rompa el tmp+rename.
    static bool loadCache(const std::filesystem::path& path, double nowUtc, TleSnapshot& out,
                          double maxAgeSec = fetchgate::kLiveTtlSec);
    static bool saveCache(const std::filesystem::path& path, const TleSnapshot& snap);
    // Lee solo la cabecera de la cache (epoch). 0 si no hay o no se entiende.
    static double cacheEpoch(const std::filesystem::path& path);

    static const char* sourceLabel(TleSource s);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata);
    HttpResult httpGet(const char* url, long timeoutSec, std::string& body);
    bool fetch(double utc);
    // SatNOGS tiene su propia puerta: un bloqueo de CelesTrak no debe
    // silenciar otro proveedor (revision C2a).
    bool fetchSatnogs(double utc, TleSnapshot& out, std::string& err);
    // Sirve SatNOGS (cache propia o red) e instala el snapshot. Devuelve false
    // y deja el motivo en error_ si tampoco hay nada.
    bool serveSatnogs(double utc, const std::string& why);

    // Sirve cache (si la hay) y deja el motivo en status_/error_.
    bool serveCache(double utc, double maxAgeSec, const std::string& why);
    // Persiste el sidecar de una puerta (best-effort).
    void persistState(const std::string& key, const fetchgate::State& st);
    // Edad de una cache en segundos, o -1 si no hay.
    static double cacheAgeOf(const std::filesystem::path& path, double utc);

    mutable std::mutex mutex_;
    TleSnapshot latest_;
    std::atomic<bool> fetching_{false};
    fetchgate::State gate_;        // puerta de CelesTrak (grupo active)
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
