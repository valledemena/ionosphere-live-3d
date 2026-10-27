#include "RbeAdapter.h"
#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <thread>
#include <utility>

// Endpoints DONKI RBE, en orden de intento (ver RbeAdapter.h). El primero es
// el verificado vivo el 2026-10-27; los otros dos son espejos del mismo
// servicio que ya usa FlareAdapter para el mismo host.
#define RBE_URL_1 \
    "https://ccmc.gsfc.nasa.gov/DONKI-API/get/RBE?startDate=%s&endDate=%s"
#define RBE_URL_2 \
    "https://ccmc.gsfc.nasa.gov/DONKI/WS/get/RBE?startDate=%s&endDate=%s"
#define RBE_URL_3 \
    "https://kauai.ccmc.gsfc.nasa.gov/DONKI/WS/get/RBE?startDate=%s&endDate=%s"

namespace {

// Limites declarados en la cabecera.
constexpr double kWindowDays = 58.0;               // DONKI corta en 60 dias
constexpr double kCacheMaxAgeSec = 24.0 * 3600.0;  // cache servible como mucho 24 h
constexpr size_t kEventCap = 8;                    // tope de eventos publicados
const char* const kCacheDir = "cache";
const char* const kCacheFile = "cache/rbe.json";
const char* const kCacheTmp = "cache/rbe.json.tmp";

// ---- Utilidades de fecha (misma aritmetica que CmeAdapter/FlareAdapter) ----

long long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

void civilFromDays(long long z, int& y, int& m, int& d) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = yy + (m <= 2 ? 1 : 0);
}

// Epoch -> "YYYY-MM-DD" (UTC). 11 bytes con el nulo.
void ymdUtc(double utc, char out[11]) {
    long long secs = (long long)std::floor(utc);
    long long days = secs / 86400;
    if (secs % 86400 != 0 && secs < 0) days--;   // division truncada hacia cero
    int y = 0, m = 0, d = 0;
    civilFromDays(days, y, m, d);
    std::snprintf(out, 11, "%04d-%02d-%02d", y, m, d);
}

// "2026-10-22T20:20Z" / "...T20:20:45Z" / con fraccion u offset.
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

// ---- Escaner dirigido (sin parser general, igual que CmeAdapter) ----------

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

bool findString(const std::string& obj, const char* key, std::string& out) {
    size_t p = 0;
    if (!findKey(obj, key, p) || p >= obj.size() || obj[p] != '"') return false;
    std::string s;
    for (size_t i = p + 1; i < obj.size(); i++) {
        char c = obj[i];
        if (c == '\\' && i + 1 < obj.size()) {
            char e = obj[++i];
            if (e == 'u') {
                i += 4;   // \uXXXX: los campos usados son ASCII
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

// Parte un array de objetos en subcadenas de objeto balanceadas.
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

// Extrae el contenido de un array que empieza en pos ('[') hasta su ']'.
bool arrayInner(const std::string& s, size_t pos, std::string& inner) {
    if (pos >= s.size() || s[pos] != '[') return false;
    int depth = 0;
    bool inStr = false, esc = false;
    for (size_t i = pos; i < s.size(); i++) {
        char c = s[i];
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
            if (depth == 0) { inner = s.substr(pos + 1, i - pos - 1); return true; }
        }
    }
    return false;
}

// Indice del ']' que cierra el array abierto en `open`; false si no cierra
// (cuerpo truncado por red o por el servidor).
bool arrayClose(const std::string& s, size_t open, size_t& close) {
    int depth = 0;
    bool inStr = false, esc = false;
    for (size_t i = open; i < s.size(); i++) {
        char c = s[i];
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
            if (depth == 0) { close = i; return true; }
        }
    }
    return false;
}

}  // namespace

void RbeAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::lock_guard<std::mutex> lk(mutex_);
    latest_ = RbeSnapshot{};
    error_.clear();
    printf("[OK] RbeAdapter initialized (DONKI RBE)\n");
}

