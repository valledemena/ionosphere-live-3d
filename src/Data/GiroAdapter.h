#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

// Un punto de la serie temporal de una estacion (fila de scaled.php).
struct GiroHistoryPoint {
    double timeUtc = 0.0;   // Unix epoch (columna Time de la fila)
    float foF2 = 0.0f;
    float muf = 0.0f;
    float hmF2 = 0.0f;
    float foE = 0.0f;
    float foEs = 0.0f;
    float B0 = 0.0f;
    float B1 = 0.0f;
    float fmin = 0.0f;
};

// Una estacion GIRO y su ultima muestra de caracteristicas escaladas.
// Columnas verificadas contra FastChar real (EB040, 2026-09-04):
// Time CS foF2 QD foF1 QD foE QD foEs QD fxI QD MUF(D) QD M(D) QD
// h'F2 QD h'F QD h'E QD h'Es QD hmF2 QD B0 QD B1 QD fmin QD
// (0 si ausente; nmF2 se calcula de foF2).
struct StationSample {
    std::string code;      // URSI code, p.ej. "EB040"
    std::string name;      // nombre, p.ej. "ROQUETES"
    float latDeg = 0.0f;   // latitud geografica (grados)
    float lonDeg = 0.0f;   // longitud geografica (grados, -180..180)
    float foF2 = 0.0f;     // MHz (F2 critical frequency)
    float muf = 0.0f;      // MHz (MUF(D), D=3000km)
    float hmF2 = 0.0f;     // km (altura del pico F2)
    float nmF2 = 0.0f;     // m^-3 (densidad electronica del pico, calculada)
    float foF1 = 0.0f;     // MHz (F1 critical frequency)
    float foE = 0.0f;      // MHz (E critical frequency)
    float foEs = 0.0f;     // MHz (esporadica E)
    float fxI = 0.0f;      // MHz (max frecuencia traza F)
    float hF2 = 0.0f;      // km (altura virtual F2)
    float hF = 0.0f;       // km (altura virtual F)
    float hE = 0.0f;       // km (altura virtual E)
    float hEs = 0.0f;      // km (altura virtual Es)
    float B0 = 0.0f;       // km (IRI thickness parameter)
    float B1 = 0.0f;       // IRI profile shape parameter
    float fmin = 0.0f;     // MHz (minima frecuencia del ionograma)
    double timeUtc = 0.0;  // Unix time de la ultima fila (0 si no parseable)
    bool valid = false;    // true si se obtuvo una muestra real
    int cs = 0;            // Autoscaling Confidence Score
    std::vector<GiroHistoryPoint> history;  // serie completa (<=36 filas, ascendente)
};

// GiroAdapter: obtiene caracteristicas escaladas de la red global GIRO.
//
// - Catalogo: GET https://lgdc.uml.edu/common/DIDBFastStationList  -> (code, name, lat, lon)
// - Datos:    POST https://giro.uml.edu/didbase/scaled.php por estacion (form),
//             parsea la ultima fila de texto plano: foF2(MHz), MUF(D)(MHz), hmF2(km).
//   IMPORTANTE: NO usar CURLOPT_CUSTOMREQUEST "POST" (devuelve HTTP 405); usar
//   CURLOPT_POSTFIELDS que infiere POST correctamente.
//
// Los datos GIRO se publican bajo licencia CC BY-NC-SA 4.0 (uso no comercial).
class GiroAdapter {
public:
    GiroAdapter();
    void init();
    // Descarga el catalogo de estaciones (code,name,lat,lon). Bloquea ~1s.
    bool fetchCatalog();
    // Lanza un hilo en background que consulta cada estacion del catalogo y
    // actualiza los samples incrementalmente. No bloquea el hilo de render.
    void fetchStationsAsync();
    // Fuerza una consulta sincrona de TODAS las estaciones (para tests). Lento.
    bool fetchStationsSync(double utc);

    bool isFetching() const { return fetching_; }
    int getProgress() const { return progress_; }
    int getStationCount() const { return (int)samples_.size(); }
    float getDataIndicator() const;   // % de estaciones con muestra valida (0..100)

    const std::vector<StationSample>& getSamples() const { return samples_; }
    std::vector<StationSample> copySamples() const;
    float getLastUpdateAge() const;   // segundos desde el ultimo refresco

    // Parsear una respuesta de scaled.php -> la ultima muestra valida de la estacion.
    // Publica y estatica para tests (no usa estado del adapter).
    static bool parseScaled(const std::string& text, StationSample& out);
    // Muestreo de la red en un instante (para replay): interpola cada estacion
    // con historia a 'epoch' (clamp a extremos). Sin historia usa el ultimo valor.
    std::vector<StationSample> sampleHistoryAt(double epoch) const;

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchURL(const std::string& url, std::string& response);
    bool postURL(const std::string& url, const std::string& body, std::string& response);
    static std::string utcNowStr(int hoursAgo);

    mutable std::mutex mutex_;
    std::vector<StationSample> samples_;
    std::atomic<bool> fetching_{false};
    std::atomic<int> progress_{0};
    double lastFetchUtc_ = 0.0;
};
