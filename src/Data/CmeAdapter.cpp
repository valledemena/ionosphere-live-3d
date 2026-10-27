#include "CmeAdapter.h"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

// Dos espejos del mismo endpoint DONKI; se prueban en orden y se queda el
// primero que conteste 200 con un array JSON.
#define CME_URL_1 "https://ccmc.gsfc.nasa.gov/DONKI-API/get/WSAEnlilSimulations"
#define CME_URL_2 "https://kauai.ccmc.gsfc.nasa.gov/DONKI/WS/get/WSAEnlilSimulations"

namespace {

// ---- Utilidades de fecha (misma aritmetica que EbroAdapter) --------------

long long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

// "2026-10-14T11:30Z" / "2026-10-14T11:30:45Z" / con fraccion u offset.
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

bool findNumber(const std::string& obj, const char* key, double& out) {
    size_t p = 0;
    if (!findKey(obj, key, p) || p >= obj.size()) return false;
    char c = obj[p];
    if (c != '-' && c != '+' && !(c >= '0' && c <= '9')) return false;   // null/"..."
    char* end = nullptr;
    double v = std::strtod(obj.c_str() + p, &end);
    if (end == obj.c_str() + p || !std::isfinite(v)) return false;
    out = v;
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

bool findBool(const std::string& obj, const char* key, bool& out) {
    size_t p = 0;
    if (!findKey(obj, key, p) || p > obj.size()) return false;
    if (obj.compare(p, 4, "true") == 0) { out = true; return true; }
    if (obj.compare(p, 5, "false") == 0) { out = false; return true; }
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

struct CmeInput {
    std::string cmeId;
    double cmeStart = 0.0, t215 = 0.0;
    float speed = 0.0f, half = 0.0f, lat = 0.0f, lon = 0.0f;
    bool hasCmeStart = false, hasT215 = false;
    bool hasSpeed = false, hasHalf = false, hasLat = false, hasLon = false;
    bool accurate = false;
};

struct CmeSim {
    std::string id, link;
    double modelTime = 0.0, eta = 0.0;
    bool hasEta = false, earthGB = false, hasKp = false, hasDuration = false;
    double duration = 0.0;
    float kpMax = 0.0f;
    std::vector<CmeInput> inputs;
};

bool parseInput(const std::string& obj, CmeInput& in) {
    std::string iso;
    if (findString(obj, "cmeStartTime", iso)) in.hasCmeStart = parseIsoUtc(iso, in.cmeStart);
    if (findString(obj, "time21_5", iso)) in.hasT215 = parseIsoUtc(iso, in.t215);
    double v = 0.0;
    if (findNumber(obj, "speed", v)) { in.speed = (float)v; in.hasSpeed = true; }
    if (findNumber(obj, "halfAngle", v)) { in.half = (float)v; in.hasHalf = true; }
    if (findNumber(obj, "latitude", v)) { in.lat = (float)v; in.hasLat = true; }
    if (findNumber(obj, "longitude", v)) { in.lon = (float)v; in.hasLon = true; }
    findBool(obj, "isMostAccurate", in.accurate);
    findString(obj, "CMEID", in.cmeId);
    return true;
}

bool parseSim(const std::string& obj, CmeSim& sim) {
    findString(obj, "simulationID", sim.id);
    findString(obj, "link", sim.link);
    std::string iso;
    if (findString(obj, "modelCompletionTime", iso)) parseIsoUtc(iso, sim.modelTime);
    if (findString(obj, "estimatedShockArrivalTime", iso))
        sim.hasEta = parseIsoUtc(iso, sim.eta);
    double v = 0.0;
    if (findNumber(obj, "estimatedDuration", v)) { sim.duration = v; sim.hasDuration = true; }
    findBool(obj, "isEarthGB", sim.earthGB);
    const char* kpKeys[3] = { "kp_90", "kp_135", "kp_180" };
    for (const char* k : kpKeys) {
        if (findNumber(obj, k, v)) {
            if (!sim.hasKp || (float)v > sim.kpMax) sim.kpMax = (float)v;
            sim.hasKp = true;
        }
    }
    size_t p = 0;
    std::string inner;
    if (findKey(obj, "cmeInputs", p) && arrayInner(obj, p, inner)) {
        std::vector<std::string> items;
        splitObjects(inner, 0, items);
        for (const std::string& it : items) {
            CmeInput in;
            parseInput(it, in);
            sim.inputs.push_back(in);
        }
    }
    return true;
}

// Entrada cmeInputs mas exacta de una simulacion (o la primera si ninguna
// declara isMostAccurate). La usan el evento y el historico del panel.
const CmeInput* bestInputOf(const CmeSim& s) {
    for (const CmeInput& in : s.inputs) {
        if (in.accurate) return &in;
    }
    return s.inputs.empty() ? nullptr : &s.inputs.front();
}

}  // namespace

void CmeAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::lock_guard<std::mutex> lk(mutex_);
    latest_ = CmeSnapshot{};
    error_.clear();
    printf("[OK] CmeAdapter initialized (ENLIL WSA-Enlil)\n");
}

size_t CmeAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool CmeAdapter::parseEnlil(const std::string& json, double nowUtc, CmeSnapshot& out) {
    out = CmeSnapshot{};
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

    std::vector<std::string> objs;
    splitObjects(json, b + 1, objs);
    if (objs.empty()) {
        out.note = "sin simulaciones";
        return false;
    }

    std::vector<CmeSim> sims;
    sims.reserve(objs.size());
    for (const std::string& o : objs) {
        CmeSim s;
        parseSim(o, s);
        sims.push_back(s);
    }

    // Orden por modelCompletionTime descendente (stable, como el sort de JS).
    std::stable_sort(sims.begin(), sims.end(),
                     [](const CmeSim& a, const CmeSim& b) { return a.modelTime > b.modelTime; });

    const int count = (int)sims.size();

    // Historico compacto para el panel Geospace (mismo orden que el evento).
    out.sims.reserve(sims.size() < 32 ? sims.size() : 32);
    for (const CmeSim& s : sims) {
        const CmeInput* best = bestInputOf(s);
        CmeSimBrief b;
        b.simId = s.id;
        b.modelTimeUtc = s.modelTime;
        b.arrivalUtc = s.eta;
        b.hasEta = s.hasEta;
        b.earthGB = s.earthGB;
        b.kpMax = s.hasKp ? s.kpMax : 0.0f;
        if (best) {
            b.cmeId = best->cmeId;
            b.speedKms = best->hasSpeed ? best->speed : 0.0f;
            b.halfAngleDeg = best->hasHalf ? best->half : 0.0f;
            if (best->hasT215) {
                b.startUtc = best->t215;
                b.hasStart = true;
            } else if (best->hasCmeStart) {
                b.startUtc = best->cmeStart;
                b.hasStart = true;
            }
        }
        out.sims.push_back(b);
        if (out.sims.size() >= 32) break;
    }

    for (const CmeSim& s : sims) {
        if (!s.hasEta) continue;
        // Entrada mas exacta, o la primera si ninguna lo declara.
        const CmeInput* best = bestInputOf(s);

        CmeEvent ev;
        ev.simId = s.id;
        ev.link = s.link;
        ev.modelTimeUtc = s.modelTime;
        ev.arrivalUtc = s.eta;
        ev.hasEta = true;
        ev.durationH = s.hasDuration ? s.duration : 0.0;
        ev.earthGB = s.earthGB;
        ev.kpMax = s.hasKp ? s.kpMax : 0.0f;
        ev.simCount = count;
        if (best) {
            if (best->hasT215) {
                ev.startUtc = best->t215;
                ev.hasStart = true;
            } else if (best->hasCmeStart) {
                ev.startUtc = best->cmeStart;
                ev.hasStart = true;
            }
            ev.cmeId = best->cmeId;
            ev.speedKms = best->hasSpeed ? best->speed : 0.0f;
            if (best->hasHalf) {
                ev.halfAngleDeg = best->half;
                ev.hasHalfAngle = true;
            }
            ev.srcLat = best->hasLat ? best->lat : 0.0f;
            ev.srcLon = best->hasLon ? best->lon : 0.0f;
        }

        out.event = ev;
        out.valid = true;
        out.hasEvent = true;
        out.note = "ENLIL " + ev.simId;
        return true;
    }

    // "quiet": hay simulaciones pero ninguna con ETA publicada.
    out.event.modelTimeUtc = sims.front().modelTime;
    out.event.simCount = count;
    out.valid = true;
    out.hasEvent = false;
    out.note = "sin ETA";
    return true;
}

bool CmeAdapter::fetch(double utc) {
    const char* urls[2] = { CME_URL_1, CME_URL_2 };
    std::string lastErr = "sin respuesta";

    for (const char* url : urls) {
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

        CmeSnapshot snap;
        if (!parseEnlil(body, utc, snap)) {
            lastErr = "parse: " + snap.note;
            continue;
        }

        std::lock_guard<std::mutex> lk(mutex_);
        latest_ = snap;
        lastFetchUtc_ = utc;
        error_.clear();
        if (snap.hasEvent) {
            printf("[CME] %s ETA %.0f speed=%.0f half=%.0f kp=%.1f\n", snap.event.simId.c_str(),
                   snap.event.arrivalUtc, snap.event.speedKms, snap.event.halfAngleDeg,
                   snap.event.kpMax);
        } else {
            printf("[CME] %s (%d sim)\n", snap.note.c_str(), snap.event.simCount);
        }
        return true;
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        error_ = lastErr;
    }
    fprintf(stderr, "[CME] %s\n", lastErr.c_str());
    return false;
}

void CmeAdapter::fetchAsync(double utc) {
    if (fetching_.exchange(true)) return;   // ya en curso
    std::thread([this, utc]() {
        fetch(utc);
        fetching_.store(false);
    }).detach();
}

CmeSnapshot CmeAdapter::getSnapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return latest_;
}

