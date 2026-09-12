#include "SolarIndicesAdapter.h"
#include <curl/curl.h>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <algorithm>

// ---------------------------------------------------------------------------
// Fuentes NOAA SWPC reales (verificadas 2026-09-04):
//   Kp    planetary_k_index_1m.json : [{"time_tag","kp_index":int,"estimated_kp":float,...}]
//   Dst   geospace_dst_1_hour.json  : [{"time_tag","dst":float}] (cadencia 1 min)
//   F107/Ap 45-day-forecast.json    : {"data":[{"time","metric":"ap"|"f107","value"}]}
//   SSN   swpc_observed_ssn.json    : [{"Obsdate","swpc_ssn":int}] (diario desde 1996)
// Cada campo cae a mock si su endpoint falla (el resto sigue real).
// ---------------------------------------------------------------------------

const char* SolarIndicesAdapter::kpUrl() {
    return "https://services.swpc.noaa.gov/json/planetary_k_index_1m.json";
}

const char* SolarIndicesAdapter::dstUrl() {
    return "https://services.swpc.noaa.gov/json/geospace/geospace_dst_1_hour.json";
}

const char* SolarIndicesAdapter::f107ApUrl() {
    return "https://services.swpc.noaa.gov/json/45-day-forecast.json";
}

const char* SolarIndicesAdapter::ssnUrl() {
    return "https://services.swpc.noaa.gov/json/solar-cycle/swpc_observed_ssn.json";
}

// M11: reporte de regiones activas (verificado HTTP 200 en vivo).
const char* SolarIndicesAdapter::sunspotUrl() {
    return "https://services.swpc.noaa.gov/json/sunspot_report.json";
}

// --- Mini-parser JSON (mismo estilo que SolarWindAdapter) ---
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

// "YYYY-MM-DDTHH:MM:SS..." (UTC, ignora sufijo Z) -> Unix epoch.
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

void SolarIndicesAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    snapshot_ = IndexSnapshot{};
    printf("[OK] SolarIndicesAdapter initialized\n");
}

size_t SolarIndicesAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool SolarIndicesAdapter::fetchURL(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || http != 200) return false;
    return true;
}

// Serie (t,v) ordenada, diezmada a maxPts para las mini-graficas.
static std::vector<IndexHistoryPoint> downsample(std::vector<IndexHistoryPoint>& rows, size_t maxPts) {
    std::sort(rows.begin(), rows.end(),
              [](const IndexHistoryPoint& a, const IndexHistoryPoint& b) { return a.timeUtc < b.timeUtc; });
    if (rows.size() <= maxPts) return rows;
    std::vector<IndexHistoryPoint> out;
    size_t step = (rows.size() + maxPts - 1) / maxPts;
    for (size_t i = 0; i < rows.size(); i += step) out.push_back(rows[i]);
    if (out.back().timeUtc != rows.back().timeUtc) out.push_back(rows.back());
    return out;
}

void SolarIndicesAdapter::fillMockKp(double utc) {
    snapshot_.kp = 1.0f;
    kpHistory_.clear();
    for (int i = 0; i < 24; i++)
        kpHistory_.push_back({utc - (23 - i) * 3600.0, 0.5f + 0.5f * fabsf(sinf(i * 0.7f))});
}

void SolarIndicesAdapter::fillMockDst(double utc) {
    snapshot_.dst = -20.0f;
    dstHistory_.clear();
    for (int i = 0; i < 24; i++)
        dstHistory_.push_back({utc - (23 - i) * 3600.0, -10.0f - 15.0f * fabsf(sinf(i * 0.4f))});
}

void SolarIndicesAdapter::fillMockF107(double utc) {
    snapshot_.f107 = 105.0f;
    snapshot_.ap = 5.0f;
    f107History_.clear();
    for (int i = 0; i < 24; i++)
        f107History_.push_back({utc - (23 - i) * 86400.0, 103.0f + 3.0f * sinf(i * 0.9f)});
}

void SolarIndicesAdapter::fillMockSsn(double utc) {
    snapshot_.ssn = 73.0f;
    ssnHistory_.clear();
    for (int i = 0; i < 30; i++)
        ssnHistory_.push_back({utc - (29 - i) * 86400.0, 60.0f + 20.0f * fabsf(sinf(i * 0.6f))});
}

