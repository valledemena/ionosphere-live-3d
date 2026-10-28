#include "TleAdapter.h"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#include "Data/FetchState.h"
#include "Utils/Paths.h"

// Formato y numeros reales de cada fuente: ver la cabecera de TleAdapter.h.

namespace {

const char* const kCelestrakUrl =
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=tle";
const char* const kSatnogsUrl = "https://db.satnogs.org/api/tle/?format=json";
const char* const kUserAgent = "IonosphereLive3D/1.0";

// Claves del sidecar: una puerta por endpoint (C2b anadira amateur/stations).
const char* const kKeyActive = "celestrak.active";
const char* const kKeySatnogs = "celestrak.satnogs";

constexpr double kFutureSlackSec = 10.0 * 60.0;
constexpr size_t kMaxSats = 20000;   // techo de dibujo de SatellitesLayer

// Jitter estable: depende de la hora de la cache, no del instante de llamada,
// para que nextNetworkUtc() y fetch() calculen lo mismo.
double jitterFor(double seedSeconds) {
    if (!(seedSeconds > 0.0)) return 0.0;
    unsigned long long h = (unsigned long long)(seedSeconds / 60.0);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return (double)(h % 1201ULL);   // 0..1200 s
}

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

    const std::string dir = paths::userDataDir();
    dirFallback_ = paths::userDataDirFallback();
    cachePath_ = paths::join(dir, "satellites.tle");
    statePath_ = paths::join(dir, "tle-state");

    const double now = (double)std::time(nullptr);
    fetchgate::State st;
    const bool restored = fetchstate::load(statePath_, kKeyActive, st);
    // Un reloj adelantado o un sidecar manipulado no deben bloquear para
    // siempre: el recorte lo hace fetchgate.
    st.blockedUntilUtc = fetchgate::sanitizeBlockedUntil(st.blockedUntilUtc, now);

    fetchgate::State stN;
    fetchstate::load(statePath_, kKeySatnogs, stN);
    stN.blockedUntilUtc = fetchgate::sanitizeBlockedUntil(stN.blockedUntilUtc, now);

    {
        std::lock_guard<std::mutex> lk(mutex_);
        gate_ = st;
        gateSatnogs_ = stN;
        latest_ = TleSnapshot{};
        latest_.note = "sin datos";
    }
    if (dirFallback_) {
        fprintf(stderr,
                "[Tle] directorio de datos de usuario no disponible: la cache y el estado "
                "viven en el CWD\n");
    }
    printf("[OK] TleAdapter initialized (TLE CelesTrak -> cache -> SatNOGS; estado %s%s)\n",
           restored ? "restaurado" : "nuevo", dirFallback_ ? ", CWD" : "");
}

size_t TleAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    const size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

size_t TleAdapter::headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    const size_t total = size * nitems;
    std::string line(buffer, total);
    std::string lower;
    lower.reserve(line.size());
    for (char c : line) lower.push_back((char)std::tolower((unsigned char)c));
    static const std::string kKey = "retry-after:";
    if (lower.compare(0, kKey.size(), kKey) == 0) {
        // Solo segundos (forma habitual en 429). Una fecha HTTP se ignora y
        // cae al bloqueo por defecto.
        const char* v = line.c_str() + kKey.size();
        char* end = nullptr;
        const double sec = std::strtod(v, &end);
        if (end != v && sec > 0.0) *static_cast<double*>(userdata) = sec;
    }
    return total;
}