std::string CmeAdapter::getErrorMessage() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return error_;
}

float CmeAdapter::cmeProgress(double startUtc, double arrivalUtc, double nowUtc) {
    bool hasStart = std::isfinite(startUtc) && startUtc > 0.0;
    if (!hasStart || !(arrivalUtc > startUtc)) return nowUtc >= arrivalUtc ? 1.0f : 0.0f;
    if (nowUtc <= startUtc) return 0.0f;
    if (nowUtc >= arrivalUtc) return 1.0f;
    float p = (float)((nowUtc - startUtc) / (arrivalUtc - startUtc));
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}

std::string CmeAdapter::cmeCountdown(double arrivalUtc, double nowUtc) {
    double d = arrivalUtc - nowUtc;
    if (fabs(d) < 60.0) return "now";
    bool past = d < 0.0;
    long long mins = (long long)floor(fabs(d) / 60.0);
    long long dd = mins / 1440;
    long long hh = (mins % 1440) / 60;
    long long mm = mins % 60;
    char buf[64];
    if (dd > 0) snprintf(buf, sizeof(buf), "%lld d %lld h", dd, hh);
    else if (hh > 0) snprintf(buf, sizeof(buf), "%lld h %lld min", hh, mm);
    else snprintf(buf, sizeof(buf), "%lld min", mm);
    return past ? std::string("arrived ") + buf + " ago" : std::string("in ") + buf;
}
