#pragma once
#include <string>
#include <vector>

// Muestra de viento solar (para el HUD y la capa de flechas 3D).
struct SolarWindSample {
    float speed = 0.0f;    // velocidad del viento solar (km/s)
    float density = 0.0f;  // densidad de protones (cm^-3)
    float bz = 0.0f;       // componente Bz del campo magnetico (nT); +/- = norte/sur
    double timeUtc = 0.0;  // timestamp Unix de la muestra
    bool valid = false;
};

// Punto de la serie temporal (para la mini-grafica de Bz de las ultimas 24h).
struct SolarWindHistoryPoint {
    double timeUtc = 0.0;
    float value = 0.0f;   // Bz (nT)
};

// SolarWindAdapter: obtiene el viento solar desde NOAA SWPC (RT SW) y lo ofrece
// como la muestra mas reciente + historico de Bz.
//
// Endpoints reales (el "mag-1m.json" del plan devuelve 404 en NOAA):
//   - https://services.swpc.noaa.gov/json/rtsw/rtsw_mag_1m.json
//     -> campo magnetico: Bz en "bz_gsm" (nT)
//   - https://services.swpc.noaa.gov/json/rtsw/rtsw_wind_1m.json
//     -> plasma: "proton_speed" (km/s), "proton_density" (cm^-3)
// Ambos son arrays de objetos con "time_tag" ISO (YYYY-MM-DDTHH:MM:SS).
//
// Si la red falla, se devuelve un snapshot simulado (valores plausibles de un
// dia tranquilo a moderado) para que el HUD y la capa sean visibles siempre.
class SolarWindAdapter {
public:
    void init();
    // Descarga y parsea ambos endpoints; rellena latest_ e bzHistory_. Devuelve
    // true si logro datos reales, false si hubo que recurrir al simulador.
    bool fetch(double utc);

    const SolarWindSample& getLatest() const { return latest_; }
    // Historico de Bz (ultimas ~24h), para la mini-grafica.
    const std::vector<SolarWindHistoryPoint>& getBzHistory() const { return bzHistory_; }
    double getLastFetchTime() const { return lastFetchUtc_; }
    bool isSimulated() const { return simulated_; }
    const std::string& getErrorMessage() const { return error_; }

    // Numero de rotacion de Carrington (aproximado, para el panel de disco solar).
    // Estatico: no usa estado (el adapter vive en el hilo worker).
    static int getCarringtonRotation(double utc);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchURL(const std::string& url, std::string& response);
    void fillSimulated(double utc);

    SolarWindSample latest_;
    std::vector<SolarWindHistoryPoint> bzHistory_;
    double lastFetchUtc_ = 0.0;
    bool simulated_ = true;
    std::string error_;
};