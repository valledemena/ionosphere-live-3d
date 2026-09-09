#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>

// Un punto de la serie temporal de una estacion (fila de scaled.php).
struct GiroHistoryPoint {
    double timeUtc = 0.0;   // Unix epoch (columna Time de la fila)
    float foF2 = 0.0f;
    float foF1 = 0.0f;   // M4/P1: la historia lleva foF1 (replay sin mezclar)
    float muf = 0.0f;
    float hmF2 = 0.0f;
    float foE = 0.0f;
    float foEs = 0.0f;
    float fxI = 0.0f;    // M4/P1: simetria con foF1
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
    std::vector<GiroHistoryPoint> history;  // serie completa (<=72 filas, ascendente; M4: 6 h @ 5 min)
};

// GiroAdapter: obtiene caracteristicas escaladas de la red global GIRO.
//
// - Catalogo: GET https://lgdc.uml.edu/common/DIDBFastStationList  -> (code, name, lat, lon)
// - Datos (M4): GET https://lgdc.uml.edu/fastchar/getbest por estacion de la
//   lista prioritaria (Apendice B del plan, EB040 primera), 1 llamada con los
//   10 caracteres en CSV fijo -> respuesta con serie 24 h (historia cap 72)
//   + ultima muestra. Cortesia 7.2: concurrencia 6, gap entre lanzamientos
//   de 12 s en frio (M4-tuning T1: arranque escalonado, ~5 GETs/min) y
//   250 ms en regimen; cache 10 min por estacion con reloj por intento
//   (M4-tuning T2), backoff x2 hasta 30 min ante 429/fallo.
//   El pool POST a scaled.php se elimino (7.4: sin #ifdef, sin fallback).
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

    // M4 (7.1): parsear una respuesta multichar de getbest (charName CSV en el
    // orden fijo de GETBEST_CHARS: valor del caracter k en el token 2+2k) ->
    // ultima muestra + historia cap 72. Publica y estatica para tests
    // (no usa estado del adapter; "STATUS: ERROR" -> false).
    static bool parseGetbest(const std::string& text, StationSample& out);
    // M4-tuning (T1, sellado): decision pura de cortesia de lanzamiento por
    // estacion. Devuelve el gap minimo (s) entre su lanzamiento y el anterior,
    // o -1 si aun no esta vencida (skip/wait): nunca intentada -> 12 s
    // (arranque escalonado: rafaga fria a ~5 GETs/min); vencida -> 0.25 s
    // (cortesia 7.2). intervalSec = cache/backoff vigente de la estacion.
    // Publica y estatica para tests, como parseGetbest.
    static double launchGapSec(double lastAttemptUtc, double nowUtc,
                               double intervalSec = 600.0);
    // Muestreo de la red en un instante (para replay): interpola cada estacion
    // con historia a 'epoch' (clamp a extremos). Sin historia usa el ultimo valor.
    std::vector<StationSample> sampleHistoryAt(double epoch) const;
    // M4 (P5): estado de red para la UI (TimeBar / panel Layers).
    struct GiroNetStatus {
        int polled = 0;             // prioritarias presentes en el catalogo
        int withData = 0;           // ... de ellas con muestra valida
        int degraded = 0;           // en backoff (429/fallo reciente)
        double retryInMin = 0.0;    // reintento mas proximo (si degraded > 0)
        double histHours = 0.0;     // alcance atras del peor historial (h)
    };
    GiroNetStatus getNetStatus() const;
    // M4 (P4): true si el codigo URSI esta en la lista prioritaria; el resto
    // del catalogo queda para el dropdown (catalogo visible, sin datos).
    bool isPolled(const std::string& code) const;

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchURL(const std::string& url, std::string& response);
    // M4 (7.1): GET getbest por estacion (UA propio, timeout 12 s).
    bool getbestGet(const std::string& ursi, std::string& response);
    // M4 (7.2): una ronda de polling con cortesia. Devuelve el numero de
    // estaciones actualizadas. Cola: solo prioritarias (EB040 primera),
    // cache 10 min, backoff x2 -> 30 min por estacion.
    int pollRound(double nowUtc);
    // M4 (7.2): estado por estacion (cache/backoff), indexado por URSI.
    struct FetchState {
        double lastAttemptUtc = 0.0;
        double intervalSec = 600.0;   // cache 10 min; el backoff la duplica
        bool degraded = false;        // 429/fallo reciente (visible en UI)
    };

    mutable std::mutex mutex_;
    std::vector<StationSample> samples_;
    std::map<std::string, FetchState> fetchState_;
    std::atomic<bool> fetching_{false};
    std::atomic<int> progress_{0};
    double lastFetchUtc_ = 0.0;
};
