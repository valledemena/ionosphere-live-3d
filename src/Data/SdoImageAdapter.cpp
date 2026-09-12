// M11: imagenes SDO (implementacion; ver SdoImageAdapter.h).
#include "Data/SdoImageAdapter.h"

#include <curl/curl.h>
#include <stb_image.h>

#include <cstdio>

namespace {
constexpr double kCacheMaxAgeS = 900.0;  // 15 min por producto (plan §13.1)
}

void SdoImageAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    printf("[OK] SdoImageAdapter initialized\n");
}

const char* SdoImageAdapter::urlFor(SdoProduct product) {
    return product == SdoProduct::A304
               ? "https://sdo.gsfc.nasa.gov/assets/img/latest/latest_512_0304.jpg"
               : "https://sdo.gsfc.nasa.gov/assets/img/latest/latest_512_HMIIF.jpg";
}

const char* SdoImageAdapter::nameFor(SdoProduct product) {
    return product == SdoProduct::A304 ? "A304" : "HMIIF";
}

size_t SdoImageAdapter::writeCallback(void* contents, size_t size, size_t nmemb,
                                      std::vector<char>* userp) {
    size_t total = size * nmemb;
    const char* p = (const char*)contents;
    userp->insert(userp->end(), p, p + total);
    return total;
}

bool SdoImageAdapter::fetchURL(const std::string& url, std::vector<char>& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || http != 200 || response.size() < 4) return false;
    // Magic JPEG SOI antes de decodificar.
    const unsigned char* b = (const unsigned char*)response.data();
    if (b[0] != 0xFF || b[1] != 0xD8) return false;
    return true;
}

const SdoImage& SdoImageAdapter::getImage(SdoProduct product) const {
    return images_[(int)product];
}

double SdoImageAdapter::getLastFetchTime(SdoProduct product) const {
    return lastFetchUtc_[(int)product];
}

bool SdoImageAdapter::fetch(SdoProduct product, double utc) {
    const int k = (int)product;
    SdoImage& img = images_[k];
    if (!visible_.load()) return img.valid;  // gate: panel cerrado = 0 peticiones
    if (img.valid && utc - lastFetchUtc_[k] < kCacheMaxAgeS) return true;
    std::vector<char> jpg;
    if (!fetchURL(urlFor(product), jpg)) {
        error_ = std::string("SDO ") + nameFor(product) + " unreachable";
        return img.valid;  // conserva la anterior
    }
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load_from_memory((const unsigned char*)jpg.data(),
                                              (int)jpg.size(), &w, &h, &ch, 3);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        error_ = std::string("SDO ") + nameFor(product) + " decode failed";
        return img.valid;
    }
    img.w = w;
    img.h = h;
    img.rgb.assign(px, px + (size_t)w * h * 3);
    stbi_image_free(px);
    img.timeUtc = utc;
    img.product = k;
    img.valid = true;
    const double age = utc - lastFetchUtc_[k];
    lastFetchUtc_[k] = utc;
    error_.clear();
    printf("[Sdo] fetch %s %dx%d (age %.0fs)\n", nameFor(product), w, h, age);
    return true;
}
