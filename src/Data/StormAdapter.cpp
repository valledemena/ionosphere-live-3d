#include "StormAdapter.h"
#include <curl/curl.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

#define STORM_ALERTS_URL \
    "https://api.weather.gov/alerts/active?event=Severe%20Thunderstorm%20Warning"
#define STORM_USER_AGENT "opencode-ionosphere-live-3d/1.0 (contact: local dev)"

namespace {

// Recorte declarado: la capa WhistlerLayer dibuja como maximo 24 celdas, asi
// que el parser no guarda mas (evita snapshots enormes en avisos masivos).
constexpr size_t kMaxCells = 24;

void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

// Indice de la '}' que cierra la '{' en 'open' (respeta cadenas y escapes).
// Devuelve std::string::npos si el objeto no esta cerrado.
size_t matchBrace(const std::string& s, size_t open) {
    if (open >= s.size() || s[open] != '{') return std::string::npos;
    int depth = 0;
    for (size_t i = open; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"') {
            ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\') ++i;
                ++i;
            }
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (--depth == 0) return i;
        }
    }
    return std::string::npos;
}

// Indice del VALOR de la clave 'key', saltando cadenas para no confundir el
// texto libre de los avisos con un nombre de campo. npos si no existe.
size_t findKeyValue(const std::string& s, size_t from, const std::string& key) {
    size_t i = from;
    while (i < s.size()) {
        if (s[i] != '"') { ++i; continue; }
        std::string tok;
        size_t j = i + 1;
        while (j < s.size() && s[j] != '"') {
            if (s[j] == '\\' && j + 1 < s.size()) tok.push_back(s[j + 1]);
            else tok.push_back(s[j]);
            ++j;
        }
        if (j >= s.size()) return std::string::npos;
        i = j + 1;
        if (tok != key) continue;
        size_t c = i;
        while (c < s.size() && (s[c] == ' ' || s[c] == '\t' || s[c] == '\n' || s[c] == '\r')) ++c;
        if (c >= s.size() || s[c] != ':') continue;   // era un valor, no una clave
        ++c;
        while (c < s.size() && (s[c] == ' ' || s[c] == '\t' || s[c] == '\n' || s[c] == '\r')) ++c;
        return (c < s.size()) ? c : std::string::npos;
    }
    return std::string::npos;
}

// Cadena JSON en 'idx' (indice del valor). Desescapa lo que NWS usa.
bool readString(const std::string& s, size_t idx, std::string& out) {
    out.clear();
    if (idx >= s.size() || s[idx] != '"') return false;
    size_t i = idx + 1;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char e = s[i + 1];
            switch (e) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'u':                       // \uXXXX: no lo usa NWS, se omite
                    if (i + 6 <= s.size()) i += 4;
                    break;
                default: out.push_back(e); break;
            }
            i += 2;
            continue;
        }
        out.push_back(s[i]);
        ++i;
    }
    return i < s.size();
}

bool readNumber(const std::string& s, size_t& i, double& out) {
    skipWs(s, i);
    const size_t start = i;
    while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' ||
                            s[i] == '.' || s[i] == 'e' || s[i] == 'E')) ++i;
    if (i == start) return false;
    out = atof(s.substr(start, i - start).c_str());
    return true;
}

// Dias desde 1970-01-01 (Howard Hinnant), mismo algoritmo que Ebro/Aurora.
long long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