TleAdapter::HttpResult TleAdapter::httpGet(const char* url, long timeoutSec, std::string& body) {
    HttpResult r;
    CURL* curl = curl_easy_init();
    if (!curl) {
        r.err = "curl: init failed";
        r.curl = (int)CURLE_FAILED_INIT;
        return r;
    }
    body.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    // Los 3xx son aviso de URL obsoleta en la politica de CelesTrak: se
    // informan, no se siguen en silencio.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &r.retryAfter);
    // TIMEOUT es total y se queda corto cuando el enlace se arrastra: el
    // servidor ya conto la descarga y el reintento daria 403. Se espera a que
    // el ritmo se recupere (LOW_SPEED) con un tope total amplio.
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);   // 1 B/s
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);   // durante 30 s
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    r.curl = (int)curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.http);
    curl_easy_cleanup(curl);

    if (r.curl != 0) {
        r.err = std::string("http: ") + curl_easy_strerror((CURLcode)r.curl);
        return r;
    }
    if (r.http != 200) {
        r.err = "http: status " + std::to_string((int)r.http);
        return r;
    }
    if (body.empty()) {
        r.err = "http: empty body";
        return r;
    }
    r.ok = true;
    return r;
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

double TleAdapter::cacheEpoch(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0.0;
    std::string header;
    if (!std::getline(f, header)) return 0.0;
    if (header.compare(0, 15, "# tle-cache v1 ") != 0) return 0.0;
    const size_t ep = header.find("epoch=");
    if (ep == std::string::npos) return 0.0;
    const double epoch = std::atof(header.c_str() + ep + 6);
    return (epoch > 0.0) ? epoch : 0.0;
}

bool TleAdapter::loadCache(const std::string& path, double nowUtc, TleSnapshot& out,
                           double maxAgeSec) {
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
    if (age > maxAgeSec) return false;

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

double TleAdapter::cacheAgeSec(double utc) const {
    const double epoch = cacheEpoch(cachePath_);
    if (!(epoch > 0.0)) return -1.0;
    const double age = utc - epoch;
    // Reloj adelantado (o cache "del futuro"): tratarla como fresca, nunca
    // como negativa (que pasaria cualquier umbral).
    if (age < 0.0) return 0.0;
    return age;
}

void TleAdapter::persistState(const std::string& key, const fetchgate::State& st) {
    fetchstate::save(statePath_, key, st);   // best-effort
}

bool TleAdapter::serveCache(double utc, double maxAgeSec, const std::string& why) {
    // Camino rapido: si el snapshot instalado YA es esta misma cache, no hay
    // que releer ni parsear 16k filas. Se compara solo el epoch (con
    // tolerancia de 0.5 s: la cabecera guarda %.1f) y no la fuente, porque
    // tras un fetch vivo del mismo instante el dato es identico.
    const double onDiskEpoch = cacheEpoch(cachePath_);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const bool sameEpoch = onDiskEpoch > 0.0 && snapshotEpoch_ > 0.0 &&
                               std::fabs(onDiskEpoch - snapshotEpoch_) < 0.5;
        if (sameEpoch) {
            // 72 h estricto: una cache que envejece en el sitio deja de valer.
            if (utc - snapshotEpoch_ <= maxAgeSec) {
                // Si venia de un fetch vivo con el mismo epoch, el dato es
                // identico pero ya no es "live": se reetiqueta para que el
                // panel no siga diciendo celestrak.
                if (latest_.source != TleSource::Cache) {
                    latest_.source = TleSource::Cache;
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "cache: %d sats (edad %.1f h)",
                                  (int)latest_.sats.size(), (utc - snapshotEpoch_) / 3600.0);
                    latest_.note = buf;
                    ++generation_;
                }
                status_ = why;
                error_.clear();
                return true;
            }
            latest_.valid = false;   // caducada: se recarga o no se dibuja
        }
    }
    TleSnapshot cached;
    if (!loadCache(cachePath_, utc, cached, maxAgeSec)) {
        std::lock_guard<std::mutex> lk(mutex_);
        error_ = why + "; sin cache utilizable";
        status_.clear();
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(mutex_);
        // Solo se instala si el epoch cambia: recopiar 16k filas cada hora para
        // el mismo contenido no aporta nada y la UI lo nota.
        if (snapshotEpoch_ != cached.timeUtc || !latest_.valid) {
            latest_ = cached;
            snapshotEpoch_ = cached.timeUtc;
            ++generation_;
        }
        status_ = why;
        error_.clear();
    }
    printf("[Tle] %s (%s)\n", cached.note.c_str(), sourceLabel(TleSource::Cache));
    return true;
}

