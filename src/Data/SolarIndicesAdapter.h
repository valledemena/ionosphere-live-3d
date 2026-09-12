#pragma once

#include <string>
#include <vector>

#include "Utils/SunDiskMath.h"  // M11: Sunspot (parseo sunspot_report)

// Snapshot de indices solares/geomagneticos para el HUD.
// Fuentes NOAA SWPC reales (JSON), con fallback a mock por campo si falla la red:
//   Kp    <- planetary_k_index_1m.json (estimated_kp, 1 min)
//   Dst   <- geospace/geospace_dst_1_hour.json (dst, 1 min)
//   F10.7 <- 45-day-forecast.json (metric f107, valor del dia 0 + serie diaria)
//   Ap    <- 45-day-forecast.json (metric ap, valor del dia 0)
//   SSN   <- solar-cycle/swpc_observed_ssn.json (swpc_ssn diario, ultimo + 30 dias)
struct IndexSnapshot {
    float kp = 0.0f;        // indice planetario Kp (0-9)
    float dst = 0.0f;       // indice de tormenta geomagnetica Dst (nT)
    float f107 = 0.0f;      // flujo solar 10.7 cm (SFU)
    float ap = 0.0f;        // indice Ap (nT)
    float ssn = 0.0f;       // numero de manchas solares
    double timeUtc = 0.0;   // tiempo del snapshot
    bool valid = false;     // true si al menos un campo es real
    bool simulated = true;  // true si TODOS los campos son mock
    std::string error;      // campos que fallaron, p.ej. "Kp, Dst (offline)"
};

// Serie temporal (para graficas de tendencia del HUD)
struct IndexHistoryPoint {
    double timeUtc = 0.0;
    float value = 0.0f;
};

class SolarIndicesAdapter {
public:
    void init();
    // Descarga y parsea los endpoints NOAA; por cada campo que falle usa mock.
    // Devuelve true si al menos un campo es real.
    bool fetch(double utc);
    const IndexSnapshot& getSnapshot() const { return snapshot_; }
    // Serie temporal de cada indice (para mini-graficas).
    const std::vector<IndexHistoryPoint>& getKpHistory() const { return kpHistory_; }
    const std::vector<IndexHistoryPoint>& getDstHistory() const { return dstHistory_; }
    const std::vector<IndexHistoryPoint>& getF107History() const { return f107History_; }
    const std::vector<IndexHistoryPoint>& getSsnHistory() const { return ssnHistory_; }
    bool isSimulated() const { return snapshot_.simulated; }

    // Endpoints NOAA SWPC (formato JSON plano).
    static const char* kpUrl();
    static const char* dstUrl();
    static const char* f107ApUrl();
    static const char* ssnUrl();
    static const char* sunspotUrl();  // M11: sunspot_report.json (verificado 200)

    // M11: manchas solares (best-effort: en fallo conserva las anteriores).
    bool fetchSunspots(double utc);
    const std::vector<sundisk::Sunspot>& getSunspots() const { return sunspots_; }
    double getLastSunspotFetchTime() const { return lastSunspotUtc_; }
    // Parser puro (testeable): filas sin Region/lat/time se saltan.
    static bool parseSunspotReport(const std::string& js,
                                   std::vector<sundisk::Sunspot>& out);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchURL(const std::string& url, std::string& response);
    // Mock por campo (solo se usa para los campos cuya descarga fallo).
    void fillMockKp(double utc);
    void fillMockDst(double utc);
    void fillMockF107(double utc);
    void fillMockSsn(double utc);

    IndexSnapshot snapshot_;
    std::vector<IndexHistoryPoint> kpHistory_;
    std::vector<IndexHistoryPoint> dstHistory_;
    std::vector<IndexHistoryPoint> f107History_;
    std::vector<IndexHistoryPoint> ssnHistory_;
    std::vector<sundisk::Sunspot> sunspots_;  // M11: ultimo fetch bueno
    double lastSunspotUtc_ = 0.0;
};