// ISO-8601 -> epoch UTC (segundos). Acepta sufijo 'Z' y desfases +-HH:MM
// (tambien +-HHMM o +-HH). NWS entrega p.ej. "2026-10-26T14:30:00-05:00":
// la hora es LOCAL al desfase, y se convierte a UTC restando el desfase.
bool isoToEpoch(const std::string& s, double& out) {
    if (s.size() < 19) return false;
    for (size_t k = 0; k < 10; ++k) {
        if (k == 4 || k == 7) {
            if (s[k] != '-') return false;
        } else if (!isdigit((unsigned char)s[k])) {
            return false;
        }
    }
    if (!isdigit((unsigned char)s[11]) || !isdigit((unsigned char)s[12])) return false;
    if (!isdigit((unsigned char)s[14]) || !isdigit((unsigned char)s[15])) return false;
    if (!isdigit((unsigned char)s[17]) || !isdigit((unsigned char)s[18])) return false;
    if (s[13] != ':' || s[16] != ':') return false;
    if (s[10] != 'T' && s[10] != 't' && s[10] != ' ') return false;

    const int Y  = atoi(s.substr(0, 4).c_str());
    const int Mo = atoi(s.substr(5, 2).c_str());
    const int D  = atoi(s.substr(8, 2).c_str());
    const int H  = atoi(s.substr(11, 2).c_str());
    const int Mi = atoi(s.substr(14, 2).c_str());
    const int S  = atoi(s.substr(17, 2).c_str());
    if (Mo < 1 || Mo > 12 || D < 1 || D > 31) return false;

    double t = (double)daysFromCivil(Y, Mo, D) * 86400.0 +
               (double)H * 3600.0 + (double)Mi * 60.0 + (double)S;

    size_t i = 19;
    if (i < s.size() && s[i] == '.') {          // fraccion de segundo
        ++i;
        double scale = 0.1;
        while (i < s.size() && isdigit((unsigned char)s[i])) {
            t += (double)(s[i] - '0') * scale;
            scale *= 0.1;
            ++i;
        }
    }
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        const bool west = (s[i] == '-');
        ++i;
        int oh = 0, om = 0;
        if (i + 2 <= s.size()) oh = atoi(s.substr(i, 2).c_str());
        i += 2;
        if (i < s.size() && s[i] == ':') ++i;
        if (i + 2 <= s.size()) om = atoi(s.substr(i, 2).c_str());
        const double off = (double)oh * 3600.0 + (double)om * 60.0;
        t -= west ? -off : off;                 // UTC = local - desfase
    }
    out = t;
    return true;
}

// Centroide declarado simple del anillo EXTERIOR de un Polygon: media de los
// pares [lon, lat] (polyCentroid de la web). Ignora la altitud opcional.
bool polygonCentroid(const std::string& feature, float& latOut, float& lonOut) {
    const size_t gIdx = findKeyValue(feature, 0, "geometry");
    if (gIdx == std::string::npos || feature[gIdx] != '{') return false;   // null o ausente
    const size_t gEnd = matchBrace(feature, gIdx);
    if (gEnd == std::string::npos) return false;
    const std::string geom = feature.substr(gIdx, gEnd - gIdx + 1);

    const size_t tIdx = findKeyValue(geom, 0, "type");
    std::string type;
    if (tIdx == std::string::npos || !readString(geom, tIdx, type)) return false;
    if (type != "Polygon") return false;       // MultiPolygon/Point fuera de alcance

    size_t i = findKeyValue(geom, 0, "coordinates");
    if (i == std::string::npos || i >= geom.size() || geom[i] != '[') return false;
    ++i;                                       // array de anillos
    skipWs(geom, i);
    if (i >= geom.size() || geom[i] != '[') return false;
    ++i;                                       // anillo exterior

    double sumLon = 0.0, sumLat = 0.0;
    size_t n = 0;
    while (i < geom.size()) {
        skipWs(geom, i);
        if (i >= geom.size()) break;
        if (geom[i] == ']') break;             // fin del anillo exterior
        if (geom[i] != '[') return false;
        ++i;
        double lon = 0.0, lat = 0.0;
        if (!readNumber(geom, i, lon)) return false;
        skipWs(geom, i);
        if (i >= geom.size() || geom[i] != ',') return false;
        ++i;                                   // separador lon / lat
        if (!readNumber(geom, i, lat)) return false;
        skipWs(geom, i);
        if (i < geom.size() && geom[i] == ',') {   // [lon,lat,alt] opcional
            ++i;
            double alt = 0.0;
            if (!readNumber(geom, i, alt)) return false;
            skipWs(geom, i);
        }
        if (i >= geom.size() || geom[i] != ']') return false;
        ++i;
        sumLon += lon;
        sumLat += lat;
        ++n;
        skipWs(geom, i);
        if (i < geom.size() && geom[i] == ',') { ++i; continue; }
        break;
    }
    if (n == 0) return false;                  // anillo vacio: fuera
    latOut = (float)(sumLat / (double)n);
    lonOut = (float)(sumLon / (double)n);
    return true;
}

std::string propString(const std::string& props, const char* key) {
    const size_t idx = findKeyValue(props, 0, key);
    std::string v;
    if (idx == std::string::npos || !readString(props, idx, v)) return std::string();
    return v;
}

bool propIso(const std::string& props, const char* key, double& out) {
    const size_t idx = findKeyValue(props, 0, key);
    if (idx == std::string::npos) return false;
    std::string v;
    if (!readString(props, idx, v) || v.empty()) return false;
    if (v == "null") return false;
    return isoToEpoch(v, out);
}

}  // namespace

void StormAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    snapshot_ = StormSnapshot{};
    printf("[OK] StormAdapter initialized (avisos NWS Severe Thunderstorm Warning)\n");
}

