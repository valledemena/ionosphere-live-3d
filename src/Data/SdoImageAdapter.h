#pragma once
// =============================================================================
// M11: imagenes SDO (disco solar HMIIF + AIA-304). Patron GloTec/Xray, worker-local.
//
// Descarga JPG (curl, bytes) + decode stb EN EL WORKER; el render solo hace
// glTexImage2D al cambiar el bundle (pointer-compare, patron lastXray).
// Trafico con gate: solo con el panel visible + cache 15 min por producto.
// =============================================================================
#include <atomic>
#include <string>
#include <vector>

enum class SdoProduct { HMIIF = 0, A304 = 1 };

struct SdoImage {
    int w = 0, h = 0;
    std::vector<unsigned char> rgb;  // 3 canales (JPG no tiene alpha)
    double timeUtc = 0.0;
    int product = 0;
    bool valid = false;
};

class SdoImageAdapter {
public:
    void init();
    // Gate de trafico (lo alimenta el checkbox del panel Sun).
    void setVisible(bool v) { visible_.store(v); }
    // Descarga+decodea si toca (cache 15 min/producto, gate visible).
    // Devuelve true con imagen valida (nueva o cacheada).
    bool fetch(SdoProduct product, double utc);
    const SdoImage& getImage(SdoProduct product) const;
    double getLastFetchTime(SdoProduct product) const;
    const std::string& getErrorMessage() const { return error_; }

    static const char* urlFor(SdoProduct product);
    static const char* nameFor(SdoProduct product);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb,
                                std::vector<char>* userp);
    bool fetchURL(const std::string& url, std::vector<char>& response);

    SdoImage images_[2];
    double lastFetchUtc_[2] = {0.0, 0.0};
    std::atomic<bool> visible_{true};
    std::string error_;
};
