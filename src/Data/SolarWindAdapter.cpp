#include "SolarWindAdapter.h"
#include <curl/curl.h>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <algorithm>

// Endpoints reales de NOAA SWPC (RTSW), 1 minuto. El endpoint "mag-1m.json" del
// enunciado devuelve 404 y no tiene campos sw_speed/sw_density/sw_p; los productos
// reales son dos archivos JSON:
//   - rtsw_mag_1m.json  -> campo magnetico: Bz en "bz_gsm" (nT)
//   - rtsw_wind_1m.json -> plasma: "proton_speed" (km/s), "proton_density" (cm^-3)
#define SW_MAG_URL  "https://services.swpc.noaa.gov/json/rtsw/rtsw_mag_1m.json"
#define SW_WIND_URL "https://services.swpc.noaa.gov/json/rtsw/rtsw_wind_1m.json"

// ---------------------------------------------------------------------------
// Mini-parser JSON (arrays planos de objetos con valores numericos/string).
// ---------------------------------------------------------------------------
static void skipWsCl(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                            s[i] == '\r' || s[i] == ':' || s[i] == ',')) i++;
}

static bool readString(const std::string& s, size_t& i, std::string& out) {
    skipWsCl(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out.clear();
    while (i < s.size() && s[i] != '"' && s[i] != '\\') { out.push_back(s[i]); i++; }
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    return true;
}

static double readNumber(const std::string& s, size_t& i) {
    skipWsCl(s, i);
    size_t start = i;
    while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '.' ||
                            s[i] == '-' || s[i] == '+' || s[i] == 'e' || s[i] == 'E')) i++;
    if (i == start) return 0.0;
    return atof(s.substr(start, i - start).c_str());
}

static size_t findMatchingBrace(const std::string& s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size(); i++) {
        if (s[i] == '"') { size_t j = i + 1; while (j < s.size() && s[j] != '"' && s[j] != '\\') j++; i = j; continue; }
        if (s[i] == '{') depth++;
        else if (s[i] == '}') { depth--; if (depth == 0) return i; }
    }
    return std::string::npos;
}

static bool objectNumber(const std::string& s, size_t a, size_t b,
                         const std::string& key, double& value) {
    size_t i = a;
    while (i < b && i < s.size()) {
        std::string k;
        if (!readString(s, i, k)) { i++; continue; }
        size_t v = i;
        while (v < b && v < s.size() && s[v] != ':' && s[v] != ',') { v++; }
        v++;
        skipWsCl(s, v);
        if (k == key) { value = readNumber(s, v); return true; }
        i = v;
    }
    return false;
}

static bool objectString(const std::string& s, size_t a, size_t b,
                         const std::string& key, std::string& out) {
    size_t i = a;
    while (i < b && i < s.size()) {
        std::string k;
        if (!readString(s, i, k)) { i++; continue; }
        size_t v = i;
        while (v < b && v < s.size() && s[v] != ':' && s[v] != ',') { v++; }
        v++;
        skipWsCl(s, v);
        if (k == key) { readString(s, v, out); return true; }
        i = v;
    }
    return false;
}

// Convierte "YYYY-MM-DDTHH:MM:SS" (UTC) a tiempo Unix. Algoritmo de dias desde
// la era civil (Howard Hinnant).
static double isoToEpoch(const std::string& t) {
    if (t.size() < 19) return 0.0;
    int Y  = atoi(t.substr(0, 4).c_str());
    int Mo = atoi(t.substr(5, 2).c_str());
    int D  = atoi(t.substr(8, 2).c_str());
    int H  = atoi(t.substr(11, 2).c_str());
    int Mi = atoi(t.substr(14, 2).c_str());
    int S  = atoi(t.substr(17, 2).c_str());

    auto daysFromCivil = [](int y, int m, int d) -> long long {
        y -= m <= 2;
        long long era = (y >= 0 ? y : y - 399) / 400;
        unsigned yoe = (unsigned)(y - era * 400);
        unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + (long long)doe - 719468;
    };
    long long days = daysFromCivil(Y, Mo, D);
    return (double)days * 86400.0 + H * 3600.0 + Mi * 60.0 + S;
}

void SolarWindAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    latest_ = SolarWindSample{};
    printf("[OK] SolarWindAdapter initialized\n");
}

