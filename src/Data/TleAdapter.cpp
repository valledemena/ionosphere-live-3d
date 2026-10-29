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
#include <unordered_map>
#include <vector>
#include "Data/FetchState.h"
#include "Utils/Paths.h"

// Formato y numeros reales de cada fuente: ver la cabecera de TleAdapter.h.

namespace {

const char* const kCelestrakUrl =
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=csv";
const char* const kSatnogsUrl = "https://db.satnogs.org/api/tle/?format=json";
const char* const kUserAgent = "IonosphereLive3D/1.0";

// Claves del sidecar: una puerta por endpoint.
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

// --- CSV / OMM de CelesTrak -------------------------------------------------
// GP CSV: una fila de cabecera con nombres y una fila por objeto. Los campos
// de texto pueden venir entre comillas (un nombre puede llevar coma) y una
// comilla literal se escribe doble. Se parsea de verdad, no partiendo por
// comas: "SAT, ""X"" 2" es UN campo.

// RFC 4180. Acepta CRLF o LF; ignora CR fuera de comillas.
std::vector<std::vector<std::string>> parseCsvRows(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool inQuotes = false;
    bool started = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == '"') {
            inQuotes = true;
            started = true;
        } else if (c == ',') {
            row.push_back(field);
            field.clear();
            started = true;
        } else if (c == '\n') {
            row.push_back(field);
            rows.push_back(row);
            row.clear();
            field.clear();
            started = false;
        } else if (c != '\r') {
            field += c;
            started = true;
        }
    }
    // Ultima fila sin salto final. Si el fichero acaba en \n no hay nada mas.
    if (started || !field.empty() || !row.empty()) {
        row.push_back(field);
        rows.push_back(row);
    }
    return rows;
}

std::string upperTrim(const std::string& in) {
    std::string out = trimName(in);
    for (char& c : out) c = (char)std::toupper((unsigned char)c);
    return out;
}