bool SolarIndicesAdapter::fetch(double utc) {
    int realFields = 0;
    std::string failed;

    // 1) Kp (estimated_kp fino; fallback a kp_index entero).
    {
        std::string js;
        std::vector<IndexHistoryPoint> rows;
        bool ok = false;
        if (fetchURL(kpUrl(), js)) {
            size_t pos = 0;
            while (true) {
                size_t open = js.find('{', pos);
                if (open == std::string::npos) break;
                size_t close = findMatchingBrace(js, open);
                if (close == std::string::npos) break;
                double v = 0, t = 0;
                std::string ts;
                bool hasV = objectNumber(js, open, close, "estimated_kp", v) ||
                            objectNumber(js, open, close, "kp_index", v);
                if (hasV && objectString(js, open, close, "time_tag", ts)) t = isoToEpoch(ts);
                if (hasV && t > 0.0 && v >= 0.0 && v <= 9.0) { rows.push_back({t, (float)v}); ok = true; }
                pos = close + 1;
            }
        }
        if (ok) {
            auto ds = downsample(rows, 72);
            kpHistory_ = ds;
            snapshot_.kp = ds.back().value;
            realFields++;
        } else {
            fillMockKp(utc);
            failed += "Kp ";
        }
    }

    // 2) Dst (nT).
    {
        std::string js;
        std::vector<IndexHistoryPoint> rows;
        bool ok = false;
        if (fetchURL(dstUrl(), js)) {
            size_t pos = 0;
            while (true) {
                size_t open = js.find('{', pos);
                if (open == std::string::npos) break;
                size_t close = findMatchingBrace(js, open);
                if (close == std::string::npos) break;
                double v = 0, t = 0;
                std::string ts;
                if (objectNumber(js, open, close, "dst", v) &&
                    objectString(js, open, close, "time_tag", ts)) t = isoToEpoch(ts);
                if (t > 0.0 && v > -1000.0 && v < 1000.0) { rows.push_back({t, (float)v}); ok = true; }
                pos = close + 1;
            }
        }
        if (ok) {
            auto ds = downsample(rows, 72);
            dstHistory_ = ds;
            snapshot_.dst = ds.back().value;
            realFields++;
        } else {
            fillMockDst(utc);
            failed += "Dst ";
        }
    }

    // 3) F10.7 + Ap (45-day forecast: data[] con metric ap/f107; dia 0 = hoy).
    {
        std::string js;
        std::vector<IndexHistoryPoint> f107rows, apRows;
        bool okF = false, okA = false;
        if (fetchURL(f107ApUrl(), js)) {
            size_t dataPos = js.find("\"data\"");
            size_t pos = (dataPos == std::string::npos) ? 0 : dataPos;
            while (true) {
                size_t open = js.find('{', pos);
                if (open == std::string::npos) break;
                size_t close = findMatchingBrace(js, open);
                if (close == std::string::npos) break;
                std::string metric, ts;
                double v = 0;
                if (objectString(js, open, close, "metric", metric) &&
                    objectString(js, open, close, "time", ts) &&
                    objectNumber(js, open, close, "value", v)) {
                    double t = isoToEpoch(ts);
                    if (t > 0.0 && v >= 0.0) {
                        if (metric == "f107" && v < 500.0) { f107rows.push_back({t, (float)v}); okF = true; }
                        else if (metric == "ap" && v < 500.0) { apRows.push_back({t, (float)v}); okA = true; }
                    }
                }
                pos = close + 1;
            }
        }
        if (okF) {
            auto ds = downsample(f107rows, 45);
            f107History_ = ds;
            snapshot_.f107 = ds.front().value;  // dia 0 = valor actual
            realFields++;
        } else {
            fillMockF107(utc);  // f107 + historial mock (ap se decide abajo)
            failed += "F10.7 ";
        }
        if (okA) {
            auto ds = downsample(apRows, 45);
            snapshot_.ap = ds.front().value;  // dia 0 = valor actual
            realFields++;
        } else {
            if (okF) snapshot_.ap = 5.0f;  // fillMockF107 ya puso 5 si F10.7 fallo
            failed += "Ap ";
        }
    }

    // 4) SSN observado diario (ultimo + ultimos 30 dias).
    {
        std::string js;
        std::vector<IndexHistoryPoint> rows;
        bool ok = false;
        if (fetchURL(ssnUrl(), js)) {
            size_t pos = 0;
            while (true) {
                size_t open = js.find('{', pos);
                if (open == std::string::npos) break;
                size_t close = findMatchingBrace(js, open);
                if (close == std::string::npos) break;
                std::string ts;
                double v = 0;
                if (objectString(js, open, close, "Obsdate", ts) &&
                    objectNumber(js, open, close, "swpc_ssn", v)) {
                    double t = isoToEpoch(ts);
                    if (t > 0.0 && v >= 0.0 && v < 1000.0) { rows.push_back({t, (float)v}); ok = true; }
                }
                pos = close + 1;
            }
        }
        if (ok) {
            std::sort(rows.begin(), rows.end(),
                      [](const IndexHistoryPoint& a, const IndexHistoryPoint& b) { return a.timeUtc < b.timeUtc; });
            size_t keep = rows.size() > 30 ? rows.size() - 30 : 0;
            ssnHistory_.assign(rows.begin() + keep, rows.end());
            snapshot_.ssn = rows.back().value;
            realFields++;
        } else {
            fillMockSsn(utc);
            failed += "SSN ";
        }
    }

    // M11: manchas solares (best-effort, fuera del conteo realFields: en
    // fallo conserva las anteriores sin ensuciar el error de indices).
    fetchSunspots(utc);

    snapshot_.timeUtc = utc;
    snapshot_.valid = (realFields > 0);
    snapshot_.simulated = (realFields == 0);
    if (!failed.empty()) {
        if (!failed.empty() && failed.back() == ' ') failed.pop_back();
        snapshot_.error = failed + " (offline, mock)";
    } else {
        snapshot_.error.clear();
    }

    printf("[SolarIndices] Kp=%.2f Dst=%.0f F10.7=%.0f Ap=%.0f SSN=%.0f (%d/5 real)\n",
           snapshot_.kp, snapshot_.dst, snapshot_.f107, snapshot_.ap, snapshot_.ssn, realFields);
    return snapshot_.valid;
}