size_t SolarWindAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool SolarWindAdapter::fetchURL(const std::string& url, std::string& response) {
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

// Barre un array de objetos y extrae para cada objeto el valor de 'key' + time_tag.

void SolarWindAdapter::fillSimulated(double utc) {
    simulated_ = true;
    double t = utc;
    latest_.timeUtc = t;
    latest_.speed   = 380.0f + 40.0f * sinf((float)(t * 0.000004f));
    latest_.density = 5.5f + 2.0f * sinf((float)(t * 0.000009f) + 1.0f);
    latest_.bz      = 1.5f + 3.0f * sinf((float)(t * 0.000012f));
    latest_.valid   = true;

    bzHistory_.clear();
    for (int i = 0; i < 288; i++) {   // ~24h en pasos de 5 min
        double tt = t - (287 - i) * 300.0;
        float v = 1.5f + 3.5f * sinf((float)(i * 0.13f)) + 0.8f * sinf((float)(i * 0.57f));
        bzHistory_.push_back({tt, v});
    }
}

bool SolarWindAdapter::fetch(double utc) {
    simulated_ = false;

    // 1) Campo magnetico (Bz).
    std::string magJson;
    std::vector<SolarWindHistoryPoint> magHist;   // time, bz
    bool gotMag = false;
    if (fetchURL(SW_MAG_URL, magJson)) {
        std::vector<std::pair<double,float>> rows;
        size_t pos = 0;
        while (true) {
            size_t open = magJson.find('{', pos);
            if (open == std::string::npos) break;
            size_t close = findMatchingBrace(magJson, open);
            if (close == std::string::npos) break;
            double t = 0, bz = 0;
            if (objectNumber(magJson, open, close, "bz_gsm", bz)) {
                std::string ts;
                objectString(magJson, open, close, "time_tag", ts);
                t = isoToEpoch(ts);
                if (t > 0.0) { rows.push_back({t, (float)bz}); gotMag = true; }
            }
            pos = close + 1;
        }
        // ordenar ascendente por tiempo
        std::sort(rows.begin(), rows.end());
        for (auto& r : rows) magHist.push_back({r.first, r.second});
    } else {
        fprintf(stderr, "[SolarWindAdapter] No se pudo obtener mag JSON: %s\n", error_.c_str());
    }

    // 2) Plasma (velocidad/densidad).
    std::string windJson;
    std::vector<SolarWindSample> windRows;
    bool gotWind = false;
    if (fetchURL(SW_WIND_URL, windJson)) {
        size_t pos = 0;
        while (true) {
            size_t open = windJson.find('{', pos);
            if (open == std::string::npos) break;
            size_t close = findMatchingBrace(windJson, open);
            if (close == std::string::npos) break;
            double sp = 0, den = 0, t = 0;
            if (objectNumber(windJson, open, close, "proton_speed", sp)) {
                objectNumber(windJson, open, close, "proton_density", den);
                std::string ts;
                objectString(windJson, open, close, "time_tag", ts);
                t = isoToEpoch(ts);
                SolarWindSample s;
                s.speed = (float)sp;
                s.density = (float)den;
                s.timeUtc = t;
                s.valid = true;
                windRows.push_back(s);
                gotWind = true;
            }
            pos = close + 1;
        }
        std::sort(windRows.begin(), windRows.end(),
                  [](const SolarWindSample& a, const SolarWindSample& b) { return a.timeUtc < b.timeUtc; });
    } else {
        fprintf(stderr, "[SolarWindAdapter] No se pudo obtener wind JSON: %s\n", error_.c_str());
    }

    // 3) Combinar: muestras mas recientes de ambos.
    if (!gotMag && !gotWind) {
        fprintf(stderr, "[SolarWindAdapter] Ambos endpoints fallaron -> simulado.\n");
        fillSimulated(utc);
        lastFetchUtc_ = utc;
        return false;
    }

    // Mag: la ultima Bz.
    float bz = 0.0f;
    if (!magHist.empty()) bz = magHist.back().value;

    // Wind: la ultima velocidad/densidad.
    float speed = 0.0f, density = 0.0f;
    if (!windRows.empty() && windRows.back().valid) {
        speed = windRows.back().speed;
        density = windRows.back().density;
    }

    // Combinar el timestamp: el mas reciente de cualquiera.
    double latestT = 0.0;
    if (!magHist.empty()) latestT = fmax(latestT, magHist.back().timeUtc);
    if (!windRows.empty()) latestT = fmax(latestT, windRows.back().timeUtc);

    latest_.speed = speed;
    latest_.density = density;
    latest_.bz = bz;
    latest_.timeUtc = latestT;
    latest_.valid = (speed > 0.0f || gotMag);

    // Historico de Bz: mostrar las ultimas ~24h de magHist.
    bzHistory_.clear();
    if (!magHist.empty()) {
        double cutoff = magHist.back().timeUtc - 24.0 * 3600.0;
        for (const auto& h : magHist) if (h.timeUtc >= cutoff) bzHistory_.push_back(h);
    }

    lastFetchUtc_ = utc;
    simulated_ = false;
    error_.clear();
    printf("[SolarWindAdapter] Real solar wind: speed=%.0f km/s density=%.1f cm^-3 Bz=%.1f nT (hist %d pts)\n",
           latest_.speed, latest_.density, latest_.bz, (int)bzHistory_.size());
    return true;
}

int SolarWindAdapter::getCarringtonRotation(double utc) {
    // Rotacion de Carrington aproximada. Calibrada: CR ~2280 el 2025-07-09 (JD ~2460848).
    double jd = 2440587.5 + utc / 86400.0;
    double base = 2460848.0;
    int cr = (int)floor((jd - base) / 27.2753) + 2280;
    return cr;
}
