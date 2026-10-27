#include "FlareAdapter.h"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <thread>
#include <utility>

// Endpoints DONKI FLR, en orden de intento (ver FlareAdapter.h).
#define FLR_URL_1 \
    "https://ccmc.gsfc.nasa.gov/DONKI-API/get/FLR?startDate=%s&endDate=%s"
#define FLR_URL_2 \
    "https://ccmc.gsfc.nasa.gov/DONKI/WS/get/FLR?startDate=%s&endDate=%s"
#define FLR_URL_3 \
    "https://kauai.ccmc.gsfc.nasa.gov/DONKI/WS/get/FLR?startDate=%s&endDate=%s"

namespace {

// Limites declarados en la cabecera.
constexpr double kWindowSec = 72.0 * 3600.0;      // ventana pedida al endpoint
constexpr double kCacheMaxAgeSec = 24.0 * 3600.0; // cache servible como mucho 24 h
constexpr size_t kEventCap = 50;                  // techo de eventos publicados
const char* const kCacheDir = "cache";
const char* const kCacheFile = "cache/flares.json";
const char* const kCacheTmp = "cache/flares.json.tmp";

// ---- Utilidades de fecha (misma aritmetica que CmeAdapter/EbroAdapter) ----

long long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

// "2026-10-26T08:21Z" / "...T08:21:00Z" / con fraccion u offset.
// Devuelve false si no es una fecha ISO reconocible.
bool parseIsoUtc(const std::string& iso, double& out) {
    auto dig = [&](size_t p, int n, int& v) {
        if (p + (size_t)n > iso.size()) return false;
        v = 0;
        for (int k = 0; k < n; k++) {
            char c = iso[p + (size_t)k];
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
        }
        return true;
    };
    if (iso.size() < 16) return false;
    int Y = 0, Mo = 0, D = 0, h = 0, mi = 0, se = 0;
    if (!dig(0, 4, Y)) return false;
    if (iso[4] != '-' || iso[7] != '-') return false;
    if (!dig(5, 2, Mo) || !dig(8, 2, D)) return false;
    if (iso[10] != 'T' && iso[10] != ' ') return false;
    if (!dig(11, 2, h) || iso[13] != ':' || !dig(14, 2, mi)) return false;
    size_t p = 16;
    if (p < iso.size() && iso[p] == ':') {
        if (!dig(p + 1, 2, se)) return false;
        p += 3;
    }
    double frac = 0.0;
    if (p < iso.size() && iso[p] == '.') {
        p++;
        double mul = 0.1;
        while (p < iso.size() && iso[p] >= '0' && iso[p] <= '9') {
            frac += (iso[p] - '0') * mul;
            mul *= 0.1;
            p++;
        }
    }
    int offMin = 0;
    if (p < iso.size() && (iso[p] == '+' || iso[p] == '-')) {
        bool neg = iso[p] == '-';
        int oh = 0, om = 0;
        p++;
        if (!dig(p, 2, oh)) return false;
        p += 2;
        if (p < iso.size() && iso[p] == ':') p++;
        if (p + 2 <= iso.size() && !dig(p, 2, om)) return false;
        offMin = (neg ? -1 : 1) * (oh * 60 + om);
    }
    if (Mo < 1 || Mo > 12 || D < 1 || D > 31 || h > 23 || mi > 59 || se > 60) return false;
    out = (double)daysFromCivil(Y, Mo, D) * 86400.0 +
          (double)(h * 3600 + mi * 60 + se) + frac - (double)offMin * 60.0;
    return true;
}

// Epoch -> "YYYY-MM-DD" (dia UTC) para startDate/endDate.
void ymdUtc(double utc, char out[11]) {
    time_t t = (time_t)utc;
    struct tm g;
#ifdef _WIN32
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    std::snprintf(out, 11, "%04d-%02d-%02d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday);
}

// ---- Escaner dirigido (sin parser general) ------------------------------

bool isWs(char c) { return std::isspace((unsigned char)c) != 0; }

// Localiza la PRIMERA aparicion de la clave como clave (precedida de '{' o
// ',') y deja valPos en el primer caracter de su valor.
bool findKey(const std::string& obj, const char* key, size_t& valPos) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t from = 0;
    while ((from = obj.find(pat, from)) != std::string::npos) {
        size_t q = from;
        while (q > 0 && isWs(obj[q - 1])) q--;
        bool keyCtx = (q == 0) || obj[q - 1] == '{' || obj[q - 1] == ',';
        size_t p = from + pat.size();
        while (p < obj.size() && isWs(obj[p])) p++;
        if (keyCtx && p < obj.size() && obj[p] == ':') {
            p++;
            while (p < obj.size() && isWs(obj[p])) p++;
            valPos = p;
            return true;
        }
        from += pat.size();
    }
    return false;
}

// Entero JSON (activeRegionNum). null / cadena / ausente -> false.
bool findInt(const std::string& obj, const char* key, int& out) {
    size_t p = 0;
    if (!findKey(obj, key, p) || p >= obj.size()) return false;
    char c = obj[p];
    if (c != '-' && c != '+' && !(c >= '0' && c <= '9')) return false;
    char* end = nullptr;
    long v = std::strtol(obj.c_str() + p, &end, 10);
    if (end == obj.c_str() + p) return false;
    out = (int)v;
    return true;
}

bool findString(const std::string& obj, const char* key, std::string& out) {
    size_t p = 0;
    if (!findKey(obj, key, p) || p >= obj.size() || obj[p] != '"') return false;
    std::string s;
    for (size_t i = p + 1; i < obj.size(); i++) {
        char c = obj[i];
        if (c == '\\' && i + 1 < obj.size()) {
            char e = obj[++i];
            if (e == 'u') {
                i += 4;                     // \uXXXX: los campos usados son ASCII
            } else if (e == 'n') {
                s += '\n';
            } else if (e == 't') {
                s += '\t';
            } else {
                s += e;
            }
        } else if (c == '"') {
            out = s;
            return true;
        } else {
            s += c;
        }
    }
    return false;
}

// Parte el array de nivel superior en subcadenas de objeto balanceadas.
void splitObjects(const std::string& s, size_t from, std::vector<std::string>& out) {
    int depth = 0;
    size_t start = 0;
    bool inStr = false, esc = false;
    for (size_t i = from; i < s.size(); i++) {
        char c = s[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') {
            if (depth == 0) start = i;
            depth++;
        } else if (c == '}' && depth > 0) {
            depth--;
            if (depth == 0) out.push_back(s.substr(start, i - start + 1));
        }
    }
}

// Un objeto FLR -> FlareEvent. Devuelve false si no hay ninguna hora legible
// (sin tiempo no hay edad ni orden: publicarlo seria inventar).
bool parseEvent(const std::string& obj, FlareEvent& ev) {
    findString(obj, "flrID", ev.id);
    findString(obj, "classType", ev.classType);
    findString(obj, "sourceLocation", ev.sourceLocation);
    findInt(obj, "activeRegionNum", ev.activeRegion);

    std::string iso;
    bool hasBegin = findString(obj, "beginTime", iso) && parseIsoUtc(iso, ev.beginUtc);
    if (!hasBegin) ev.beginUtc = 0.0;
    bool hasPeak = findString(obj, "peakTime", iso) && parseIsoUtc(iso, ev.peakUtc);
    if (!hasPeak) ev.peakUtc = 0.0;
    if (!findString(obj, "endTime", iso) || !parseIsoUtc(iso, ev.endUtc)) ev.endUtc = 0.0;

    ev.hasPeak = hasPeak;
    if (!hasPeak) ev.peakUtc = ev.beginUtc;   // sin peakTime se ordena por beginTime
    if (!hasBegin && !hasPeak) return false;

    float r = 0.0f;
    ev.rank = FlareAdapter::flrRank(ev.classType, r) ? r : 0.0f;
    return true;
}

// Tiempo efectivo de ordenacion (peakTime, o beginTime si falta).
double sortTime(const FlareEvent& ev) { return ev.hasPeak ? ev.peakUtc : ev.beginUtc; }

}  // namespace

void FlareAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::lock_guard<std::mutex> lk(mutex_);
    latest_ = FlareSnapshot{};
    error_.clear();
    printf("[OK] FlareAdapter initialized (DONKI FLR)\n");
}

bool FlareAdapter::flrRank(const std::string& classType, float& rank) {
    size_t b = 0, e = classType.size();
    while (b < e && isWs(classType[b])) b++;
    while (e > b && isWs(classType[e - 1])) e--;
    if (e - b < 2) return false;   // hace falta clase + numero ("M" no vale)

    float base = 0.0f;
    switch (classType[b]) {
        case 'C': base = 0.0f; break;
        case 'M': base = 10.0f; break;
        case 'X': base = 100.0f; break;
        default: return false;     // "Z9", o clase en minuscula: no se adivina
    }

    // Numero detras: digitos con un unico punto decimal y nada mas. Se parsea a
    // mano para no depender del locale (LC_NUMERIC con coma romperia strtod).
    bool digits = false, dot = false, frac = false;
    double whole = 0.0, part = 0.0, mul = 0.1;
    for (size_t i = b + 1; i < e; i++) {
        char c = classType[i];
        if (c >= '0' && c <= '9') {
            digits = true;
            if (frac) {
                part += (double)(c - '0') * mul;
                mul *= 0.1;
            } else {
                whole = whole * 10.0 + (double)(c - '0');
            }
            continue;
        }
        if (c == '.' && !dot && !frac) { dot = true; frac = true; continue; }
        return false;              // "M1.2x", "M1..2", espacio interior, etc.
    }
    if (!digits) return false;     // "M." / "X"
    rank = base + (float)(whole + part);
    return true;
}