// M11: parseo puro del sunspot_report (filas live verificadas 2026-09-12:
// Region/lat/Location/Carlon/Numspot/time_tag; Region null -> skip).
bool SolarIndicesAdapter::parseSunspotReport(const std::string& js,
                                             std::vector<sundisk::Sunspot>& out) {
    out.clear();
    size_t pos = 0;
    bool any = false;
    while (true) {
        size_t open = js.find('{', pos);
        if (open == std::string::npos) break;
        size_t close = findMatchingBrace(js, open);
        if (close == std::string::npos) break;
        double region = 0, lat = 0, t = 0;
        std::string ts, loc;
        bool hasRegion = objectNumber(js, open, close, "Region", region);
        bool hasLat = objectNumber(js, open, close, "Latitude", lat);
        bool hasTime = objectString(js, open, close, "time_tag", ts);
        if (hasTime) t = isoToEpoch(ts);
        if (hasRegion && hasLat && t > 0.0 && region > 0.0 &&
            lat >= -90.0 && lat <= 90.0) {
            sundisk::Sunspot s;
            s.region = (int)region;
            s.lat = lat;
            s.timeUtc = t;
            double v = 0;
            if (objectNumber(js, open, close, "Carlon", v)) {
                s.lonCarr = v;
                s.hasLonCarr = true;
            }
            if (objectNumber(js, open, close, "Longitude", v)) {
                s.dlon = v;
                s.hasDlon = true;
            }
            if (objectString(js, open, close, "Location", loc)) s.loc = loc;
            if (objectNumber(js, open, close, "Numspot", v) && v >= 0.0)
                s.numspot = (int)v;
            out.push_back(s);
            any = true;
        }
        pos = close + 1;
    }
    return any;
}

bool SolarIndicesAdapter::fetchSunspots(double utc) {
    (void)utc;
    std::string js;
    if (!fetchURL(sunspotUrl(), js)) return false;
    std::vector<sundisk::Sunspot> rows;
    if (!parseSunspotReport(js, rows)) return false;
    sunspots_ = std::move(rows);
    lastSunspotUtc_ = utc;
    printf("[SolarIndices] sunspots: %d filas\n", (int)sunspots_.size());
    return true;
}
