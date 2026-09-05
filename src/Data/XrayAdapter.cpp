#include "XrayAdapter.h"
#include <curl/curl.h>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <algorithm>

#define XRAY_URL "https://services.swpc.noaa.gov/json/goes/primary/xrays-6-hour.json"

void XrayAdapter::classify(float flux, char& cls, float& sub) {
    // Umbrales estandar NOAA (W/m^2, banda 1-8 A). A1.0 = 1e-8.
    const struct { char c; float base; } bands[] = {
        {'A', 1e-8f}, {'B', 1e-7f}, {'C', 1e-6f}, {'M', 1e-5f}, {'X', 1e-4f}
    };
    cls = 'A'; sub = 0.0f;
    if (!(flux > 0.0f)) return;
    for (int i = 4; i >= 0; i--) {
        if (flux >= bands[i].base) { cls = bands[i].c; sub = flux / bands[i].base; break; }
    }
}

float XrayAdapter::flareFactor(float flux) {
    // Multiplicador ILUSTRATIVO (no literatura validada): ~1 en fondo B/C,
    // ~21 en M1, ~41 en X1. log10(0) = -inf -> max() lo deja en 1.
    return 1.0f + 20.0f * std::max(0.0f, log10f(flux / 1e-6f));
}

void XrayAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    latest_ = XraySample{};
    printf("[OK] XrayAdapter initialized\n");
}

size_t XrayAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool XrayAdapter::fetchURL(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { error_ = std::string("curl: ") + curl_easy_strerror(res); return false; }
    if (http != 200) { error_ = "HTTP " + std::to_string(http); return false; }
    return true;
}

// Parser ad-hoc: busca objetos {"time_tag":"...","flux":N,...,"energy":"0.1-0.8nm"}.
bool XrayAdapter::fetch(double utc) {
    std::string js;
    if (!fetchURL(XRAY_URL, js)) {
        fprintf(stderr, "[Xray] Download failed: %s\n", error_.c_str());
        latest_ = XraySample{};
        latest_.flux = 1.0e-7f;   // mock: fondo B1 quieto
        latest_.fclass = 'B'; latest_.fsub = 1.0f;
        latest_.timeUtc = utc;
        latest_.valid = true;
        simulated_ = true;
        lastFetchUtc_ = utc;
        return false;
    }

    double bestT = 0.0;
    float bestFlux = 0.0f;
    bool anyFlux = false;
    size_t pos = 0;
    while (true) {
        size_t open = js.find('{', pos);
        if (open == std::string::npos) break;
        // fin aproximado del objeto (plano, sin anidar)
        size_t close = js.find('}', open);
        if (close == std::string::npos) break;
        std::string obj = js.substr(open, close - open);
        if (obj.find("0.1-0.8nm") != std::string::npos) {
            // flux
            size_t kf = obj.find("\"flux\"");
            size_t kt = obj.find("\"time_tag\"");
            if (kf != std::string::npos && kt != std::string::npos) {
                size_t c = obj.find(':', kf);
                double v = atof(obj.substr(c + 1).c_str());
                size_t q1 = obj.find('"', kt + 11);
                size_t q2 = (q1 == std::string::npos) ? std::string::npos : obj.find('"', q1 + 1);
                std::string ts = (q2 == std::string::npos) ? "" : obj.substr(q1 + 1, q2 - q1 - 1);
                double t = 0.0;
                if (ts.size() >= 19) {
                    int Y = atoi(ts.substr(0, 4).c_str()), Mo = atoi(ts.substr(5, 2).c_str()),
                        D = atoi(ts.substr(8, 2).c_str()), H = atoi(ts.substr(11, 2).c_str()),
                        Mi = atoi(ts.substr(14, 2).c_str()), S = atoi(ts.substr(17, 2).c_str());
                    auto dc = [](int y, int m, int d) -> long long {
                        y -= m <= 2;
                        long long era = (y >= 0 ? y : y - 399) / 400;
                        unsigned yoe = (unsigned)(y - era * 400);
                        unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
                        return era * 146097 + (long long)(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
                    };
                    t = (double)dc(Y, Mo, D) * 86400.0 + H * 3600.0 + Mi * 60.0 + S;
                }
                // Ojo: el fichero trae "flux":0.0 en huecos; solo cuenta flux>0.
                if (v > 0.0 && t >= bestT) { bestT = t; bestFlux = (float)v; anyFlux = true; }
            }
        }
        pos = close + 1;
    }

    if (!anyFlux) {
        error_ = "no long-band flux found";
        fprintf(stderr, "[Xray] %s\n", error_.c_str());
        return false;
    }
    latest_.flux = bestFlux;
    classify(bestFlux, latest_.fclass, latest_.fsub);
    latest_.timeUtc = bestT;
    latest_.valid = true;
    simulated_ = false;
    error_.clear();
    lastFetchUtc_ = utc;
    printf("[Xray] GOES long %.2e W/m2 (%c%.1f)\n", bestFlux, latest_.fclass, latest_.fsub);
    return true;
}
