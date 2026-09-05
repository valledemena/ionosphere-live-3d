#include "AuroraAdapter.h"
#include <curl/curl.h>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <algorithm>

#define OVATION_URL "https://services.swpc.noaa.gov/json/ovation_aurora_latest.json"

static void skipWsCl(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                            s[i] == '\r' || s[i] == ':' || s[i] == ',' ||
                            s[i] == '[' || s[i] == ']')) i++;
}

static double readNumber(const std::string& s, size_t& i) {
    skipWsCl(s, i);
    size_t start = i;
    while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '.' ||
                            s[i] == '-' || s[i] == '+' || s[i] == 'e' || s[i] == 'E')) i++;
    if (i == start) return 0.0;
    return atof(s.substr(start, i - start).c_str());
}

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
    return (double)daysFromCivil(Y, Mo, D) * 86400.0 + H * 3600.0 + Mi * 60.0 + S;
}

AuroraAdapter::AuroraAdapter() = default;

void AuroraAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    grid_.width = gridW_;
    grid_.height = gridH_;
    grid_.values.assign(gridW_ * gridH_, 0.0f);
    grid_.valid = false;
    printf("[OK] AuroraAdapter initialized (%dx%d)\n", gridW_, gridH_);
}

size_t AuroraAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool AuroraAdapter::fetchURL(const std::string& url, std::string& response) {
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

bool AuroraAdapter::parseOvation(const std::string& json, double utc) {
    // Dimensiones aqui (no solo en init): el worker usa instancias sin init().
    grid_.width = gridW_;
    grid_.height = gridH_;
    grid_.values.assign(gridW_ * gridH_, 0.0f);
    std::vector<int> hits(gridW_ * gridH_, 0);

    size_t coordPos = json.find("\"coordinates\"");
    if (coordPos == std::string::npos) { error_ = "sin coordinates"; return false; }

    // Timestamp del pronostico ("Forecast Time":"...Z").
    double ts = 0.0;
    size_t ft = json.find("\"Forecast Time\"");
    if (ft != std::string::npos) {
        size_t q1 = json.find('"', ft + 15);
        if (q1 != std::string::npos) {
            size_t q2 = json.find('"', q1 + 1);
            if (q2 != std::string::npos) ts = isoToEpoch(json.substr(q1 + 1, q2 - q1 - 1));
        }
    }

    size_t pos = coordPos;
    size_t n = 0;
    while (true) {
        pos = json.find('[', pos);
        if (pos == std::string::npos) break;
        size_t i = pos + 1;
        // Evitar el array exterior: debe seguir un numero, no otro '['.
        size_t j = i;
        while (j < json.size() && (json[j] == ' ' || json[j] == '\n' || json[j] == '\r' || json[j] == '\t')) j++;
        if (j >= json.size()) break;
        if (json[j] == '[') { pos = i; continue; }
        double lon = readNumber(json, i);
        // Tras el primer numero debe venir ',' (triplete) y no ']' (cierre).
        size_t k = i;
        while (k < json.size() && (json[k] == ' ' || json[k] == '\n' || json[k] == '\r' || json[k] == '\t')) k++;
        if (k >= json.size() || json[k] != ',') { pos = i; continue; }
        double lat = readNumber(json, i);
        double val = readNumber(json, i);
        pos = i;

        if (lon < 0.0 || lon > 360.0 || lat < -90.0 || lat > 90.0) continue;
        double lonE = (lon > 180.0) ? lon - 360.0 : lon;  // 0..360 -> -180..180
        int lonIdx = (int)floor((lonE + 180.0) / 5.0);
        int latIdx = (int)floor((lat + 90.0) / 2.5);
        lonIdx = std::max(0, std::min(gridW_ - 1, lonIdx));
        latIdx = std::max(0, std::min(gridH_ - 1, latIdx));
        int idx = latIdx * gridW_ + lonIdx;
        if (val > grid_.values[idx]) grid_.values[idx] = (float)val;  // max: borde nitido
        hits[idx]++;
        n++;
    }
    if (n == 0) { error_ = "sin tripletes"; return false; }

    grid_.timestamp = (ts > 0.0) ? ts : utc;
    grid_.valid = true;
    return true;
}

bool AuroraAdapter::fetchAurora(double utc) {
    std::string js;
    if (!fetchURL(OVATION_URL, js)) {
        fprintf(stderr, "[AuroraAdapter] Download failed: %s\n", error_.c_str());
        return false;
    }
    if (!parseOvation(js, utc)) {
        fprintf(stderr, "[AuroraAdapter] Parse failed: %s\n", error_.c_str());
        return false;
    }
    float minv = 1e9f, maxv = -1e9f;
    for (float v : grid_.values) { minv = fminf(minv, v); maxv = fmaxf(maxv, v); }
    lastFetchUtc_ = utc;
    simulated_ = false;
    error_.clear();
    printf("[AuroraAdapter] Ovation OK (%dx%d): min=%.1f max=%.1f\n", grid_.width, grid_.height, minv, maxv);
    return true;
}
