#include "Kc2gAdapter.h"
#include "Data/Kc2gCache.h"
#include "Utils/Paths.h"  // cache configurable (Paths.h)
#include <curl/curl.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <ctime>

#define KC2G_STATIONS_URL "https://prop.kc2g.com/api/stations.json"

// ---------------------------------------------------------------------------
// Nucleo de parseo JSON (puro, sin regex): escaneo caracter a caracter del
// schema plano de stations.json (array de objetos con un subobjeto
// "station"). Mismo estilo de robustez que el nucleo de GiroAdapter
// (tokenizador + rowTime): nada de bibliotecas, todo testeable con fixture.
// ---------------------------------------------------------------------------
namespace {

void skipWs(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
}

// String JSON con escapes basicos (\", \\, \/, \b, \f, \n, \r, \t, \uXXXX
// -> UTF-8 BMP; los nombres de estacion son ASCII en la practica).
bool parseString(const char*& p, const char* end, std::string& out) {
    if (p >= end || *p != '"') return false;
    p++;
    out.clear();
    while (p < end) {
        char c = *p;
        if (c == '"') { p++; return true; }
        if (c == '\\') {
            p++;
            if (p >= end) return false;
            char e = *p++;
            switch (e) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    if (end - p < 4) return false;
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = *p++;
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        else return false;
                    }
                    if (cp < 0x80) {
                        out.push_back((char)cp);
                    } else if (cp < 0x800) {
                        out.push_back((char)(0xC0 | (cp >> 6)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back((char)(0xE0 | (cp >> 12)));
                        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: return false;
            }
            continue;
        }
        out.push_back(c);
        p++;
    }
    return false;   // sin comilla de cierre
}

// Numero JSON (strtod acotado al token; rechaza colas no numericas).
bool parseNumber(const char*& p, const char* end, double& out) {
    if (p >= end) return false;
    if (!(*p == '-' || (*p >= '0' && *p <= '9'))) return false;
    const char* q = p;
    if (*q == '-') q++;
    while (q < end && ((*q >= '0' && *q <= '9') || *q == '.' ||
                       *q == 'e' || *q == 'E' || *q == '+' || *q == '-')) q++;
    if (q == p) return false;
    char buf[64];
    size_t len = (size_t)(q - p);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    std::memcpy(buf, p, len);
    buf[len] = '\0';
    char* stop = nullptr;
    out = std::strtod(buf, &stop);
    if (stop != buf + len) return false;
    p = q;
    return true;
}

// Salta (y valida sintacticamente) cualquier valor: string, numero, null,
// true/false, objeto o array anidado ( tec/yf1/yf2/hmf1/hme/fbes/scalef2/id
// y cualquier clave nueva que KC2G anada manana ).
bool skipValue(const char*& p, const char* end) {
    if (p >= end) return false;
    char c = *p;
    if (c == '"') { std::string tmp; return parseString(p, end, tmp); }
    if (c == '{' || c == '[') {
        const char close = (c == '{') ? '}' : ']';
        p++;
        skipWs(p, end);
        if (p < end && *p == close) { p++; return true; }
        for (;;) {
            skipWs(p, end);
            if (c == '{') {
                std::string k;
                if (p >= end || *p != '"' || !parseString(p, end, k)) return false;
                skipWs(p, end);
                if (p >= end || *p != ':') return false;
                p++;
                skipWs(p, end);
            }
            if (!skipValue(p, end)) return false;
            skipWs(p, end);
            if (p < end && *p == ',') { p++; continue; }
            if (p < end && *p == close) { p++; return true; }
            return false;
        }
    }
    if (c == '-' || (c >= '0' && c <= '9')) { double d; return parseNumber(p, end, d); }
    if (end - p >= 4 && std::memcmp(p, "null", 4) == 0) { p += 4; return true; }
    if (end - p >= 4 && std::memcmp(p, "true", 4) == 0) { p += 4; return true; }
    if (end - p >= 5 && std::memcmp(p, "false", 5) == 0) { p += 5; return true; }
    return false;
}

// "2026-09-09T16:45:01" -> epoch (mismo algoritmo daysFromCivil que
// GiroAdapter::rowTime; sufijo "Z" u offset final: ignorado — kc2g publica
// UTC). 0 si no es parseable.
double isoToEpoch(const std::string& t) {
    if (t.size() < 19) return 0.0;
    int Y  = std::atoi(t.substr(0, 4).c_str());
    int Mo = std::atoi(t.substr(5, 2).c_str());
    int D  = std::atoi(t.substr(8, 2).c_str());
    int H  = std::atoi(t.substr(11, 2).c_str());
    int Mi = std::atoi(t.substr(14, 2).c_str());
    int S  = std::atoi(t.substr(17, 2).c_str());
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

}   // namespace

// ---------------------------------------------------------------------------
// Estaticas puras (tests)
// ---------------------------------------------------------------------------
bool Kc2gAdapter::isGridBlockedCode(const std::string& code) {
    // M4R-A (sello FASE A): EG931-2024 — el feed NOAA de EG931 (Fairford)
    // lleva congelado desde 2024-08-22 (fixture 2026-09-09: ~2 anios de
    // edad con datos). Regla EXPLICITA: fuera de grids siempre, aunque la
    // fila llegase fresca; el resto de ventanas decide el time-filter.
    return code == "EG931";
}

bool Kc2gAdapter::rowHasData(const Kc2gRow& r) {
    return r.fof2 > 0.0f || r.mufd > 0.0f || r.hmf2 > 0.0f;
}

double Kc2gAdapter::rowAgeSec(const Kc2gRow& r, double nowUtc) {
    if (r.timeUtc <= 0.0) return 1.0e18;   // ISO no parseable = vieja
    return nowUtc - r.timeUtc;
}

bool Kc2gAdapter::gridUsable(const Kc2gRow& r, double nowUtc) {
    return rowHasData(r) && !isGridBlockedCode(r.code) &&
           rowAgeSec(r, nowUtc) <= kGridMaxAgeSec;
}

bool Kc2gAdapter::panelUsable(const Kc2gRow& r, double nowUtc) {
    return rowHasData(r) && rowAgeSec(r, nowUtc) <= kPanelMaxAgeSec;
}

StationSample Kc2gAdapter::toSample(const Kc2gRow& r, double nowUtc) {
    StationSample s;
    s.code = r.code;
    s.name = r.name;
    s.latDeg = (float)r.latDeg;
    s.lonDeg = (float)r.lonDeg;
    s.foF2 = r.fof2;
    s.muf  = r.mufd;    // kc2g "mufd" = MUF(D), D=3000 km
    s.hmF2 = r.hmf2;
    s.foF1 = r.fof1;
    s.foE  = r.foe;
    s.foEs = r.foes;
    s.hF2  = r.hf2;     // kc2g "hf2" = h'F2 (altura virtual F2)
    s.hE   = r.he;      // kc2g "he"  = h'E  (altura virtual E)
    s.cs   = (int)(r.cs + 0.5f);
    s.nmF2 = (r.fof2 > 0.0f)
        ? (float)(1.24e10 * (double)r.fof2 * (double)r.fof2) : 0.0f;
    s.timeUtc = r.timeUtc;
    // B0/B1/fmin/fxI/hF/hEs/hF2-virtual-extra: kc2g NO los trae -> 0;
    // sanitizeB0/B1 (LayerProfile.h) sanean a 100/3 por defecto (sello
    // FASE A: verificado — funciona sin codigo extra).
    s.valid = panelUsable(r, nowUtc);
    s.stale = !gridUsable(r, nowUtc);   // >6 h o EG931-2024: panel si, grids no
    if (s.valid) {
        // Historia de 1 punto: sampleHistoryAt clampa al ultimo valor para
        // cualquier epoch (replay congelado pero honesto, marcado stale).
        GiroHistoryPoint p;
        p.timeUtc = r.timeUtc;
        p.foF2 = r.fof2;
        p.foF1 = r.fof1;
        p.muf  = r.mufd;
        p.hmF2 = r.hmf2;
        p.foE  = r.foe;
        p.foEs = r.foes;
        p.fxI  = 0.0f;   // ausente en kc2g (consistente con s.fxI = 0)
        p.B0   = 0.0f;
        p.B1   = 0.0f;
        p.fmin = 0.0f;
        s.history.push_back(p);
    }
    return s;
}

// FASE B (P1): acumulacion por batch en kccHist_. Epoch del punto =
// timeUtc DE LA FILA (paridad GIRO); mismo epoch reemplaza (dedup);
// EG931 jamas; solo ventana panel (<=24 h) al fetch.
void Kc2gAdapter::accumulateBatch(const std::vector<Kc2gRow>& rows, double nowUtc) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& r : rows) {
        if (r.code.empty() || isGridBlockedCode(r.code)) continue;
        if (!rowHasData(r) || r.timeUtc <= 0.0) continue;
        if (rowAgeSec(r, nowUtc) > kPanelMaxAgeSec) continue;
        StationSample& e = kccHist_[r.code];
        e.code = r.code;
        e.name = r.name;
        e.latDeg = (float)r.latDeg;
        e.lonDeg = (float)r.lonDeg;
        e.cs = (int)(r.cs + 0.5f);
        e.valid = true;
        GiroHistoryPoint p;
        p.timeUtc = r.timeUtc;
        p.foF2 = r.fof2;
        p.foF1 = r.fof1;
        p.muf  = r.mufd;
        p.hmF2 = r.hmf2;
        p.foE  = r.foe;
        p.foEs = r.foes;
        p.fxI  = 0.0f;
        p.B0   = 0.0f;
        p.B1   = 0.0f;
        p.fmin = 0.0f;
        auto& h = e.history;
        if (!h.empty() && h.back().timeUtc == r.timeUtc) {
            h.back() = p;
        } else {
            auto it = h.begin();
            while (it != h.end() && it->timeUtc < r.timeUtc) ++it;
            h.insert(it, p);
            while ((int)h.size() > kHistCap) h.erase(h.begin());
        }
        e.timeUtc = h.back().timeUtc;
    }
}