// "2026-10-27T14:18:26.839680" (UTC) -> segundos epoch. Fraccion opcional.
bool iso8601Utc(const std::string& in, double& out) {
    const std::string s = trimName(in);
    if (s.size() < 19) return false;
    for (size_t i = 0; i < 19; ++i) {
        const char c = s[i];
        if (i == 4 || i == 7) {
            if (c != '-') return false;
        } else if (i == 10) {
            if (c != 'T' && c != ' ') return false;
        } else if (i == 13 || i == 16) {
            if (c != ':') return false;
        } else if (c < '0' || c > '9') {
            return false;
        }
    }
    const int y = std::atoi(s.substr(0, 4).c_str());
    const int mo = std::atoi(s.substr(5, 2).c_str());
    const int d = std::atoi(s.substr(8, 2).c_str());
    const int h = std::atoi(s.substr(11, 2).c_str());
    const int mi = std::atoi(s.substr(14, 2).c_str());
    const int se = std::atoi(s.substr(17, 2).c_str());
    double frac = 0.0;
    if (s.size() > 19 && s[19] == '.') frac = std::atof(s.c_str() + 19);
    if (y < 1957 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    if (h > 23 || mi > 59 || se > 60) return false;

    // days_from_civil (Hinnant): dias desde 1970-01-01, sin tablas de mes.
    long long yy = y;
    yy -= (mo <= 2) ? 1 : 0;
    const long long era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = (unsigned)(yy - era * 400);
    const unsigned doy = (unsigned)((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = era * 146097 + (long long)doe - 719468;

    out = (double)days * 86400.0 + h * 3600.0 + mi * 60.0 + se + frac;
    return true;
}

bool csvNumber(const std::vector<std::string>& row, const std::vector<int>& col, size_t idx,
               double& out) {
    if (col[idx] < 0 || (size_t)col[idx] >= row.size()) return false;
    const std::string& v = row[(size_t)col[idx]];
    if (v.empty()) return false;
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0' || !std::isfinite(d)) return false;
    out = d;
    return true;
}

// Cabecera de cache, v1 (sin format=, cuerpo TLE) o v2 (con format=).
bool isCacheHeader(const std::string& header) {
    return header.compare(0, 15, "# tle-cache v1 ") == 0 ||
           header.compare(0, 15, "# tle-cache v2 ") == 0;
}

void setNote(TleSnapshot& out, size_t dropped) {
    char buf[128];
    const size_t total = out.sats.size() + dropped;
    const int pct = (total > 0) ? (int)(100.0 * (double)dropped / (double)total) : 0;
    if (out.sats.empty()) {
        std::snprintf(buf, sizeof(buf), "sin TLE validos (%d descartados, %d%%)",
                      (int)dropped, pct);
    } else if (dropped > 0) {
        std::snprintf(buf, sizeof(buf), "%d sats (%d descartados, %d%%)",
                      (int)out.sats.size(), (int)dropped, pct);
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

    const std::filesystem::path dir = paths::userDataDir();
    dirFallback_ = paths::userDataDirFallback();
    cachePath_ = dir / "satellites.tle";
    satnogsCachePath_ = dir / "satellites-satnogs.tle";
    statePath_ = dir / "tle-state";

    const double now = (double)std::time(nullptr);
    fetchgate::State st;
    const bool restored = fetchstate::load(statePath_, kKeyActive, st);
    // Un reloj adelantado o un sidecar manipulado no deben bloquear para
    // siempre: el recorte lo hace fetchgate.
    st.blockedUntilUtc = fetchgate::sanitizeBlockedUntil(st.blockedUntilUtc, now);
    // lastHttp == -1 es el provisional del write-ahead. Si sobrevive a un
    // arranque, la peticion estaba EN VUELO cuando murio la app: el bloqueo de
    // 2 h se mantiene (no se repite la descarga), pero ya no es "en curso", asi
    // que deja de ocultarse en el panel (getBlockedUntilUtc).
    if (st.lastHttp == -1) st.lastHttp = 0;

    fetchgate::State stN;
    fetchstate::load(statePath_, kKeySatnogs, stN);
    stN.blockedUntilUtc = fetchgate::sanitizeBlockedUntil(stN.blockedUntilUtc, now);
    if (stN.lastHttp == -1) stN.lastHttp = 0;

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
    // Costura de pruebas: si hay transporte inyectado, no se toca curl.
    if (transport_) {
        HttpResult r = transport_(url, body);
        if (!r.ok && r.err.empty()) r.err = "transporte simulado";
        return r;
    }
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

bool TleAdapter::parseCelesTrakCsv(const std::string& text, double nowUtc, TleSnapshot& out) {
    out = TleSnapshot{};
    out.timeUtc = nowUtc;
    out.source = TleSource::Celestrak;

    const std::vector<std::vector<std::string>> rows = parseCsvRows(text);
    if (rows.size() < 2) return false;   // sin cabecera o sin filas

    // Las columnas se localizan POR NOMBRE. El orden del GP CSV puede cambiar
    // (y de hecho ha cambiado al anadir OBJECT_TYPE/EPOCH ISO), asi que
    // indexar por posicion es una bomba de relojeria.
    enum CsvCol {
        kName = 0,
        kNorad,
        kEpoch,
        kMeanMotion,
        kEcc,
        kInc,
        kRaan,
        kArgp,
        kMeanAnom,
        kCsvCols
    };
    static const char* const kColNames[kCsvCols] = {
        "OBJECT_NAME",     "NORAD_CAT_ID",  "EPOCH",        "MEAN_MOTION", "ECCENTRICITY",
        "INCLINATION",     "RA_OF_ASC_NODE", "ARG_OF_PERICENTER", "MEAN_ANOMALY"};
    std::vector<int> col(kCsvCols, -1);
    for (size_t i = 0; i < rows[0].size(); ++i) {
        const std::string name = upperTrim(rows[0][i]);
        for (int k = 0; k < kCsvCols; ++k) {
            if (name == kColNames[k]) col[k] = (int)i;
        }
    }
    // Sin los elementos no hay nada que dibujar: mejor fallar que inventar.
    // El nombre es opcional (hay fallback por NORAD).
    for (int k = 1; k < kCsvCols; ++k) {
        if (col[k] < 0) return false;
    }

    size_t dropped = 0;
    for (size_t r = 1; r < rows.size(); ++r) {
        const std::vector<std::string>& row = rows[r];
        if (row.empty() || (row.size() == 1 && trimName(row[0]).empty())) continue;   // linea vacia

        // NORAD como ENTERO: aqui caben los 6 digitos que el TLE clasico ya no
        // puede escribir (catalogo de 5 digitos agotado el 2026-07-11).
        double norad = 0.0;
        if (!csvNumber(row, col, kNorad, norad) || norad < 1.0 || norad > 1.0e9) {
            ++dropped;
            continue;
        }
        double epoch = 0.0;
        if (col[kEpoch] < 0 || (size_t)col[kEpoch] >= row.size() ||
            !iso8601Utc(row[(size_t)col[kEpoch]], epoch)) {
            ++dropped;
            continue;
        }
        double mm = 0.0, ecc = 0.0, inc = 0.0, raan = 0.0, argp = 0.0, ma = 0.0;
        if (!csvNumber(row, col, kMeanMotion, mm) || !csvNumber(row, col, kEcc, ecc) ||
            !csvNumber(row, col, kInc, inc) || !csvNumber(row, col, kRaan, raan) ||
            !csvNumber(row, col, kArgp, argp) || !csvNumber(row, col, kMeanAnom, ma)) {
            ++dropped;
            continue;
        }
        // Mismos topes que parseTle: si no es un satelite terrestre, fuera.
        if (!(mm > 0.0 && mm <= 20.0) || !(ecc >= 0.0 && ecc < 1.0) ||
            !(inc >= 0.0 && inc <= 180.0)) {
            ++dropped;
            continue;
        }
        // EPOCH de un objeto activo: ni del futuro ni de hace un ano. Con el
        // reloj del sistema mal, esto descarta filas absurdas sin tocar las
        // buenas (la cache se valida contra su propia cabecera, no contra el
        // reloj de pared).
        if (epoch > nowUtc + 7.0 * 86400.0 || epoch < nowUtc - 365.0 * 86400.0) {
            ++dropped;
            continue;
        }

        TleSat sat;
        sat.noradId = (int)norad;
        if (col[kName] >= 0 && (size_t)col[kName] < row.size()) {
            sat.name = trimName(row[(size_t)col[kName]]);
        }
        if (sat.name.empty()) sat.name = fallbackName(sat.noradId);
        sat.el.epochUtc = epoch;
        sat.el.incDeg = inc;
        sat.el.raanDeg = std::fmod(std::fmod(raan, 360.0) + 360.0, 360.0);
        sat.el.ecc = ecc;
        sat.el.argpDeg = std::fmod(std::fmod(argp, 360.0) + 360.0, 360.0);
        sat.el.meanAnomDeg = std::fmod(std::fmod(ma, 360.0) + 360.0, 360.0);
        sat.el.meanMotionRevDay = mm;
        sat.el.valid = true;

        if (out.sats.size() < kMaxSats) out.sats.push_back(sat);
        else ++dropped;
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

TleSnapshot TleAdapter::mergeSnapshots(const std::vector<TleSnapshot>& parts) {
    TleSnapshot out;
    // indice NORAD -> posicion en out.sats. Con ~20k filas por grupo, buscar
    // linealmente por cada fila seria cuadratico.
    std::unordered_map<int, std::size_t> byNorad;
    std::size_t groups = 0;
    std::size_t dups = 0;
    std::size_t dropped = 0;
    double oldest = 0.0;
    bool haveOldest = false;
    TleSource firstSource = TleSource::None;
    bool sameSource = true;

    for (const TleSnapshot& p : parts) {
        if (!p.valid || p.sats.empty()) continue;   // un grupo caido no aporta nada
        if (groups == 0) firstSource = p.source;
        else if (p.source != firstSource) sameSource = false;
        ++groups;
        if (!haveOldest || p.timeUtc < oldest) {
            oldest = p.timeUtc;
            haveOldest = true;
        }
        for (const TleSat& s : p.sats) {
            if (s.noradId > 0) {
                const auto it = byNorad.find(s.noradId);
                if (it != byNorad.end()) {
                    // Gana el EPOCH mas nuevo. El orden de los grupos no
                    // decide nada: el mismo objeto puede venir de un grupo
                    // actualizado antes que otro.
                    TleSat& prev = out.sats[it->second];
                    if (s.el.epochUtc > prev.el.epochUtc) prev = s;
                    ++dups;
                    continue;
                }
                if (out.sats.size() >= kMaxSats) {
                    ++dropped;
                    continue;
                }
                byNorad.emplace(s.noradId, out.sats.size());
                out.sats.push_back(s);
            } else {
                // Sin NORAD no hay clave de dedup: se conserva tal cual.
                if (out.sats.size() >= kMaxSats) {
                    ++dropped;
                    continue;
                }
                out.sats.push_back(s);
            }
        }
    }

    if (groups == 0) {
        out.timeUtc = 0.0;
        out.valid = false;
        out.source = TleSource::None;
        out.note = "sin datos";
        return out;
    }

    out.timeUtc = oldest;
    out.valid = !out.sats.empty();
    // Grupos con origenes distintos: una parte no es viva, asi que el
    // conjunto se declara como cache en vez de pasar por catalogo fresco.
    out.source = sameSource ? firstSource : TleSource::Cache;

    char buf[192];
    if (dups == 0 && dropped == 0) {
        std::snprintf(buf, sizeof(buf), "%d sats (%d grupos%s)", (int)out.sats.size(),
                      (int)groups, sameSource ? "" : ", mixto");
    } else {
        std::snprintf(buf, sizeof(buf), "%d sats (%d grupos, %d duplicados%s%s)",
                      (int)out.sats.size(), (int)groups, (int)dups,
                      dropped > 0 ? ", techo alcanzado" : "", sameSource ? "" : ", mixto");
    }
    out.note = buf;
    return out;
}

bool TleAdapter::saveCache(const std::filesystem::path& path, const TleSnapshot& snap,
                           const std::string& rawBody) {
    if (!snap.valid || snap.sats.empty()) return false;   // nunca guardar vacio
    namespace fs = std::filesystem;

    // Con cuerpo crudo se guarda TAL CUAL (el CSV de CelesTrak): no hay que
    // reconstruir nada y sobreviven los NORAD de 6 digitos, que no caben en un
    // TLE de 5 columnas. Sin cuerpo crudo solo se puede volcar un TLE de 3
    // lineas (SatNOGS o ficheros viejos); si falta alguna linea, no se guarda
    // una cache que luego no se podria releer.
    const bool csv = !rawBody.empty();
    if (!csv) {
        for (const TleSat& s : snap.sats) {
            if (s.line1.empty() || s.line2.empty()) return false;
        }
    }

    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);

    // tmp hermana del destino (fs::path, no std::string): con un perfil no
    // ASCII el nombre debe viajar intacto hasta el rename.
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        char header[192];
        std::snprintf(header, sizeof(header),
                      "# tle-cache v2 epoch=%.1f count=%d source=%s format=%s\n", snap.timeUtc,
                      (int)snap.sats.size(), TleAdapter::sourceLabel(snap.source),
                      csv ? "csv" : "tle");
        f << header;
        if (csv) {
            f.write(rawBody.data(), (std::streamsize)rawBody.size());
            if (rawBody.back() != '\n') f << '\n';
        } else {
            for (const TleSat& s : snap.sats) {
                // Registro de 3 lineas; si el nombre viniera vacio se usa el
                // NORAD (nunca se deja una linea en blanco que rompa el grupo).
                const std::string& nm = s.name.empty() ? fallbackName(s.noradId) : s.name;
                f << nm << '\n' << s.line1 << '\n' << s.line2 << '\n';
            }
        }
        if (!f.good()) return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

double TleAdapter::cacheEpoch(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0.0;
    std::string header;
    if (!std::getline(f, header)) return 0.0;
    if (!isCacheHeader(header)) return 0.0;
    const size_t ep = header.find("epoch=");
    if (ep == std::string::npos) return 0.0;
    const double epoch = std::atof(header.c_str() + ep + 6);
    return (epoch > 0.0) ? epoch : 0.0;
}

double TleAdapter::celestrakCacheEpoch(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0.0;
    std::string header;
    if (!std::getline(f, header)) return 0.0;
    // Un satellites.tle con source=satnogs lo escribieron los builds anteriores
    // a c76d6a5 (el subconjunto, con epoch = ahora). Tratarlo como "sin cache"
    // es lo unico seguro: si contara como cache joven, la puerta no pediria el
    // catalogo completo en 6 h y ademas se dibujaria como si lo fuera.
    if (header.find("source=satnogs") != std::string::npos) return 0.0;
    if (!isCacheHeader(header)) return 0.0;
    const size_t ep = header.find("epoch=");
    if (ep == std::string::npos) return 0.0;
    const double epoch = std::atof(header.c_str() + ep + 6);
    return (epoch > 0.0) ? epoch : 0.0;
}

double TleAdapter::celestrakCacheAge(const std::filesystem::path& path, double utc) {
    const double epoch = celestrakCacheEpoch(path);
    if (!(epoch > 0.0)) return -1.0;
    const double age = utc - epoch;
    return age < 0.0 ? 0.0 : age;   // reloj adelantado: nunca negativa
}

bool TleAdapter::loadCelestrakCache(const std::filesystem::path& path, double nowUtc,
                                    TleSnapshot& out, double maxAgeSec) {
    TleSnapshot snap;
    if (!loadCache(path, nowUtc, snap, maxAgeSec)) return false;
    // Misma razon que en celestrakCacheEpoch: el subconjunto de SatNOGS tiene
    // su propio fichero y no puede servirse como catalogo activo completo.
    if (snap.source == TleSource::SatNogsCache) return false;
    out = snap;
    return true;
}

bool TleAdapter::loadCache(const std::filesystem::path& path, double nowUtc, TleSnapshot& out,
                           double maxAgeSec) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (text.empty()) return false;

    const size_t nl = text.find('\n');
    const std::string header = (nl == std::string::npos) ? text : text.substr(0, nl);
    if (!isCacheHeader(header)) return false;
    const size_t ep = header.find("epoch=");
    if (ep == std::string::npos) return false;
    const double epoch = std::atof(header.c_str() + ep + 6);
    if (!(epoch > 0.0)) return false;
    if (epoch > nowUtc + kFutureSlackSec) return false;
    const double age = nowUtc - epoch;
    if (age > maxAgeSec) return false;

    // v2 lleva `format=`; v1 (builds anteriores) siempre fue TLE de 3 lineas.
    const std::string body = (nl == std::string::npos) ? std::string() : text.substr(nl + 1);
    const bool csv = header.find("format=csv") != std::string::npos;

    TleSnapshot snap;
    const bool ok = csv ? parseCelesTrakCsv(body, epoch, snap) : parseTleCatalog(body, epoch, snap);
    if (!ok) return false;
    // La cabecera guarda el origen (`source=`), asi que una cache de SatNOGS
    // se distingue del catalogo completo aunque ambas se lean del disco.
    snap.source = (header.find("source=satnogs") != std::string::npos)
                      ? TleSource::SatNogsCache
                      : TleSource::Cache;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "cache: %d sats (edad %.1f h)", (int)snap.sats.size(),
                  age / 3600.0);
    snap.note = buf;
    out = snap;
    return true;
}

double TleAdapter::cacheAgeOf(const std::filesystem::path& path, double utc) {
    const double epoch = cacheEpoch(path);
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

bool TleAdapter::serveCache(const GroupSpec& group, double utc, double maxAgeSec,
                            const std::string& why) {
    // Camino rapido: si el snapshot instalado YA es esta misma cache, no hay
    // que releer ni parsear 16k filas. Se compara solo el epoch (con
    // tolerancia de 0.5 s: la cabecera guarda %.1f) y no la fuente: el
    // snapshot no se muta, la etiqueta sigue diciendo de donde vino el dato y
    // la edad se calcula en la UI.
    const double onDiskEpoch = celestrakCacheEpoch(group.cache);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const bool sameEpoch = onDiskEpoch > 0.0 && snapshotEpoch_ > 0.0 &&
                               std::fabs(onDiskEpoch - snapshotEpoch_) < 0.5;
        if (sameEpoch) {
            // 72 h estricto: una cache que envejece en el sitio deja de valer.
            if (utc - snapshotEpoch_ <= maxAgeSec) {
                status_ = why;
                error_.clear();
                return true;
            }
            // Caducada: se invalida UNA vez y se sube la generacion, porque
            // App solo recopia el snapshot cuando cambia. Poner el epoch a 0
            // evita que esta rama vuelva a dispararse en cada pase (antes
            // subia la generacion sin parar y la UI recopiaba en bucle).
            if (latest_.valid) {
                latest_.valid = false;
                latest_.sats.clear();
                latest_.note = "cache caducada (>72 h)";
                snapshotEpoch_ = 0.0;
                ++generation_;
            }
        }
    }
    TleSnapshot cached;
    if (!loadCelestrakCache(group.cache, utc, cached, maxAgeSec)) {
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
    // Cache PROPIA de SatNOGS: el subconjunto de ~1.7k satelites nunca se
    // guarda en satellites.tle, o pasaria por catalogo completo durante 6 h.
    const double age = cacheAgeOf(satnogsCachePath_, utc);
    const double jitter = jitterFor(cacheEpoch(satnogsCachePath_));
    const fetchgate::Decision d = fetchgate::decide(utc, st, age, jitter);

    const bool networkAllowed =
        d.action == fetchgate::Action::FetchNow ||
        (d.action == fetchgate::Action::FetchJittered && d.fetchAtUtc <= utc);
    if (!networkAllowed) {
        TleSnapshot cached;
        if (loadCache(satnogsCachePath_, utc, cached, fetchgate::kMaxFallbackAgeSec)) {
            out = cached;
            err += "; satnogs: servido de cache";
            return true;
        }
        err += "; satnogs: " + std::string(d.reason);
        return false;
    }

    // Write-ahead: el intento se persiste ANTES de la descarga con un bloqueo
    // provisional. Si la app muere a mitad, el reinicio ve el bloqueo y no
    // repite una peticion que el servidor ya pudo contar.
    st.lastAttemptUtc = utc;
    st.blockedUntilUtc = utc + fetchgate::kBlockSec;
    st.lastHttp = -1;
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
        st.blockedUntilUtc = 0.0;   // se retira el provisional
        st.retryFails = std::min(st.retryFails + 1, 6);
        st.lastHttp = (int)res.http;
        st.lastCurl = res.curl;
    } else {
        st.blockedUntilUtc = 0.0;   // se retira el provisional
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

    if (ok) saveCache(satnogsCachePath_, out);
    return ok;
}

TleAdapter::GroupSpec TleAdapter::activeGroup() const {
    return GroupSpec{kKeyActive, kCelestrakUrl, cachePath_};
}

bool TleAdapter::fetch(double utc) { return fetchGroup(activeGroup(), utc); }

bool TleAdapter::fetchGroup(const GroupSpec& group, double utc) {
    fetchgate::State st;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        st = gate_;
    }

    const double age = celestrakCacheAge(group.cache, utc);
    const double cacheEp = celestrakCacheEpoch(group.cache);
    const double jitter = jitterFor(cacheEp);
    const fetchgate::Decision d = fetchgate::decide(utc, st, age, jitter);

    // SatNOGS es OTRO proveedor y tiene su propia puerta: si la cache de
    // CelesTrak no sirve, se prueba aunque CelesTrak este bloqueado.
    if (d.action == fetchgate::Action::Blocked) {
        const std::string why = std::string("celestrak: ") + d.reason;
        if (serveCache(group, utc, fetchgate::kMaxFallbackAgeSec, why)) return true;
        return serveSatnogs(utc, why);
    }
    if (d.action == fetchgate::Action::CacheOnly) {
        const std::string why = std::string("red: ") + d.reason;
        if (serveCache(group, utc, fetchgate::kMaxFallbackAgeSec, why)) return true;
        return serveSatnogs(utc, why);
    }
    if (d.action == fetchgate::Action::FetchJittered && d.fetchAtUtc > utc) {
        const std::string why = std::string("red: ") + d.reason;
        if (serveCache(group, utc, fetchgate::kMaxFallbackAgeSec, why)) return true;
        return serveSatnogs(utc, why);
    }

    // --- A partir de aqui se toca la red: un unico intento por pase. ---
    // Write-ahead: el intento se persiste ANTES de la descarga como bloqueo
    // PROVISIONAL. lastAttempt solo no sirve: decide() lo ignora si
    // retryFails == 0, asi que un cierre a mitad volvia a pedir y gastaba la
    // ventana de 2 h. El provisional se retira al terminar (exito o fallo
    // blando) y se sustituye por el bloqueo real si la respuesta no es 200.
    st.lastAttemptUtc = utc;
    st.blockedUntilUtc = utc + fetchgate::kBlockSec;
    st.lastHttp = -1;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        gate_ = st;
    }
    persistState(group.key, st);

    std::string err;
    TleSnapshot snap;
    bool ok = false;

    std::string body;
    const HttpResult res = httpGet(group.url.c_str(), 120L, body);
    if (res.ok) {
        ok = parseCelesTrakCsv(body, utc, snap);
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
        // con backoff 5 -> 60 min. Se retira el bloqueo provisional.
        st.blockedUntilUtc = 0.0;
        st.retryFails = std::min(st.retryFails + 1, 6);
        st.lastHttp = (int)res.http;
        st.lastCurl = res.curl;
    } else {
        st.blockedUntilUtc = 0.0;   // se retira el provisional
        st.retryFails = 0;
        st.lastHttp = 200;
        st.lastCurl = 0;
        st.lastSuccessUtc = utc;
    }

    if (!ok) {
        // Cache (hasta 72 h) antes que SatNOGS: un catalogo completo de hace
        // 12 h es mejor que 1.7k satelites frescos, y no gasta otra cuota.
        TleSnapshot cached;
        if (loadCelestrakCache(group.cache, utc, cached, fetchgate::kMaxFallbackAgeSec)) {
            snap = cached;
            ok = true;
            err += "; servido de cache";
        }
    }

    // El estado de la puerta de CelesTrak se guarda antes de cualquier salida.
    {
        std::lock_guard<std::mutex> lk(mutex_);
        gate_ = st;
    }
    persistState(group.key, st);

    if (!ok) {
        err += "; cache: sin fichero utilizable";
        return serveSatnogs(utc, err);
    }

    // Solo el catalogo completo de CelesTrak va a satellites.tle. El
    // subconjunto de SatNOGS tiene su propia cache: guardarlo aqui lo haria
    // pasar por catalogo activo durante 6 h y taparia el reintento.
    // Se guarda el CUERPO CRUDO: es lo que permite releerlo sin reconstruir un
    // TLE de 5 columnas (imposible con NORAD de 6 digitos).
    if (snap.source == TleSource::Celestrak) {
        saveCache(group.cache, snap, body);   // best-effort: fallar aqui no es fatal
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
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
    }

    printf("[Tle] %s (%s)\n", snap.note.c_str(), sourceLabel(snap.source));
    return true;
}

bool TleAdapter::serveSatnogs(double utc, const std::string& why) {
    TleSnapshot snap;
    std::string err = why;
    if (!fetchSatnogs(utc, snap, err)) {
        std::lock_guard<std::mutex> lk(mutex_);
        error_ = err;              // el snapshot de CelesTrak no se toca
        status_.clear();
        fprintf(stderr, "[Tle] %s\n", err.c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (snapshotEpoch_ != snap.timeUtc || !latest_.valid) ++generation_;
        latest_ = snap;
        snapshotEpoch_ = snap.timeUtc;
        // Catalogo parcial de otro proveedor: se dibuja, pero declarado.
        error_ = err;
        status_.clear();
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
    const double age = celestrakCacheAge(cachePath_, nowUtc);
    const double jitter = jitterFor(celestrakCacheEpoch(cachePath_));
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
    // lastHttp == -1 marca el bloqueo PROVISIONAL del intento en vuelo: no es
    // una decision de politica, asi que el panel no debe pintarlo como
    // "bloqueado" mientras la descarga esta en curso (para eso esta
    // isFetching()). Si el provisional sobrevive a un reinicio, init() lo
    // convierte en 0 (peticion interrumpida) y entonces SI se declara.
    if (gate_.lastHttp == -1) return 0.0;
    return gate_.blockedUntilUtc;
}

std::string TleAdapter::getSourceLabel() const {
    return sourceLabel(source());
}

const char* TleAdapter::sourceLabel(TleSource s) {
    switch (s) {
        case TleSource::Celestrak: return "celestrak";
        case TleSource::SatNogs: return "satnogs";
        case TleSource::SatNogsCache: return "satnogs-cache";
        case TleSource::Cache: return "cache";
        case TleSource::None:
        default: return "sin datos";
    }
}

void TleAdapter::setPathsForTests(const std::filesystem::path& cache,
                                  const std::filesystem::path& satnogsCache,
                                  const std::filesystem::path& state) {
    std::lock_guard<std::mutex> lk(mutex_);
    cachePath_ = cache;
    satnogsCachePath_ = satnogsCache;
    statePath_ = state;
    gate_ = fetchgate::State{};
    gateSatnogs_ = fetchgate::State{};
    latest_ = TleSnapshot{};
    latest_.note = "sin datos";
    snapshotEpoch_ = 0.0;
    generation_ = 0;
    error_.clear();
    status_.clear();
}

void TleAdapter::loadStateForTests(double nowUtc) {
    fetchgate::State a;
    fetchstate::load(statePath_, kKeyActive, a);
    a.blockedUntilUtc = fetchgate::sanitizeBlockedUntil(a.blockedUntilUtc, nowUtc);
    // Mismo criterio que init(): un provisional que sobrevive al reinicio es
    // una peticion interrumpida, no una descarga en curso.
    if (a.lastHttp == -1) a.lastHttp = 0;
    fetchgate::State n;
    fetchstate::load(statePath_, kKeySatnogs, n);
    n.blockedUntilUtc = fetchgate::sanitizeBlockedUntil(n.blockedUntilUtc, nowUtc);
    if (n.lastHttp == -1) n.lastHttp = 0;
    std::lock_guard<std::mutex> lk(mutex_);
    gate_ = a;
    gateSatnogs_ = n;
}
