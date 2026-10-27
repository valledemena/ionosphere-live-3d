#include "TleAdapter.h"
#include <curl/curl.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

// Formato y numeros reales de cada fuente: ver la cabecera de TleAdapter.h.

namespace {

const char* const kCelestrakUrl =
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=tle";
const char* const kSatnogsUrl = "https://db.satnogs.org/api/tle/?format=json";
const char* const kCachePath = "cache/satellites.tle";

// TTL del snapshot vivo (misma cadencia que la demo web: 6 h). Evita pedir
// el grupo activo a CelesTrak en cada frame o en cada apertura del panel.
constexpr double kLiveTtlSec = 6.0 * 3600.0;
// Edad maxima de la cache servida como sustituto (nunca se rotula "live").
constexpr double kCacheTtlSec = 6.0 * 3600.0;
constexpr double kFutureSlackSec = 10.0 * 60.0;
constexpr size_t kMaxSats = 20000;   // techo de dibujo de SatellitesLayer

std::string trimLine(const std::string& in) {
    size_t n = in.size();
    while (n > 0) {
        const char c = in[n - 1];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') --n;
        else break;
    }
    return in.substr(0, n);
}

std::string trimName(const std::string& in) {
    size_t b = 0;
    while (b < in.size() && (in[b] == ' ' || in[b] == '\t' || in[b] == '\r')) ++b;
    size_t e = in.size();
    while (e > b && (in[e - 1] == ' ' || in[e - 1] == '\t' || in[e - 1] == '\r')) --e;
    return in.substr(b, e - b);
}

int noradOfLine1(const std::string& line1) {
    if (line1.size() < 7) return 0;
    const std::string field = line1.substr(2, 5);
    // Campo numerico clasico; Alpha-5 (p.ej. "A0000") no es un entero util.
    for (char c : field) {
        if (c < '0' || c > '9') return 0;
    }
    return std::atoi(field.c_str());
}

std::string fallbackName(int noradId) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "NORAD %05d", noradId);
    return buf;
}

void setNote(TleSnapshot& out, size_t dropped) {
    char buf[96];
    if (out.sats.empty()) {
        std::snprintf(buf, sizeof(buf), "sin TLE validos (%d descartados)", (int)dropped);
    } else if (dropped > 0) {
        std::snprintf(buf, sizeof(buf), "%d sats (%d descartados)", (int)out.sats.size(),
                      (int)dropped);
    } else {
        std::snprintf(buf, sizeof(buf), "%d sats", (int)out.sats.size());
    }
    out.note = buf;
}

// Extrae el siguiente objeto {...} de nivel 0. Respeta cadenas y escapes.
bool nextJsonObject(const std::string& s, size_t& pos, std::string& obj) {
    const size_t start = s.find('{', pos);
    if (start == std::string::npos) return false;
    int depth = 0;
    bool inStr = false;
    bool esc = false;
    for (size_t i = start; i < s.size(); ++i) {
        const char c = s[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') { ++depth; continue; }
        if (c == '}') {
            --depth;
            if (depth == 0) {
                obj = s.substr(start, i - start + 1);
                pos = i + 1;
                return true;
            }
        }
    }
    return false;
}

// Valor string de una clave plana (SatNOGS: tle0/tle1/tle2, sin anidamiento).
bool jsonString(const std::string& obj, const char* key, std::string& out) {
    const std::string pat = std::string("\"") + key + "\"";
    const size_t k = obj.find(pat);
    if (k == std::string::npos) return false;
    const size_t colon = obj.find(':', k + pat.size());
    if (colon == std::string::npos) return false;
    size_t q = obj.find('"', colon + 1);
    if (q == std::string::npos) return false;
    std::string v;
    for (size_t i = q + 1; i < obj.size(); ++i) {
        const char c = obj[i];
        if (c == '\\' && i + 1 < obj.size()) {
            v.push_back(obj[i + 1]);
            ++i;
            continue;
        }
        if (c == '"') {
            out = v;
            return true;
        }
        v.push_back(c);
    }
    return false;
}

bool jsonInt(const std::string& obj, const char* key, int& out) {
    const std::string pat = std::string("\"") + key + "\"";
    const size_t k = obj.find(pat);
    if (k == std::string::npos) return false;
    const size_t colon = obj.find(':', k + pat.size());
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
    const size_t start = i;
    while (i < obj.size() && ((obj[i] >= '0' && obj[i] <= '9') || obj[i] == '-')) ++i;
    if (i == start) return false;
    out = std::atoi(obj.substr(start, i - start).c_str());
    return true;
}

}  // namespace

void TleAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::lock_guard<std::mutex> lk(mutex_);
    latest_ = TleSnapshot{};
    latest_.note = "sin datos";
    printf("[OK] TleAdapter initialized (TLE CelesTrak -> SatNOGS -> cache)\n");
}

size_t TleAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    const size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool TleAdapter::httpGet(const char* url, long timeoutSec, std::string& body, std::string& err) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        err = "curl: init failed";
        return false;
    }
    body.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    const CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        err = std::string("http: ") + curl_easy_strerror(res);
        return false;
    }
    if (code != 200) {
        err = "http: status " + std::to_string((int)code);
        return false;
    }
    if (body.empty()) {
        err = "http: empty body";
        return false;
    }
    return true;
}

bool TleAdapter::parseTleCatalog(const std::string& text, double nowUtc, TleSnapshot& out) {
    out = TleSnapshot{};
    out.timeUtc = nowUtc;
    out.source = TleSource::Celestrak;

    std::vector<std::string> lines;
    lines.reserve(text.size() / 80 + 1);
    size_t start = 0;
    while (start <= text.size()) {
        const size_t nl = text.find('\n', start);
        const std::string raw =
            (nl == std::string::npos) ? text.substr(start) : text.substr(start, nl - start);
        lines.push_back(trimLine(raw));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }

    size_t dropped = 0;
    size_t i = 0;
    while (i < lines.size()) {
        const std::string& line = lines[i];
        if (line.size() > 1 && line[0] == '1' && line[1] == ' ') {
            // El nombre es la linea anterior, si no es otra linea TLE ni un
            // comentario de la cache.
            std::string name;
            if (i > 0 && !lines[i - 1].empty()) {
                const std::string& prev = lines[i - 1];
                const bool prevIsTle = prev.size() > 1 && (prev[0] == '1' || prev[0] == '2');
                if (!prevIsTle && prev[0] != '#') name = trimName(prev);
            }
            orbitprop::OrbitElements el;
            if (i + 1 < lines.size() && orbitprop::parseTle(line, lines[i + 1], el)) {
                TleSat sat;
                sat.el = el;
                sat.noradId = noradOfLine1(line);
                sat.line1 = line;
                sat.line2 = lines[i + 1];
                sat.name = name.empty() ? fallbackName(sat.noradId) : name;
                if (out.sats.size() < kMaxSats) out.sats.push_back(sat);
                else ++dropped;
                i += 2;
                continue;
            }
            ++dropped;
            i += (i + 1 < lines.size()) ? 2 : 1;
            continue;
        }
        ++i;
    }

    setNote(out, dropped);
    out.valid = !out.sats.empty();
    return out.valid;
}

bool TleAdapter::parseSatNogsJson(const std::string& json, double nowUtc, TleSnapshot& out) {
    out = TleSnapshot{};
    out.timeUtc = nowUtc;
    out.source = TleSource::SatNogs;

    size_t pos = 0;
    size_t dropped = 0;
    std::string obj;
    while (nextJsonObject(json, pos, obj)) {
        std::string l1, l2, tle0;
        if (!jsonString(obj, "tle1", l1) || !jsonString(obj, "tle2", l2)) {
            ++dropped;
            continue;
        }
        orbitprop::OrbitElements el;
        if (!orbitprop::parseTle(l1, l2, el)) {
            ++dropped;
            continue;
        }
        TleSat sat;
        sat.el = el;
        sat.line1 = trimLine(l1);
        sat.line2 = trimLine(l2);
        sat.noradId = 0;
        jsonInt(obj, "norad_cat_id", sat.noradId);
        if (jsonString(obj, "tle0", tle0)) {
            std::string n = trimName(tle0);
            // tle0 viene como "0 NOMBRE": se quita el prefijo de linea.
            if (n.size() > 2 && n[0] == '0' && n[1] == ' ') n = trimName(n.substr(2));
            if (!n.empty() && n != "0") sat.name = n;
        }
        if (sat.name.empty()) sat.name = fallbackName(sat.noradId);
        if (out.sats.size() < kMaxSats) out.sats.push_back(sat);
        else ++dropped;
    }

    setNote(out, dropped);
    out.valid = !out.sats.empty();
    return out.valid;
}

bool TleAdapter::saveCache(const std::string& path, const TleSnapshot& snap) {
    if (!snap.valid || snap.sats.empty()) return false;   // nunca guardar vacio
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);

    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        char header[160];
        std::snprintf(header, sizeof(header), "# tle-cache v1 epoch=%.1f count=%d source=%s\n",
                      snap.timeUtc, (int)snap.sats.size(), TleAdapter::sourceLabel(snap.source));
        f << header;
        for (const TleSat& s : snap.sats) {
            // Registro de 3 lineas; si el nombre viniera vacio se usa el NORAD
            // (nunca se deja una linea en blanco que rompa el grupo).
            const std::string& nm = s.name.empty() ? fallbackName(s.noradId) : s.name;
            f << nm << '\n' << s.line1 << '\n' << s.line2 << '\n';
        }
        if (!f.good()) return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

