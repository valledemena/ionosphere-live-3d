#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

#include "Data/GiroAdapter.h"  // StationSample + GiroHistoryPoint (reuso)

// DiasAdapter: red europea DIAS Ionostream (NOA, Atenas) via HTTP JSON.
//
// Endpoint: GET https://electron.space.noa.gr/ionostream/api/v2/idb/sao/pager
//   ?station=CODE&start=ISO&end=ISO&limit=N
// Responde {"items":[{dataset:{ursi_code,timestamp,...},
//                     scaled:{foF2,mufD,foE,foEs,fmin,fxI,hF,hE,...}}, ...]}.
// OJO verificado: el filtro `station` se ignora (devuelve las ultimas ~50
// globales) -> se filtra por ursi_code en cliente. `9999`/null = ausente.
// hmF2 <- phF2lyr (la unica altura de pico disponible).
//
// Rol: COMPLEMENTO europeo de GiroAdapter (misma StationSample). EB040 no
// fluye a DIAS (verificado 2h/24h/7d) -> RO040/EB040 siguen via GIRO.
// Sin red: muestras vacias (GIRO cubre).
class DiasAdapter {
public:
    DiasAdapter();
    void init();
    // Descarga ventana de 2h y reconstruye muestras + historial (cap 36).
    // Devuelve true si alguna estacion quedo valida.
    bool fetch(double utc);
    // Igual en hilo background (no bloquea el render).
    void fetchAsync(double utc);

    bool isFetching() const { return fetching_; }
    int getStationCount() const;
    // Muestras SIN coordenadas (el pager no trae lat/lon): App las resuelve
    // contra el catalogo GIRO antes de mostrar.
    std::vector<StationSample> copySamples() const;
    double getLastFetchTime() const { return lastFetchUtc_; }
    const std::string& getErrorMessage() const { return error_; }

    // Parseo expuesto para tests (no usa estado).
    static bool parsePager(const std::string& json, std::vector<StationSample>& out);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchURL(const std::string& url, std::string& response);
    static std::string windowStart(int hoursAgo);
    static std::string windowEnd();

    mutable std::mutex mutex_;
    std::vector<StationSample> samples_;
    std::atomic<bool> fetching_{false};
    double lastFetchUtc_ = 0.0;
    std::string error_;
};
