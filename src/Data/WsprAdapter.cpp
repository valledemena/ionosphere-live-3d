#include "WsprAdapter.h"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#define WSPR_URL "http://wsprnet.org/olddb?mode=html&band=all"

namespace {

// Persistencia declarada (port de wspr.ts: SPOT_TTL_MIN / SPOT_CAP).
constexpr double kSpotTtlSec = 10.0 * 60.0;   // 10 min
constexpr size_t kSpotCap = 40;               // techo de spots/arcos
constexpr double kFutureSlackSec = 5.0 * 60.0;
constexpr size_t kCellCount = 14;

// Tabla de llamada WSPR en MHz (port literal de WSPR_DIAL en wspr.ts).
struct Dial {
    float mhz;
    const char* band;
};
const Dial kDial[] = {
    {0.4742f, "2200m"}, {1.8366f, "160m"}, {3.5686f, "80m"},
    {5.2872f, "60m"},   {7.0386f, "40m"},  {10.1387f, "30m"},
    {14.0956f, "20m"},  {18.1046f, "17m"}, {21.0946f, "15m"},
    {24.9246f, "12m"},  {28.1246f, "10m"}, {50.293f, "6m"},
    {70.091f, "4m"},    {144.489f, "2m"},
};

char lowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Busqueda sin distinguir mayusculas. `needle` debe venir en minusculas.
size_t findCI(const std::string& hay, const char* needle, size_t from) {
    const size_t n = strlen(needle);
    if (n == 0 || hay.size() < n) return std::string::npos;
    for (size_t i = from; i + n <= hay.size(); ++i) {
        size_t k = 0;
        while (k < n && lowerAscii(hay[i + k]) == needle[k]) ++k;
        if (k == n) return i;
    }
    return std::string::npos;
}

// Entidades HTML de la tabla: &nbsp; es la unica habitual, el resto por
// completitud. Se sustituyen por su caracter; el colapso de espacios va
// despues.
void decodeEntities(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] != '&') { out.push_back(in[i]); continue; }
        size_t semi = in.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 7) { out.push_back('&'); continue; }
        std::string ent = in.substr(i + 1, semi - i - 1);
        if (ent == "nbsp") out.push_back(' ');
        else if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos" || ent == "#39") out.push_back('\'');
        else { out.append(in, i, semi - i + 1); }
        i = semi;
    }
}

// Quita tags, decodifica entidades, colapsa espacios y recorta.
std::string stripTags(const std::string& s) {
    std::string noTags;
    noTags.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '<') {
            size_t gt = s.find('>', i + 1);
            if (gt == std::string::npos) break;
            i = gt;
        } else {
            noTags.push_back(s[i]);
        }
    }
    std::string decoded;
    decodeEntities(noTags, decoded);

    std::string out;
    out.reserve(decoded.size());
    bool pendingSpace = false;
    for (char c : decoded) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            pendingSpace = !out.empty();
        } else {
            if (pendingSpace) out.push_back(' ');
            pendingSpace = false;
            out.push_back(c);
        }
    }
    return out;
}

long long daysFromCivil(int y, int m, int d) {
    // Howard Hinnant: dias desde 1970-01-01 (valido para fechas modernas).
    y -= (m <= 2) ? 1 : 0;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

// "YYYY-MM-DD HH:MM" (UTC, sin segundos) -> epoch. false si no encaja.
bool parseDateUtc(const std::string& s, double& out) {
    if (s.size() < 16) return false;
    for (size_t i = 0; i < 16; i++) {
        bool sep = (i == 4 || i == 7 || i == 13) ? (s[i] == '-' || s[i] == ':') : false;
        if (i == 10) sep = (s[i] == ' ' || s[i] == 'T');
        bool digit = (s[i] >= '0' && s[i] <= '9');
        if (!sep && !digit) return false;
    }
    if (s[4] != '-' || s[7] != '-' || s[13] != ':') return false;
    if (s[10] != ' ' && s[10] != 'T') return false;
    int y = atoi(s.substr(0, 4).c_str());
    int mo = atoi(s.substr(5, 2).c_str());
    int d = atoi(s.substr(8, 2).c_str());
    int h = atoi(s.substr(11, 2).c_str());
    int mi = atoi(s.substr(14, 2).c_str());
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59) return false;
    out = (double)daysFromCivil(y, mo, d) * 86400.0 + (double)(h * 3600 + mi * 60);
    return true;
}

float parseFloatOr(const std::string& s, float fallback) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    float v = strtof(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(v)) return fallback;
    return v;
}

}  // namespace

void WsprAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::lock_guard<std::mutex> lk(mutex_);
    latest_ = WsprSnapshot{};
    latest_.note = "sin spots";
    printf("[OK] WsprAdapter initialized (HTML WSPRnet olddb)\n");
}

size_t WsprAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool WsprAdapter::fetchPage(std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::lock_guard<std::mutex> lk(mutex_);
        error_ = "curl: init failed";
        return false;
    }
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, WSPR_URL);
    // wsprnet.org responde 302 -> www.wsprnet.org (mismo path, verificado).
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    // El error se escribe bajo mutex (el hilo principal lo lee via
    // getErrorMessage); el formateo va fuera para no retener el lock.
    std::string err;
    if (res != CURLE_OK) {
        err = std::string("http: ") + curl_easy_strerror(res);
    } else if (code != 200) {
        err = "http: status " + std::to_string((int)code);
    } else if (response.empty()) {
        err = "http: empty body";
    }
    if (!err.empty()) {
        std::lock_guard<std::mutex> lk(mutex_);
        error_ = err;
        return false;
    }
    return true;
}

