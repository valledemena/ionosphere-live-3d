#include "DiasAdapter.h"
#include <curl/curl.h>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <algorithm>
#include <map>

#define DIAS_PAGER_URL "https://electron.space.noa.gr/ionostream/api/v2/idb/sao/pager"

// --- Mini-parser JSON (mismo estilo que el resto de adapters) ---
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
    if (i == start) return 0.0;   // incluye "null" -> 0
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

static float cleanf(double v) {
    // 9999 (SAO N/A) o no-positivo -> 0 (ausente, convencion del repo).
    if (!(v > 0.0) || v >= 9999.0) return 0.0f;
    return (float)v;
}

DiasAdapter::DiasAdapter() {}

void DiasAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    printf("[OK] DiasAdapter initialized (Ionostream EU)\n");
}

size_t DiasAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool DiasAdapter::fetchURL(const std::string& url, std::string& response) {
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
    if (res != CURLE_OK) { error_ = std::string("curl: ") + curl_easy_strerror(res); return false; }
    if (http != 200) { error_ = "HTTP " + std::to_string(http); return false; }
    return true;
}

static std::string isoShift(int hoursAgo) {
    std::time_t t = std::time(nullptr) + hoursAgo * 3600;
    std::tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return std::string(buf);
}

std::string DiasAdapter::windowStart(int hoursAgo) { return isoShift(-hoursAgo); }
std::string DiasAdapter::windowEnd() { return isoShift(0); }

bool DiasAdapter::parsePager(const std::string& json, std::vector<StationSample>& out) {
    out.clear();
    // Filas por estacion: codigo -> lista (t, campos).
    struct Row { double t; double foF2, muf, hmF2, foE, foEs, fmin, fxI, B0, B1; };
    std::map<std::string, std::vector<Row>> byCode;

    size_t pos = 0;
    while (true) {
        size_t d = json.find("\"dataset\"", pos);
        if (d == std::string::npos) break;
        size_t open = json.find('{', d);
        if (open == std::string::npos) break;
        size_t close = findMatchingBrace(json, open);
        if (close == std::string::npos) break;
        std::string code, ts;
        objectString(json, open, close, "ursi_code", code);
        objectString(json, open, close, "timestamp", ts);
        // El objeto "scaled" hermano (dentro del mismo item): debe aparecer
        // antes que el siguiente "dataset" o se descarta (sin robar ajenos).
        size_t s = json.find("\"scaled\"", close);
        size_t nextDs = json.find("\"dataset\"", close);
        double foF2 = 0, muf = 0, hm = 0, foE = 0, foEs = 0, fmin = 0, fxI = 0, B0 = 0, B1 = 0;
        if (s != std::string::npos && (nextDs == std::string::npos || s < nextDs)) {
            size_t so = json.find('{', s);
            size_t sc = (so == std::string::npos) ? std::string::npos : findMatchingBrace(json, so);
            if (sc != std::string::npos) {
                objectNumber(json, so, sc, "foF2", foF2);
                objectNumber(json, so, sc, "mufD", muf);
                objectNumber(json, so, sc, "phF2lyr", hm);   // altura del pico F2
                objectNumber(json, so, sc, "foE", foE);
                objectNumber(json, so, sc, "foEs", foEs);
                objectNumber(json, so, sc, "fmin", fmin);
                objectNumber(json, so, sc, "fxI", fxI);
                objectNumber(json, so, sc, "b0IRI", B0);
                objectNumber(json, so, sc, "b1IRI", B1);
            }
        }
        if (!code.empty()) {
            double t = isoToEpoch(ts);
            if (t > 0.0) byCode[code].push_back({t, foF2, muf, hm, foE, foEs, fmin, fxI, B0, B1});
        }
        pos = close + 1;
    }

    for (auto& kv : byCode) {
        auto& rows = kv.second;
        std::sort(rows.begin(), rows.end(),
                  [](const Row& a, const Row& b) { return a.t < b.t; });
        if (rows.size() > 36) rows.erase(rows.begin(), rows.end() - 36);
        const Row& last = rows.back();
        float foF2 = cleanf(last.foF2);
        float muf = cleanf(last.muf);
        if (foF2 <= 0.0f && muf <= 0.0f) continue;   // sin dato util
        StationSample st;
        st.code = kv.first;
        st.name = kv.first;   // sin nombre en el pager (App lo resuelve via GIRO)
        st.foF2 = foF2;
        st.muf = muf;
        st.hmF2 = cleanf(last.hmF2);
        st.foE = cleanf(last.foE);
        st.foEs = cleanf(last.foEs);
        st.fmin = cleanf(last.fmin);
        st.fxI = cleanf(last.fxI);
        st.B0 = cleanf(last.B0);
        st.B1 = cleanf(last.B1);
        st.nmF2 = (foF2 > 0.0f) ? (float)(1.24e10 * (double)foF2 * (double)foF2) : 0.0f;
        st.timeUtc = last.t;
        st.valid = true;
        for (const auto& r : rows) {
            GiroHistoryPoint hp;
            hp.timeUtc = r.t;
            hp.foF2 = cleanf(r.foF2);
            hp.muf = cleanf(r.muf);
            hp.hmF2 = cleanf(r.hmF2);
            hp.foE = cleanf(r.foE);
            hp.foEs = cleanf(r.foEs);
            hp.B0 = cleanf(r.B0);
            hp.B1 = cleanf(r.B1);
            hp.fmin = cleanf(r.fmin);
            hp.fxI = cleanf(r.fxI);   // M4/P1: simetria (replay DIAS sin mezclar fxI)
            st.history.push_back(hp);
        }
        out.push_back(st);
        if (kv.first == "EB040")
            printf("[Dias] EB040 flowing via Ionostream!\n");
    }
    return !out.empty();
}

bool DiasAdapter::fetch(double utc) {
    std::string url = std::string(DIAS_PAGER_URL) + "?station=EB040&start=" +
                      windowStart(2) + "&end=" + windowEnd() + "&limit=100";
    std::string js;
    if (!fetchURL(url, js)) {
        fprintf(stderr, "[DiasAdapter] Download failed: %s\n", error_.c_str());
        return false;
    }
    std::vector<StationSample> parsed;
    if (!parsePager(js, parsed)) {
        error_ = "parsePager: sin estaciones validas";
        fprintf(stderr, "[DiasAdapter] %s\n", error_.c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(mutex_);
        samples_ = std::move(parsed);
    }
    lastFetchUtc_ = utc;
    error_.clear();
    printf("[DiasAdapter] OK: %d estaciones EU\n", (int)samples_.size());
    return true;
}

void DiasAdapter::fetchAsync(double utc) {
    if (fetching_.exchange(true)) return;
    std::thread([this, utc]() {
        fetch(utc);
        fetching_.store(false);
    }).detach();
}

int DiasAdapter::getStationCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (int)samples_.size();
}

std::vector<StationSample> DiasAdapter::copySamples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_;
}