// FASE B (P1): muestreo al instante epoch — misma semantica clamp/lerp
// que GiroAdapter::sampleHistoryAt; stale se evalua al epoch consumidor.
std::vector<StationSample> Kc2gAdapter::sampleHistoryAt(double epoch) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto lerp = [](float a, float b, double f) -> float {
        if (a > 0.0f && b > 0.0f) return (float)(a + (b - a) * f);
        if (f < 0.5) return a > 0.0f ? a : b;
        return b > 0.0f ? b : a;
    };
    auto fromHist = [](StationSample& c, const GiroHistoryPoint& p) {
        c.foF2 = p.foF2; c.muf = p.muf; c.hmF2 = p.hmF2;
        c.foF1 = p.foF1; c.fxI = p.fxI;
        c.foE = p.foE; c.foEs = p.foEs; c.B0 = p.B0; c.B1 = p.B1; c.fmin = p.fmin;
        c.nmF2 = (p.foF2 > 0.0f) ? (float)(1.24e10 * (double)p.foF2 * (double)p.foF2) : 0.0f;
        c.timeUtc = p.timeUtc;
    };
    std::vector<StationSample> out;
    for (const auto& kv : kccHist_) {
        StationSample c = kv.second;
        const auto& h = c.history;
        if (h.empty()) continue;
        size_t i = 0;
        while (i + 1 < h.size() && h[i + 1].timeUtc <= epoch) i++;
        if (h.size() == 1 || epoch <= h.front().timeUtc) {
            fromHist(c, h.front());
        } else if (epoch >= h.back().timeUtc) {
            fromHist(c, h.back());
        } else {
            double t0 = h[i].timeUtc, t1 = h[i + 1].timeUtc;
            double f = (t1 > t0) ? (epoch - t0) / (t1 - t0) : 0.0;
            GiroHistoryPoint p;
            p.timeUtc = epoch;
            p.foF2 = lerp(h[i].foF2, h[i + 1].foF2, f);
            p.muf  = lerp(h[i].muf,  h[i + 1].muf,  f);
            p.hmF2 = lerp(h[i].hmF2, h[i + 1].hmF2, f);
            p.foE  = lerp(h[i].foE,  h[i + 1].foE,  f);
            p.foEs = lerp(h[i].foEs, h[i + 1].foEs, f);
            p.B0   = lerp(h[i].B0,   h[i + 1].B0,   f);
            p.B1   = lerp(h[i].B1,   h[i + 1].B1,   f);
            p.fmin = lerp(h[i].fmin, h[i + 1].fmin, f);
            p.foF1 = lerp(h[i].foF1, h[i + 1].foF1, f);
            p.fxI  = lerp(h[i].fxI,  h[i + 1].fxI,  f);
            fromHist(c, p);
        }
        c.stale = (epoch - c.timeUtc > kGridMaxAgeSec);  // ventana al consumidor
        out.push_back(c);
    }
    return out;
}

