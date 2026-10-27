#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "Data/OrbitProp.h"

// Catalogo TLE de satelites activos. Cadena de fuentes, en orden:
//
// 1) CelesTrak (primaria, viva):
//    https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=tle
//    Verificado en vivo el 2026-10-27: HTTP 200, "text/plain; charset=UTF-8",
//    2 671 704 bytes = 47 709 lineas = 15 903 objetos. Texto plano, 3 lineas
//    por objeto: nombre, linea 1, linea 2 (TLE clasico de 69 columnas).
//
// 2) SatNOGS (fallback, vivo):
//    https://db.satnogs.org/api/tle/?format=json
//    Verificado en vivo el 2026-10-27: HTTP 200, "application/json",
//    537 295 bytes, 1 735 filas, SIN paginacion (Content-Length completo y
//    sin cabecera Link). Array de objetos con los campos tle0 ("0 NOMBRE"),
//    tle1, tle2, tle_source, sat_id, norad_cat_id, updated (ISO-8601).
//    Solo cubre satelites con emisor registrado: es un subconjunto, no el
//    catalogo activo completo.
//
// 3) Cache de disco "cache/satellites.tle" (opcional, NUNCA "live"):
//    primera linea "# tle-cache v1 epoch=<segundos UTC> count=<n>" y despues
//    el volcado TLE en el mismo formato de 3 lineas. Se sirve solo si es mas
//    joven que kCacheTtlSec; la fuente se declara TleSource::Cache y la nota
//    lo dice. El fichero se escribe con tmp + rename (nunca a medias).
//
// HONESTIDAD: solo TLEs reales parseados. Si las tres fuentes fallan el
// snapshot queda invalido (valid == false) con el motivo en
// getErrorMessage() y NADA se dibuja. Nunca se inventan satelites ni
// posiciones. Los TLEs con checksum roto o campos ilegibles se DESCARTAN y
// se cuentan en la nota (no se auto-corrigen). Techo de 20 000 satelites.
//
// CIUDADANIA: el snapshot vivo se reutiliza kLiveTtlSec (6 h, el mismo TTL
// que la demo web); fetchAsync() durante ese plazo no vuelve a pedir a la red.
// User-Agent propio. Timeouts 25 s (CelesTrak) y 15 s (SatNOGS).
//
// La posicion NO se calcula aqui: este modulo solo entrega elementos medios
// (orbitprop::OrbitElements). Ver OrbitProp.h para el modelo y sus limites.

enum class TleSource { None, Celestrak, SatNogs, Cache };

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
    void init();
    // Descarga + parsea en hilo detached. No-op si ya hay una en curso.
    // Si el snapshot vivo tiene menos de kLiveTtlSec, no toca la red.
    void fetchAsync(double utc);
    bool isFetching() const { return fetching_; }

    // COPIA del snapshot bajo mutex. Son ~16k filas: llamarlo solo cuando
    // cambie getGeneration() (p.ej. una vez cada 6 h), no cada frame.
    TleSnapshot getSnapshot() const;

    TleSource source() const;               // fuente del ultimo snapshot
    std::string getSourceLabel() const;     // "celestrak"/"satnogs"/"cache"/"sin datos"
    // Copia bajo mutex: el texto lo escribe el hilo de fetch, asi que devolver
    // una referencia seria una carrera al leerla desde el hilo principal.
    std::string getErrorMessage() const;
    double getLastFetchTime() const { return lastFetchUtc_; }

    // Sube en cada snapshot valido instalado (cambio de catalogo).
    unsigned long getGeneration() const;
    int satCount() const;

    // Puros (sin red): parsean y dejan el snapshot listo. Devuelven true si
    // hay al menos un TLE valido. `out.source` lo fija el parser.
    static bool parseTleCatalog(const std::string& text, double nowUtc, TleSnapshot& out);
    static bool parseSatNogsJson(const std::string& json, double nowUtc, TleSnapshot& out);

    // Cache de disco (tambien puro: solo ficheros). loadCache valida la edad.
    static bool loadCache(const std::string& path, double nowUtc, TleSnapshot& out);
    static bool saveCache(const std::string& path, const TleSnapshot& snap);

    static const char* sourceLabel(TleSource s);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool httpGet(const char* url, long timeoutSec, std::string& body, std::string& err);
    bool fetch(double utc);

    mutable std::mutex mutex_;
    TleSnapshot latest_;
    std::atomic<bool> fetching_{false};
    double lastFetchUtc_ = 0.0;
    unsigned long generation_ = 0;
    std::string error_;
};