bool WsprAdapter::fetch(double utc) {
    std::string body;
    if (!fetchPage(body)) {
        // Sin red: el snapshot NO se toca (jamas datos sinteticos). Si aun no
        // hubo exito sigue invalido con el texto de error.
        std::string e;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            e = error_;
        }
        fprintf(stderr, "[Wspr] %s\n", e.c_str());
        return false;
    }

    WsprSnapshot snap;
    bool ok = parseTable(body, utc, snap);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        latest_ = snap;          // aunque este vacio: "sin spots" es honesto
        lastFetchUtc_ = utc;
        error_.clear();
    }
    if (!ok) {
        fprintf(stderr, "[Wspr] respuesta sin spots validos\n");
        return false;
    }
    printf("[Wspr] %s\n", snap.note.c_str());
    return true;
}

void WsprAdapter::fetchAsync(double utc) {
    if (fetching_.exchange(true)) return;   // ya en curso
    std::thread([this, utc]() {
        fetch(utc);
        fetching_.store(false);
    }).detach();
}

WsprSnapshot WsprAdapter::getSnapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return latest_;
}

std::string WsprAdapter::getErrorMessage() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return error_;
}

bool WsprAdapter::parseTable(const std::string& html, double nowUtc, WsprSnapshot& out) {
    out = WsprSnapshot{};
    out.timeUtc = nowUtc;

    size_t pos = 0;
    while (out.spots.size() < kSpotCap) {
        size_t rowStart = findCI(html, "<tr", pos);
        if (rowStart == std::string::npos) break;
        size_t rowEnd = findCI(html, "</tr>", rowStart);
        std::string row = (rowEnd == std::string::npos)
                              ? html.substr(rowStart)
                              : html.substr(rowStart, rowEnd - rowStart);
        pos = (rowEnd == std::string::npos) ? html.size() : rowEnd + 5;

        // Celdas <td>/<th>: maquina de estados minima (apertura, '>', cierre).
        std::vector<std::string> cells;
        size_t cur = 0;
        while (cells.size() < kCellCount) {
            size_t td = findCI(row, "<td", cur);
            size_t th = findCI(row, "<th", cur);
            size_t open = std::min(td, th);
            if (open == std::string::npos) break;
            size_t gt = row.find('>', open + 3);
            if (gt == std::string::npos) break;
            size_t close = findCI(row, "</t", gt + 1);
            if (close == std::string::npos) break;
            cells.push_back(stripTags(row.substr(gt + 1, close - gt - 1)));
            cur = close + 3;
        }
        // Cabecera (<th>) u otras tablas: sin 14 celdas no es un spot.
        if (cells.size() < kCellCount) continue;

        double t = 0.0;
        if (!parseDateUtc(cells[0], t)) continue;          // fecha ilegible
        if (nowUtc - t > kSpotTtlSec) continue;            // caducado (TTL 10 min)
        if (t > nowUtc + kFutureSlackSec) continue;        // futuro imposible

        float freq = parseFloatOr(cells[2], 0.0f);
        std::string band = bandForFreq(freq);
        if (band.empty()) continue;                        // fuera de banda

        float txLat = 0.0f, txLon = 0.0f, rxLat = 0.0f, rxLon = 0.0f;
        if (!maidenheadToLatLon(cells[5], txLat, txLon)) continue;   // grid TX
        if (!maidenheadToLatLon(cells[9], rxLat, rxLon)) continue;   // grid RX

        WsprSpot s;
        s.tx = cells[1];
        s.rx = cells[8];
        s.band = band;
        s.freqMHz = freq;
        s.snrDb = parseFloatOr(cells[3], 0.0f);
        s.timeUtc = t;
        s.txLat = txLat;
        s.txLon = txLon;
        s.rxLat = rxLat;
        s.rxLon = rxLon;
        s.km = parseFloatOr(cells[10], 0.0f);
        out.spots.push_back(s);
    }

    out.valid = !out.spots.empty();
    char buf[32];
    if (out.valid) snprintf(buf, sizeof(buf), "%d spots", (int)out.spots.size());
    else snprintf(buf, sizeof(buf), "sin spots");
    out.note = buf;
    return out.valid;
}

bool WsprAdapter::maidenheadToLatLon(const std::string& grid, float& lat, float& lon) {
    std::string g;
    for (char c : grid) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        g.push_back((char)toupper((unsigned char)c));
    }
    if (g.size() != 4 && g.size() != 6) return false;
    if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R') return false;
    if (g[2] < '0' || g[2] > '9' || g[3] < '0' || g[3] > '9') return false;
    if (g.size() == 6) {
        if (g[4] < 'A' || g[4] > 'X' || g[5] < 'A' || g[5] > 'X') return false;
    }
    float lonB = (float)(g[0] - 'A') * 20.0f - 180.0f + (float)(g[2] - '0') * 2.0f;
    float latB = (float)(g[1] - 'A') * 10.0f - 90.0f + (float)(g[3] - '0') * 1.0f;
    if (g.size() == 6) {
        // Subcuadricula 24x24: 5' lon x 2.5' lat, centro de la subcelda.
        lat = latB + ((float)(g[5] - 'A') + 0.5f) / 24.0f;
        lon = lonB + ((float)(g[4] - 'A') + 0.5f) / 12.0f;
    } else {
        lat = latB + 0.5f;
        lon = lonB + 1.0f;
    }
    return true;
}

std::string WsprAdapter::bandForFreq(float mhz) {
    if (!(mhz > 0.0f)) return "";
    for (const Dial& d : kDial) {
        float tol = (d.mhz < 30.0f) ? 0.15f : 1.0f;
        if (fabsf(mhz - d.mhz) <= tol) return d.band;
    }
    return "";
}