bool TleAdapter::fetchSatnogs(double utc, TleSnapshot& out, std::string& err) {
    fetchgate::State st;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        st = gateSatnogs_;
    }
    // SatNOGS no tiene cache propia en disco, asi que solo importan las ramas
    // de bloqueo y de backoff.
    const fetchgate::Decision d = fetchgate::decide(utc, st, -1.0, 0.0);
    if (d.action == fetchgate::Action::Blocked || d.action == fetchgate::Action::CacheOnly) {
        err += "; satnogs: " + std::string(d.reason);
        return false;
    }

    // Write-ahead: si la app se cierra durante la descarga, el reinicio ya
    // sabe que se pidio y no repite la peticion (que gastaria la ventana).
    st.lastAttemptUtc = utc;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        gateSatnogs_ = st;
    }
    persistState(kKeySatnogs, st);

    std::string body;
    const HttpResult res = httpGet(kSatnogsUrl, 90L, body);
    bool ok = false;
    if (res.ok) {
        ok = parseSatNogsJson(body, utc, out);
        if (!ok) err += "; satnogs: respuesta ilegible";
    } else {
        err += "; satnogs: " + res.err;
    }

    // Un 200 que no se puede interpretar tambien consumio la descarga: se
    // trata como bloqueo, no como fallo blando (revision C2a).
    const fetchgate::Outcome outcome =
        (!ok && res.http == 200 && res.curl == 0)
            ? fetchgate::unreadableBody()
            : fetchgate::classify(res.http, res.curl, res.retryAfter);

    if (outcome.block) {
        st.blockedUntilUtc = utc + fetchgate::blockDurationSec(outcome);
        st.lastHttp = (int)res.http;
        st.lastCurl = res.curl;
        st.retryFails = 0;
    } else if (outcome.transport || !ok) {
        st.retryFails = std::min(st.retryFails + 1, 6);
        st.lastHttp = (int)res.http;
        st.lastCurl = res.curl;
    } else {
        st.retryFails = 0;
        st.lastHttp = 200;
        st.lastCurl = 0;
        st.lastSuccessUtc = utc;
    }
    {
        std::lock_guard<std::mutex> lk(mutex_);
        gateSatnogs_ = st;
    }
    persistState(kKeySatnogs, st);
    return ok;
}