size_t StormAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    const size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool StormAdapter::fetchURL(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::lock_guard<std::mutex> lk(mutex_);
        error_ = "curl: init failed";
        return false;
    }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/geo+json");
    headers = curl_slist_append(headers, "Accept: application/json");
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, STORM_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    const CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    std::string err;
    if (res != CURLE_OK) {
        err = std::string("nws: ") + curl_easy_strerror(res);
    } else if (http != 200) {
        err = "nws: HTTP " + std::to_string(http);
    } else if (response.empty()) {
        err = "nws: respuesta vacia";
    }
    if (!err.empty()) {
        std::lock_guard<std::mutex> lk(mutex_);
        error_ = err;
        return false;
    }
    return true;
}

bool StormAdapter::parseAlerts(const std::string& json, double nowUtc, StormSnapshot& out) {
    out = StormSnapshot{};
    out.timeUtc = nowUtc;

    // Si NWS contesta un problem+json (o cualquier cosa que no sea
    // FeatureCollection) no se inventa nada: snapshot invalido.
    const size_t rtIdx = findKeyValue(json, 0, "type");
    std::string rootType;
    if (rtIdx == std::string::npos || !readString(json, rtIdx, rootType) ||
        rootType != "FeatureCollection") {
        out.note = "respuesta no FeatureCollection";
        return false;
    }

    const size_t fIdx = findKeyValue(json, 0, "features");
    if (fIdx == std::string::npos || fIdx >= json.size() || json[fIdx] != '[') {
        out.note = "sin features";
        return false;
    }

    size_t i = fIdx + 1;
    while (i < json.size() && out.cells.size() < kMaxCells) {
        skipWs(json, i);
        if (i >= json.size()) break;
        if (json[i] == ']') break;
        if (json[i] != '{') { ++i; continue; }
        const size_t fEnd = matchBrace(json, i);
        if (fEnd == std::string::npos) break;
        const std::string feature = json.substr(i, fEnd - i + 1);
        i = fEnd + 1;

        float lat = 0.0f, lon = 0.0f;
        if (!polygonCentroid(feature, lat, lon)) continue;

        // properties del MISMO feature: id/areaDesc/onset/ends.
        std::string props;
        const size_t pIdx = findKeyValue(feature, 0, "properties");
        if (pIdx != std::string::npos && pIdx < feature.size() && feature[pIdx] == '{') {
            const size_t pEnd = matchBrace(feature, pIdx);
            if (pEnd != std::string::npos) props = feature.substr(pIdx, pEnd - pIdx + 1);
        }

        StormCell cell;
        cell.lat = lat;
        cell.lon = lon;
        cell.id = propString(props, "id");
        cell.area = propString(props, "areaDesc");
        double onset = 0.0;
        if (propIso(props, "onset", onset)) cell.onsetUtc = onset;
        double ends = 0.0;
        if (propIso(props, "ends", ends)) {
            cell.endsUtc = ends;
            cell.hasEnds = true;
        }
        if (cell.hasEnds && cell.endsUtc < nowUtc) continue;   // caducado
        out.cells.push_back(cell);
    }

    out.valid = true;
    out.note = out.cells.empty() ? "sin avisos"
                                 : (std::to_string(out.cells.size()) + " avisos");
    return true;
}

bool StormAdapter::fetch(double utc) {
    std::string body;
    if (!fetchURL(STORM_ALERTS_URL, body)) {
        std::string e;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            e = error_;
        }
        fprintf(stderr, "[Storm] %s\n", e.c_str());
        return false;
    }
    StormSnapshot snap;
    if (!parseAlerts(body, utc, snap)) {
        const std::string e = "nws: " + snap.note;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            error_ = e;
        }
        fprintf(stderr, "[Storm] %s\n", e.c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(mutex_);
        snapshot_ = snap;
        lastFetchUtc_ = utc;
        error_.clear();
    }
    printf("[Storm] NWS OK: %s\n", snap.note.c_str());
    return true;
}

void StormAdapter::fetchAsync(double utc) {
    if (fetching_.exchange(true)) return;   // ya en curso
    std::thread([this, utc]() {
        fetch(utc);
        fetching_.store(false);
    }).detach();
}

bool StormAdapter::isFetching() const { return fetching_; }

StormSnapshot StormAdapter::getSnapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return snapshot_;
}

double StormAdapter::getLastFetchTime() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return lastFetchUtc_;
}

std::string StormAdapter::getErrorMessage() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return error_;
}