size_t RbeAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool RbeAdapter::parseRbe(const std::string& json, double nowUtc, RbeSnapshot& out) {
    out = RbeSnapshot{};
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
    size_t close = 0;
    if (!arrayClose(json, b, close)) {
        out.note = "JSON truncado";
        return false;
    }

    bool hasContent = false;
    for (size_t i = b + 1; i < close; i++) {
        if (!isWs(json[i])) { hasContent = true; break; }
    }

    std::vector<std::string> objs;
    splitObjects(json, b + 1, objs);
    if (objs.empty() && hasContent) {
        // Hay algo entre corchetes que no es un objeto: no es "sin eventos".
        out.note = "JSON no reconocido";
        return false;
    }

    std::vector<RbeEvent> events;
    events.reserve(objs.size());
    for (const std::string& o : objs) {
        RbeEvent ev;
        std::string iso;
        // Sin eventTime legible no hay orden posible: se descarta el objeto.
        if (!findString(o, "eventTime", iso) || !parseIsoUtc(iso, ev.timeUtc)) continue;
        ev.eventTimeIso = iso;
        findString(o, "rbeID", ev.id);
        findString(o, "link", ev.link);

        // instruments[0].displayName (puede faltar o ser null -> "").
        size_t p = 0;
        std::string inner;
        if (findKey(o, "instruments", p) && arrayInner(o, p, inner)) {
            std::vector<std::string> items;
            splitObjects(inner, 0, items);
            if (!items.empty()) findString(items[0], "displayName", ev.instrument);
        }
        events.push_back(std::move(ev));
    }

    // Mas reciente primero (stable, como el sort de JS).
    std::stable_sort(events.begin(), events.end(),
                     [](const RbeEvent& a, const RbeEvent& b) { return a.timeUtc > b.timeUtc; });
    if (events.size() > kEventCap) events.resize(kEventCap);

    out.events = std::move(events);
    out.valid = true;   // el array se parseo: una lista vacia NO es error
    if (out.events.empty()) {
        out.note = "no RBE events in 58 days";
    } else {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%d RBE events", (int)out.events.size());
        out.note = buf;
    }
    return true;
}

bool RbeAdapter::readCache(std::string& body, double& epoch) {
    FILE* fp = std::fopen(kCacheFile, "rb");
    if (!fp) return false;
    std::string all;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
    std::fclose(fp);

    // Cabecera "RBE1 <epoch>\n" + respuesta cruda.
    if (all.compare(0, 5, "RBE1 ") != 0) return false;
    const size_t nl = all.find('\n');
    if (nl == std::string::npos) return false;
    const std::string num = all.substr(5, nl - 5);
    char* end = nullptr;
    const double ep = std::strtod(num.c_str(), &end);
    if (end == num.c_str() || *end != '\0' || !std::isfinite(ep) || ep <= 0.0) return false;
    body = all.substr(nl + 1);
    if (body.empty()) return false;
    epoch = ep;
    return true;
}

void RbeAdapter::writeCache(double epoch, const std::string& body) {
    if (body.empty() || !(epoch > 0.0)) return;
    std::error_code ec;
    std::filesystem::create_directories(kCacheDir, ec);
    if (ec) return;
    // Temporal + rename: nunca dejar un cache a medias si se corta.
    FILE* fp = std::fopen(kCacheTmp, "wb");
    if (!fp) return;
    char head[48];
    const int hn = std::snprintf(head, sizeof(head), "RBE1 %.0f\n", epoch);
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

bool RbeAdapter::fetch(double utc) {
    char d1[11], d2[11];
    ymdUtc(utc - kWindowDays * 86400.0, d1);
    ymdUtc(utc, d2);

    const char* pats[3] = { RBE_URL_1, RBE_URL_2, RBE_URL_3 };
    std::string lastErr = "no response";

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
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
        const CURLcode res = curl_easy_perform(curl);
        long http = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            lastErr = std::string("http: ") + curl_easy_strerror(res);
            continue;
        }
        if (http != 200) {
            lastErr = "http " + std::to_string(http);
            continue;
        }

        RbeSnapshot snap;
        if (!parseRbe(body, utc, snap)) {
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
        printf("[RBE] %s (window %s..%s, %zu bytes)\n", snap.note.c_str(), d1, d2, body.size());
        return true;
    }

    // Red caida: cache de disco declarada (hasta 24 h). Si ya habia datos
    // buenos se conservan (stale-while-revalidate).
    std::string cached;
    double cacheEpoch = 0.0;
    if (readCache(cached, cacheEpoch) && (utc - cacheEpoch) <= kCacheMaxAgeSec &&
        (utc - cacheEpoch) >= 0.0) {
        RbeSnapshot cs;
        if (parseRbe(cached, cacheEpoch, cs)) {
            cs.fromCache = true;
            char age[32];
            std::snprintf(age, sizeof(age), "%.0f h", (utc - cacheEpoch) / 3600.0);
            std::lock_guard<std::mutex> lk(mutex_);
            if (!latest_.valid) latest_ = cs;   // si ya habia datos buenos, se conservan
            error_ = lastErr + " (cache " + age + ")";
            printf("[RBE] cache %s: %s\n", age, cs.note.c_str());
            return true;
        }
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!latest_.valid) {
            RbeSnapshot bad;
            bad.timeUtc = utc;
            bad.valid = false;
            bad.note = "error: " + lastErr;
            latest_ = bad;
        }
        error_ = lastErr;
    }
    fprintf(stderr, "[RBE] %s\n", lastErr.c_str());
    return false;
}

void RbeAdapter::fetchAsync(double utc) {
    if (fetching_.exchange(true)) return;   // ya en curso
    std::thread([this, utc]() {
        fetch(utc);
        fetching_.store(false);
    }).detach();
}

RbeSnapshot RbeAdapter::getSnapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return latest_;
}

std::string RbeAdapter::getErrorMessage() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return error_;
}