bool TleAdapter::fetch(double utc) {
    fetchgate::State st;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        st = gate_;
    }

    const double age = cacheAgeSec(utc);
    const double cacheEp = cacheEpoch(cachePath_);
    const double jitter = jitterFor(cacheEp);
    const fetchgate::Decision d = fetchgate::decide(utc, st, age, jitter);

    if (d.action == fetchgate::Action::Blocked) {
        return serveCache(utc, fetchgate::kMaxFallbackAgeSec,
                          std::string("celestrak: ") + d.reason);
    }
    if (d.action == fetchgate::Action::CacheOnly) {
        return serveCache(utc, fetchgate::kMaxFallbackAgeSec,
                          std::string("red: ") + d.reason);
    }
    if (d.action == fetchgate::Action::FetchJittered && d.fetchAtUtc > utc) {
        return serveCache(utc, fetchgate::kMaxFallbackAgeSec,
                          std::string("red: ") + d.reason);
    }

    // --- A partir de aqui se toca la red: un unico intento por pase. ---
    // Write-ahead: el intento se persiste ANTES de la descarga. Si la app se
    // cierra a mitad, el reinicio no repite la peticion y no gasta la ventana
    // de actualizacion de 2 h.
    st.lastAttemptUtc = utc;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        gate_ = st;
    }
    persistState(kKeyActive, st);

    std::string err;
    TleSnapshot snap;
    bool ok = false;

    std::string body;
    const HttpResult res = httpGet(kCelestrakUrl, 120L, body);
    if (res.ok) {
        ok = parseTleCatalog(body, utc, snap);
        if (!ok) err = "celestrak: respuesta ilegible";
    } else {
        err = "celestrak: " + res.err;
    }

    // Un 200 con cuerpo ilegible no es un fallo de red: la descarga ya conto
    // y reintentar a los 5 min daria 403. Bloqueo de 2 h.
    const fetchgate::Outcome outcome =
        (!ok && res.http == 200 && res.curl == 0)
            ? fetchgate::unreadableBody()
            : fetchgate::classify(res.http, res.curl, res.retryAfter);

    if (outcome.block) {
        // Politica: cualquier respuesta distinta de 200 detiene las peticiones
        // 2 h. 429 respeta Retry-After (acotado a 24 h).
        st.blockedUntilUtc = utc + fetchgate::blockDurationSec(outcome);
        st.lastHttp = (int)res.http;
        st.lastCurl = res.curl;
        st.retryFails = 0;
        err += std::string(" [") + outcome.label + "]";
    } else if (outcome.transport || !ok) {
        // Transporte: no es una violacion de politica, solo un fallo blando
        // con backoff 5 -> 60 min.
        st.retryFails = std::min(st.retryFails + 1, 6);
        st.lastHttp = (int)res.http;
        st.lastCurl = res.curl;
    } else {
        st.retryFails = 0;
        st.lastHttp = 200;
        st.lastCurl = 0;
        st.lastSuccessUtc = utc;
    }

    if (!ok) {
        // Cache (hasta 72 h) antes que SatNOGS: un catalogo completo de hace
        // 12 h es mejor que 1.7k satelites frescos, y no gasta otra cuota.
        TleSnapshot cached;
        if (loadCache(cachePath_, utc, cached, fetchgate::kMaxFallbackAgeSec)) {
            snap = cached;
            ok = true;
            err += "; servido de cache";
        } else {
            err += "; cache: sin fichero utilizable";
            // SatNOGS es otro proveedor y tiene su propia puerta: un bloqueo
            // de CelesTrak no lo silencia.
            ok = fetchSatnogs(utc, snap, err);
        }
    }

    if (ok && snap.source != TleSource::Cache) {
        saveCache(cachePath_, snap);   // best-effort: fallar aqui no es fatal
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        gate_ = st;
        if (ok) {
            if (snapshotEpoch_ != snap.timeUtc || !latest_.valid) ++generation_;
            latest_ = snap;
            snapshotEpoch_ = snap.timeUtc;
            // Un catalogo vivo y completo no deja aviso; cache o SatNOGS si
            // (el dato no es "live" y el panel debe poder decirlo).
            if (snap.source == TleSource::Celestrak) {
                error_.clear();
                status_.clear();
            } else {
                error_ = err;
                status_.clear();
            }
        } else {
            error_ = err;               // el snapshot anterior no se toca
            status_.clear();
        }
    }
    persistState(kKeyActive, st);

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

double TleAdapter::nextNetworkUtc(double nowUtc) const {
    fetchgate::State st;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        st = gate_;
    }
    const double age = cacheAgeSec(nowUtc);
    const double jitter = jitterFor(cacheEpoch(cachePath_));
    const fetchgate::Decision d = fetchgate::decide(nowUtc, st, age, jitter);
    switch (d.action) {
        case fetchgate::Action::Blocked:
        case fetchgate::Action::FetchJittered:
            return d.fetchAtUtc;
        case fetchgate::Action::CacheOnly:
            // Espera tras fallo: el instante ya viene calculado. Cache viva:
            // despertar justo al cruzar el TTL (App lo acota a 1 h).
            if (d.fetchAtUtc > 0.0) return d.fetchAtUtc;
            if (age >= 0.0) return nowUtc + (fetchgate::kLiveTtlSec - age) + jitter;
            return nowUtc + 3600.0;
        case fetchgate::Action::FetchNow:
        default:
            return nowUtc;
    }
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

std::string TleAdapter::getStatusNote() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return status_;
}

double TleAdapter::getLastFetchTime() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return snapshotEpoch_;
}

double TleAdapter::getLastAttemptUtc() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return gate_.lastAttemptUtc;
}

double TleAdapter::getBlockedUntilUtc() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return gate_.blockedUntilUtc;
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