// FASE B (P2): persistencia en disco (KCC1). La historia NO se serializa:
// el barrido la reconstruye via accumulateBatch (dedup por epoch).
int Kc2gAdapter::restoreFromCache(const std::string& dir, double nowUtc) {
    kc2gcache::ScanResult scan = kc2gcache::loadCached(dir);
    for (const auto& f : scan.frames) {
        std::vector<Kc2gRow> rows;
        rows.reserve(f.rows.size());
        for (const auto& cr : f.rows) {
            Kc2gRow r;
            r.code = cr.code;
            r.name = cr.name;
            r.latDeg = cr.latDeg;
            r.lonDeg = cr.lonDeg;
            r.timeUtc = cr.timeUtc;
            r.fof2 = cr.foF2;
            r.mufd = cr.muf;
            r.hmf2 = cr.hmF2;
            r.fof1 = cr.foF1;
            r.foe = cr.foE;
            r.foes = cr.foEs;
            r.hf2 = cr.hF2;
            r.he = cr.hE;
            r.cs = (float)cr.cs;
            rows.push_back(std::move(r));
        }
        accumulateBatch(rows, nowUtc);
    }
    return (int)scan.frames.size();
}

bool Kc2gAdapter::historyStats(int& nSt, double& t0, double& t1) const {
    std::lock_guard<std::mutex> lock(mutex_);
    nSt = 0;
    t0 = 1.0e18;
    t1 = -1.0e18;
    for (const auto& kv : kccHist_) {
        const auto& h = kv.second.history;
        if (h.empty()) continue;
        nSt++;
        for (const auto& p : h) {
            if (p.timeUtc < t0) t0 = p.timeUtc;
            if (p.timeUtc > t1) t1 = p.timeUtc;
        }
    }
    return nSt > 0;
}