bool FlareAdapter::parseFlares(const std::string& json, double nowUtc, FlareSnapshot& out) {
    out = FlareSnapshot{};
    out.timeUtc = nowUtc;

    // BOM defensivo.
    size_t b = 0;
    if (json.size() >= 3 && (unsigned char)json[0] == 0xEF && (unsigned char)json[1] == 0xBB &&
        (unsigned char)json[2] == 0xBF) {
        b = 3;
    }
    while (b < json.size() && isWs(json[b])) b++;
    if (b >= json.size() || json[b] != '[') {
        out.note = "JSON no reconocido";
        return false;
    }

    // El array de nivel superior tiene que cerrar: un cuerpo truncado no es
    // "cero eventos", es un error de red/parseo y se declara como tal.
    {
        int depth = 0;
        bool inStr = false, esc = false, closed = false;
        for (size_t i = b; i < json.size(); i++) {
            char c = json[i];
            if (inStr) {
                if (esc) esc = false;
                else if (c == '\\') esc = true;
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; continue; }
            if (c == '[') depth++;
            else if (c == ']') {
                depth--;
                if (depth == 0) { closed = true; break; }
            }
        }
        if (!closed) {
            out.note = "JSON truncado";
            return false;
        }
    }

    std::vector<std::string> objs;
    splitObjects(json, b + 1, objs);

    std::vector<FlareEvent> events;
    events.reserve(objs.size());
    for (const std::string& o : objs) {
        FlareEvent ev;
        if (parseEvent(o, ev)) events.push_back(ev);
    }

    // Mas reciente primero (stable, como el sort de JS).
    std::stable_sort(events.begin(), events.end(), [](const FlareEvent& a, const FlareEvent& b) {
        return sortTime(a) > sortTime(b);
    });
    if (events.size() > kEventCap) events.resize(kEventCap);

    out.events = std::move(events);
    out.valid = true;   // el array se parseo: una lista vacia NO es error
    if (out.events.empty()) {
        out.note = "sin fulguraciones";
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d flares", (int)out.events.size());
        out.note = buf;
    }
    return true;
}

size_t FlareAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool FlareAdapter::readCache(std::string& body, double& epoch) {
    FILE* fp = std::fopen(kCacheFile, "rb");
    if (!fp) return false;
    std::string all;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
    std::fclose(fp);

    // Cabecera "FLR1 <epoch>\n" + respuesta cruda.
    if (all.compare(0, 5, "FLR1 ") != 0) return false;
    size_t nl = all.find('\n');
    if (nl == std::string::npos) return false;
    const std::string num = all.substr(5, nl - 5);
    char* end = nullptr;
    double ep = std::strtod(num.c_str(), &end);
    if (end == num.c_str() || *end != '\0' || !std::isfinite(ep) || ep <= 0.0) return false;
    body = all.substr(nl + 1);
    if (body.empty()) return false;
    epoch = ep;
    return true;
}

