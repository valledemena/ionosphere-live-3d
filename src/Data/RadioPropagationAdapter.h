#pragma once
#include <string>
#include <vector>

// Muestra global de propagacion HF / efectos ionosfericos (Fase 5).
// - MUF/LUF/FOT/Spread-F derivan de GIRO getbest (via GiroAdapter samples, M4).
// - S4/AE/TAI/Faraday intentan NOAA SWPC y caen a mock plausible si no hay red.
struct RadioPropSample {
    float muf = 0.0f;        // MHz, mediana global MUF(D) 3000km
    float luf = 0.0f;        // MHz, lowest usable frequency (derivada)
    float fot = 0.0f;        // MHz, frequency of optimum traffic ~= 0.85*MUF
    float s4 = 0.0f;         // indice de centelleo S4 (0..1, mostrado en dB aprox)
    float ae = 0.0f;         // nT, auroral electrojet (proxy de Kp si no hay Kyoto)
    float tai = 0.0f;        // indice TEC activity (proxy 0..3)
    float faradayDeg = 0.0f; // deg, mediana de Omega REAL por estacion (cada una a su FOT, un salto)
    float spreadF = 0.0f;    // %, ocurrencia Spread-F ecuatorial/nocturna
    double timeUtc = 0.0;
    bool valid = false;
};

struct RadioPropHistoryPoint {
    double timeUtc = 0.0;
    float value = 0.0f;
};

// Variables de rejilla que el adapter puede interpolar (IDW) desde las
// muestras GIRO. Los IDs son estables para App.cpp:
//   0=LUF [MHz], 1=FOT [MHz], 2=Spread-F [%], 3=MUF [MHz] (copia GIRO)
enum RadioGridVar {
    RADIO_LUF = 0,
    RADIO_FOT = 1,
    RADIO_SPREAD_F = 2,
    RADIO_MUF = 3
};

struct StationSample;  // fwd (GiroAdapter.h) - se evita el include circular

class RadioPropagationAdapter {
public:
    void init();
    // Calcula el snapshot global a partir de las muestras GIRO + intento NOAA.
    // Devuelve true si uso datos GIRO reales, false si todo mock.
    // M7 (plan 10): xrayFlux (W/m^2, GOES 0.1-0.8 nm) activa el termino SID
    // del LUF global; el worker lo pasa del bundle X-ray del pase anterior
    // (rp corre antes que xr en el mismo pase; stale <= 5 min — sellado).
    bool fetch(double utc, const std::vector<StationSample>& giroSamples, float xrayFlux);

    const RadioPropSample& getLatest() const { return latest_; }
    const std::vector<RadioPropHistoryPoint>& getAeHistory() const { return aeHistory_; }
    const std::vector<RadioPropHistoryPoint>& getS4History() const { return s4History_; }
    double getLastFetchTime() const { return lastFetchUtc_; }
    bool isSimulated() const { return simulated_; }
    bool usedGiroReal() const { return usedGiroReal_; }
    const std::string& getErrorMessage() const { return error_; }

    // --- Derivadas por estacion (ITU / practica operacional) ---
    // FOT = 0.85 * MUF(D). LUF: absorcion D diurna + termino SID por flare
    // X-ray (M7, plan 10 — Sauer-Wilkinson simplificado); cap 0.9*foF2 solo
    // en calma, suelo 2 MHz, techo 20 MHz.
    // Spread-F %: pico nocturno ecuatorial (|lat|<20, noche) modulado por Kp proxy.
    // S4: pico ecuatorial nocturno post-puesta de sol.
    static float deriveLUF(float muf, float foF2, float latDeg, float lonDeg, double utc,
                           float xrayFlux, float kp);
    static float deriveFOT(float muf);
    static float deriveSpreadF(float latDeg, float lonDeg, double utc, float kpProxy);
    static float deriveS4(float latDeg, float lonDeg, double utc);
    // M'-1 (ruling 142 sec.4): rotacion de Faraday REAL en camino vertical v1
    // (juguete 2.2*TEC*(0.4+0.6|..|) EXTINGUIDO). Omega en radianes via
    // igrf::omegaRad con B vertical IGRF-14 a 300 km. tecM2 en m^-2 (SI).
    static double stationOmegaRad(double latDeg, double lonDeg, double tecM2,
                                  double freqHz);
    // Cadena TEC unica (3 eslabones declarados): grid GloTec si valido (rung
    // 0) -> heuristica nmF2-slab (rung 1) -> mock 15 TECU (rung 2). SI m^-2.
    // El worker NO toca el grid (hilo principal): sus eslabones son nmF2->15.
    static float resolveTecSI(bool gridOk, float gridTecu, bool hasNmF2, float nmF2,
                              int& rung);
    // Mediana (patron mufMed): copia ordenada, elemento central superior.
    static float medianOf(std::vector<float> v);
    // M7 (plan 10, P4 sellado): termino de flare del LUF (MHz-equivalente,
    // A ~ F^0.75). Unica fuente de verdad: la usa deriveLUF (dayF interno
    // lstH) y el panel para el trigger del aviso (dayFactor = max(0, cosChi);
    // caveat dayF-vs-cosChi del sello: ambos monotonos en F, advisory).
    static float flareAbsorption(float xrayFlux, float dayFactor);
    // M7 (plan 10): absorcion D mostrable en dB (SID diurno + auroral Kp)
    // para el aviso del panel. cosChi del circuito via densityDir (patron M5).
    static float dRegionAbsorptionDb(float cosChi, float xrayFlux, float kp);
    // Valor por estacion segun RadioGridVar (para IDW en App.cpp o buildGrid).
    static float stationValue(const StationSample& s, int var, double utc, float kpProxy,
                              float xrayFlux);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchURL(const std::string& url, std::string& response);
    void fillSimulated(double utc);
    // Intenta leer Kp actual de NOAA para derivar AE/TAI (devuelve Kp o -1).
    float tryFetchKpProxy();

    RadioPropSample latest_;
    std::vector<RadioPropHistoryPoint> aeHistory_;
    std::vector<RadioPropHistoryPoint> s4History_;
    double lastFetchUtc_ = 0.0;
    bool simulated_ = true;
    bool usedGiroReal_ = false;
    std::string error_;
};