double Kc2gAdapter::freshestAgeMinLive(double nowUtc) const {
    std::lock_guard<std::mutex> lock(mutex_);
    double freshest = 1.0e18;
    for (const auto& s : samples_) {
        if (!s.valid || s.timeUtc <= 0.0 || isGridBlockedCode(s.code)) continue;
        const double age = nowUtc - s.timeUtc;
        if (age < 0.0) continue;  // clock skew: como fresca al fetch
        if (age > kGridMaxAgeSec) continue;
        if (age < freshest) freshest = age;
    }
    return (freshest < 1.0e17) ? freshest / 60.0 : -1.0;
}

// ---------------------------------------------------------------------------
// Parser del array completo
// ---------------------------------------------------------------------------
bool Kc2gAdapter::parseStations(const std::string& json, std::vector<Kc2gRow>& out) {
    out.clear();
    const char* p = json.c_str();
    const char* end = p + json.size();
    skipWs(p, end);
    if (p >= end || *p != '[') return false;
    p++;
    skipWs(p, end);
    if (p < end && *p == ']') { p++; skipWs(p, end); return p == end; }

    for (;;) {
        skipWs(p, end);
        if (p >= end || *p != '{') return false;
        p++;
        Kc2gRow row;
        std::string stationCode, stationName, stationLat, stationLon;
        std::string rowTime, rowSource, rowMd;
        float fof2 = 0, mufd = 0, hmf2 = 0, fof1 = 0, foe = 0, foes = 0,
              hf2 = 0, he = 0, cs = 0;
        bool mdIsString = false;

        skipWs(p, end);
        if (p < end && *p == '}') {
            p++;   // objeto vacio: valido
        } else {
            for (;;) {
                skipWs(p, end);
                std::string key;
                if (p >= end || *p != '"' || !parseString(p, end, key)) return false;
                skipWs(p, end);
                if (p >= end || *p != ':') return false;
                p++;
                skipWs(p, end);

                if (key == "station") {
                    if (p >= end || *p != '{') {
                        if (!skipValue(p, end)) return false;
                    } else {
                        p++;
                        skipWs(p, end);
                        if (p < end && *p == '}') {
                            p++;
                        } else {
                            for (;;) {
                                skipWs(p, end);
                                std::string sk;
                                if (p >= end || *p != '"' || !parseString(p, end, sk)) return false;
                                skipWs(p, end);
                                if (p >= end || *p != ':') return false;
                                p++;
                                skipWs(p, end);
                                if (sk == "code") {
                                    if (!parseString(p, end, stationCode)) return false;
                                } else if (sk == "name") {
                                    if (!parseString(p, end, stationName)) return false;
                                } else if (sk == "latitude") {
                                    if (!parseString(p, end, stationLat)) return false;
                                } else if (sk == "longitude") {
                                    if (!parseString(p, end, stationLon)) return false;
                                } else {
                                    if (!skipValue(p, end)) return false;   // id
                                }
                                skipWs(p, end);
                                if (p < end && *p == ',') { p++; continue; }
                                if (p < end && *p == '}') { p++; break; }
                                return false;
                            }
                        }
                    }
                } else if (key == "time") {
                    if (!parseString(p, end, rowTime)) return false;
                } else if (key == "source") {
                    if (!parseString(p, end, rowSource)) return false;
                } else if (key == "md") {
                    // Sello FASE A: md COMO CADENA ("" si null). Si manana
                    // llega como numero, se ignora (no se coercion).
                    if (p < end && *p == '"') {
                        if (!parseString(p, end, rowMd)) return false;
                        mdIsString = true;
                    } else {
                        if (!skipValue(p, end)) return false;
                    }
                } else {
                    float* dst = nullptr;
                    if (key == "fof2") dst = &fof2;
                    else if (key == "mufd") dst = &mufd;
                    else if (key == "hmf2") dst = &hmf2;
                    else if (key == "fof1") dst = &fof1;
                    else if (key == "foe") dst = &foe;
                    else if (key == "foes") dst = &foes;
                    else if (key == "hf2") dst = &hf2;
                    else if (key == "he") dst = &he;
                    else if (key == "cs") dst = &cs;
                    if (dst) {
                        if (p < end && (*p == '-' || (*p >= '0' && *p <= '9'))) {
                            double v = 0.0;
                            if (!parseNumber(p, end, v)) return false;
                            *dst = (float)v;
                        } else {
                            if (!skipValue(p, end)) return false;   // null -> 0
                        }
                    } else {
                        if (!skipValue(p, end)) return false;   // tec/yf1/yf2/.../id
                    }
                }

                skipWs(p, end);
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; break; }
                return false;
            }
        }

        // Fila completa: sin station.code no hay estacion (se descarta).
        if (!stationCode.empty()) {
            row.code = stationCode;
            row.name = stationName;
            row.latDeg = std::atof(stationLat.c_str());
            double lon = std::atof(stationLon.c_str());
            if (lon > 180.0) lon -= 360.0;   // sello: 0-360 -> -180..180 EN PARSE
            row.lonDeg = lon;
            row.timeUtc = isoToEpoch(rowTime);
            row.source = rowSource;
            row.md = mdIsString ? rowMd : std::string();
            row.fof2 = fof2; row.mufd = mufd; row.hmf2 = hmf2;
            row.fof1 = fof1; row.foe = foe;   row.foes = foes;
            row.hf2 = hf2;   row.he = he;     row.cs = cs;
            out.push_back(std::move(row));
        }

        skipWs(p, end);
        if (p < end && *p == ',') { p++; continue; }
        if (p < end && *p == ']') { p++; break; }
        return false;
    }
    skipWs(p, end);
    return p == end;   // solo whitespace despues del array
}