void FlareAdapter::writeCache(double epoch, const std::string& body) {
    if (body.empty() || !(epoch > 0.0)) return;
    std::error_code ec;
    std::filesystem::create_directories(kCacheDir, ec);
    if (ec) return;
    // Temporal + rename: nunca dejar un cache a medias si se corta.
    FILE* fp = std::fopen(kCacheTmp, "wb");
    if (!fp) return;
    char head[48];
    int hn = std::snprintf(head, sizeof(head), "FLR1 %.0f\n", epoch);
    bool ok = hn > 0 && std::fwrite(head, 1, (size_t)hn, fp) == (size_t)hn;
    ok = ok && std::fwrite(body.data(), 1, body.size(), fp) == body.size();
    if (std::fclose(fp) != 0) ok = false;
    if (!ok) {
        std::filesystem::remove(kCacheTmp, ec);
        return;
    }
    std::filesystem::rename(kCacheTmp, kCacheFile, ec);
    if (ec) std::filesystem::remove(kCacheTmp, ec);
}

bool FlareAdapter::fetch(double utc) {
    char d1[11], d2[11];
    ymdUtc(utc - kWindowSec, d1);
    ymdUtc(utc, d2);

    const char* pats[3] = { FLR_URL_1, FLR_URL_2, FLR_URL_3 };
    std::string lastErr = "sin respuesta";

    for (const char* pat : pats) {
        char url[256];
        std::snprintf(url, sizeof(url), pat, d1, d2);

        CURL* curl = curl_easy_init();
        if (!curl) {
            lastErr = "curl: init";
            continue;
        }
        std::string body;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
        CURLcode res = curl_easy_perform(curl);
        long http = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            lastErr = std::string("http: ") + curl_easy_strerror(res);
            continue;
        }
        if (http != 200) {
            lastErr = "http " + std::to_string(http) + " " + url;
            continue;
        }

        FlareSnapshot snap;
        if (!parseFlares(body, utc, snap)) {
            lastErr = "parse: " + snap.note;
            continue;
        }

        writeCache(utc, body);   // best-effort, nunca fatal
        {
            std::lock_guard<std::mutex> lk(mutex_);
            latest_ = snap;
            lastFetchUtc_ = utc;
            error_.clear();
        }
        printf("[FLR] %s\n", snap.note.c_str());
        return true;
    }

    // Red caida: cache de disco declarada (hasta 24 h), y si ya habia datos
    // buenos NO se pisan (stale-while-revalidate).
    std::string cached;
    double cacheEpoch = 0.0;
    if (readCache(cached, cacheEpoch) && (utc - cacheEpoch) <= kCacheMaxAgeSec) {
        FlareSnapshot cs;
        if (parseFlares(cached, cacheEpoch, cs)) {
            cs.fromCache = true;
            cs.note += " (cache)";
            char age[32];
            std::snprintf(age, sizeof(age), "%.0f h", (utc - cacheEpoch) / 3600.0);
            std::lock_guard<std::mutex> lk(mutex_);
            if (!latest_.valid) latest_ = cs;   // si ya habia datos buenos, se conservan
            error_ = lastErr + " (cache " + age + ")";
            printf("[FLR] cache %s: %s\n", age, cs.note.c_str());
            return true;
        }
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!latest_.valid) {
            FlareSnapshot bad;
            bad.timeUtc = utc;
            bad.valid = false;
            bad.note = "error: " + lastErr;
            latest_ = bad;
        }
        error_ = lastErr;
    }
    fprintf(stderr, "[FLR] %s\n", lastErr.c_str());
    return false;
}

void FlareAdapter::fetchAsync(double utc) {
    if (fetching_.exchange(true)) return;   // ya en curso
    std::thread([this, utc]() {
        fetch(utc);
        fetching_.store(false);
    }).detach();
}

FlareSnapshot FlareAdapter::getSnapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return latest_;
}

std::string FlareAdapter::getErrorMessage() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return error_;
}