bool TleAdapter::loadCache(const std::string& path, double nowUtc, TleSnapshot& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (text.empty()) return false;

    const size_t nl = text.find('\n');
    const std::string header = (nl == std::string::npos) ? text : text.substr(0, nl);
    if (header.compare(0, 15, "# tle-cache v1 ") != 0) return false;
    const size_t ep = header.find("epoch=");
    if (ep == std::string::npos) return false;
    const double epoch = std::atof(header.c_str() + ep + 6);
    if (!(epoch > 0.0)) return false;
    if (epoch > nowUtc + kFutureSlackSec) return false;
    const double age = nowUtc - epoch;
    if (age > kCacheTtlSec) return false;

    TleSnapshot snap;
    if (!parseTleCatalog(text, epoch, snap)) return false;
    snap.source = TleSource::Cache;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "cache: %d sats (edad %.1f h)", (int)snap.sats.size(),
                  age / 3600.0);
    snap.note = buf;
    out = snap;
    return true;
}

bool TleAdapter::fetch(double utc) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        // TTL del snapshot vivo: durante 6 h no se vuelve a pedir el grupo.
        if (latest_.valid && latest_.source != TleSource::Cache && lastFetchUtc_ > 0.0) {
            const double age = utc - lastFetchUtc_;
            if (age >= 0.0 && age < kLiveTtlSec) return true;
        }
    }

    std::string err;
    TleSnapshot snap;
    bool ok = false;

    std::string body;
    if (httpGet(kCelestrakUrl, 25L, body, err)) {
        if (parseTleCatalog(body, utc, snap)) ok = true;
        else err = "celestrak: sin TLE validos";
    } else {
        err = "celestrak: " + err;
    }

    if (!ok) {
        // Fallback: subconjunto con emisor registrado (~1.7k, no el catalogo).
        std::string body2;
        std::string err2;
        if (httpGet(kSatnogsUrl, 15L, body2, err2)) {
            if (parseSatNogsJson(body2, utc, snap)) ok = true;
            else err += "; satnogs: sin filas validas";
        } else {
            err += "; satnogs: " + err2;
        }
    }

    if (!ok) {
        // Ultimo recurso declarado: cache de disco joven. Nunca se rotula viva.
        TleSnapshot cached;
        if (loadCache(kCachePath, utc, cached)) {
            snap = cached;
            ok = true;
            err += "; servido de cache";
        } else {
            err += "; cache: sin fichero fresco";
        }
    }

    if (ok && snap.source != TleSource::Cache) {
        saveCache(kCachePath, snap);   // best-effort: fallar aqui no es fatal
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (ok) {
            latest_ = snap;
            lastFetchUtc_ = utc;        // ultimo fetch CORRECTO
            ++generation_;
            // Si se sirvio de cache, el motivo del fallo vivo se conserva:
            // el dato no es "live" y el panel debe poder decirlo.
            error_ = (snap.source == TleSource::Cache) ? err : std::string();
        } else {
            error_ = err;               // el snapshot anterior no se toca
        }
    }

    if (!ok) {
        fprintf(stderr, "[Tle] %s\n", err.c_str());
        return false;
    }
    printf("[Tle] %s (%s)\n", snap.note.c_str(), sourceLabel(snap.source));
    return true;
}

void TleAdapter::fetchAsync(double utc) {
    if (fetching_.exchange(true)) return;   // ya en curso
    std::thread([this, utc]() {
        fetch(utc);
        fetching_.store(false);
    }).detach();
}

TleSnapshot TleAdapter::getSnapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return latest_;
}

unsigned long TleAdapter::getGeneration() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return generation_;
}

TleSource TleAdapter::source() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return latest_.source;
}

int TleAdapter::satCount() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return (int)latest_.sats.size();
}

std::string TleAdapter::getErrorMessage() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return error_;
}

std::string TleAdapter::getSourceLabel() const {
    return sourceLabel(source());
}

const char* TleAdapter::sourceLabel(TleSource s) {
    switch (s) {
        case TleSource::Celestrak: return "celestrak";
        case TleSource::SatNogs: return "satnogs";
        case TleSource::Cache: return "cache";
        case TleSource::None:
        default: return "sin datos";
    }
}
