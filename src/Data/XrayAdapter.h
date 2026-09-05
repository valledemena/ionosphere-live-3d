#pragma once
#include <string>

// Muestra de rayos X GOES (banda larga 0.1-0.8 nm, la relevante para la D).
// Endpoint: https://services.swpc.noaa.gov/json/goes/primary/xrays-6-hour.json
//   [{"time_tag","satellite","flux","observed_flux",...,"energy"}, ...]
// Se usa "flux" (corregido) filtrando energy == "0.1-0.8nm".
struct XraySample {
    float flux = 0.0f;        // W/m^2 (banda 0.1-0.8 nm)
    char fclass = 'A';        // A/B/C/M/X segun umbrales estandar NOAA
    float fsub = 0.0f;        // subclase (p.ej. 4.2 en B4.2)
    double timeUtc = 0.0;
    bool valid = false;
};

class XrayAdapter {
public:
    void init();
    // Descarga y parsea; devuelve true con dato real, false -> mock (B1 quieto).
    bool fetch(double utc);
    const XraySample& getLatest() const { return latest_; }
    double getLastFetchTime() const { return lastFetchUtc_; }
    bool isSimulated() const { return simulated_; }
    const std::string& getErrorMessage() const { return error_; }

    // Clase NOAA desde flujo: A<1e-7<B<1e-6<C<1e-5<M<1e-4<X (banda 1-8 A).
    // Subclase = flujo/base (B4.2 = 4.2e-7). A usa base 1e-8 (A1.0 = 1e-8).
    static void classify(float flux, char& cls, float& sub);
    // Multiplicador ILUSTRATIVO de la D (no literatura validada).
    static float flareFactor(float flux);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchURL(const std::string& url, std::string& response);

    XraySample latest_;
    double lastFetchUtc_ = 0.0;
    bool simulated_ = true;
    std::string error_;
};