// ---------------------------------------------------------------------------
// Red (1 GET por ciclo)
// ---------------------------------------------------------------------------
Kc2gAdapter::Kc2gAdapter() {}

void Kc2gAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    printf("[OK] Kc2gAdapter initialized (failover-only)\n");
}

size_t Kc2gAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool Kc2gAdapter::fetchURL(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // UA identificable (mismo que getbest; FASE B unificara los UA).
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ionosphere-live-3d/1.0 (educational)");
    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return false;
    return http == 200;
}

bool Kc2gAdapter::fetch(double nowUtc) {
    std::string json;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        status_.lastFetchUtc = nowUtc;   // throttle del INTENTO (exito o no)
    }
    if (!fetchURL(KC2G_STATIONS_URL, json)) {
        std::lock_guard<std::mutex> lk(mutex_);
        error_ = "kc2g: fallo de red/HTTP en prop.kc2g.com/api/stations.json";
        return false;   // el ultimo batch bueno se conserva (patron GIRO)
    }
    std::vector<Kc2gRow> rows;
    if (!parseStations(json, rows)) {
        std::lock_guard<std::mutex> lk(mutex_);
        error_ = "kc2g: JSON malformado (parser)";
        return false;
    }
    // Conversion + time-filter: se guardan SOLO las filas del panel window
    // (<=24 h). Los flags valid/stale quedan acotados al lote: la cadencia
    // del ciclo (600 s) acota su deriva.
    std::vector<StationSample> samples;
    Kc2gStatus st;
    st.rows = (int)rows.size();
    st.lastFetchUtc = nowUtc;
    st.lastOkUtc = nowUtc;
    double freshest = 1.0e18;
    for (const auto& r : rows) {
        StationSample s = toSample(r, nowUtc);
        if (!s.valid) continue;
        samples.push_back(std::move(s));
        st.panelUsable++;
        if (s.stale) {
            st.stale++;
        } else {
            st.gridUsable++;
            double age = rowAgeSec(r, nowUtc);
            if (age >= 0.0 && age < freshest) freshest = age;
        }
    }
    st.freshestAgeMin = (freshest < 1.0e17) ? freshest / 60.0 : -1.0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        samples_ = std::move(samples);
        status_ = st;
        error_.clear();
    }
    accumulateBatch(rows, nowUtc);  // FASE B (P1): historia, independiente del modo
    // FASE B (P2): persiste el batch bueno (gate: >=1 fila panelUsable).
    // La historia NO se serializa: el restore la reconstruye por barrido.
    if (kc2gcache::shouldCacheBatch(st.panelUsable)) {
        kc2gcache::Frame f;
        f.epoch = nowUtc;
        for (const auto& r : rows) {
            if (!panelUsable(r, nowUtc)) continue;
            kc2gcache::Row cr;
            cr.code = r.code;
            cr.name = r.name;
            cr.latDeg = r.latDeg;
            cr.lonDeg = r.lonDeg;
            cr.foF2 = r.fof2;
            cr.muf = r.mufd;
            cr.hmF2 = r.hmf2;
            cr.foF1 = r.fof1;
            cr.foE = r.foe;
            cr.foEs = r.foes;
            cr.hF2 = r.hf2;
            cr.hE = r.he;
            cr.timeUtc = r.timeUtc;
            cr.valid = true;
            cr.stale = !gridUsable(r, nowUtc);
            cr.cs = (int)(r.cs + 0.5f);
            f.rows.push_back(std::move(cr));
        }
        kc2gcache::saveFrame(paths::cacheRootString(), f);
    }
    printf("[Kc2gAdapter] failover: %d/%d usable rows (%d grid, %d stale, fresh %.1f min)\n",
           st.panelUsable, st.rows, st.gridUsable, st.stale,
           st.freshestAgeMin < 0.0 ? -1.0 : st.freshestAgeMin);
    return st.panelUsable > 0;
}

void Kc2gAdapter::fetchAsync(double nowUtc) {
    if (fetching_.exchange(true)) return;   // ya en curso
    std::thread([this, nowUtc]() {
        fetch(nowUtc);
        fetching_.store(false);
    }).detach();
}

std::vector<StationSample> Kc2gAdapter::copySamples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_;
}

float Kc2gAdapter::getDataIndicator() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.rows <= 0) return 0.0f;
    return 100.0f * (float)status_.panelUsable / (float)status_.rows;
}

Kc2gAdapter::Kc2gStatus Kc2gAdapter::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}
