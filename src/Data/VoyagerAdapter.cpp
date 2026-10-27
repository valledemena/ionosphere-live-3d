// Lote 5 / modulo B: Voyager 1/2 desde JPL Horizons (implementacion; ver .h).
//
// Dos POST al mismo endpoint (uno por sonda), parseo de la tabla observer y
// presupuesto de enlace declarado. Sin red no hay distancia: el snapshot se
// queda invalido (o con los datos buenos anteriores) y el motivo se declara.
#include "Data/VoyagerAdapter.h"

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
#include <thread>

namespace {

const char* const kHorizonsUrl = "https://ssd.jpl.nasa.gov/api/horizons.api";
const char* const kCachePath = "cache/voyager.txt";

// Caducidad del cache servido. El fetch en vivo NO usa este TTL: el kick
// manual del panel debe refrescar de verdad (dos POST pequenos a JPL).
constexpr double kCacheTtlSec = 6.0 * 3600.0;
// Tolerancia por si el reloj local va unos minutos atras respecto a la cache.
constexpr double kFutureSlackSec = 600.0;
// Banda de rango geocentrico de las dos Voyager (AU). Descarta filas de
// cualquier otro cuerpo si la respuesta viniera de otro target.
constexpr double kAuMin = 100.0;
constexpr double kAuMax = 200.0;
// El barrido del pulso se comprime a 10 s de animacion (factor xN visible).
constexpr double kSweepMs = 10000.0;
constexpr size_t kMaxRows = 512;

// Constantes del modelo declarado (publicas JPL, copiadas del demo web).
constexpr double VOYAGER_FREQ_HZ = 8.42e9;  // banda X de bajada
constexpr double VOYAGER_TX_W = 23.0;       // TWTA de la nave
constexpr double VOYAGER_DISH_M = 3.7;      // antena de la nave
constexpr double DSN_DISH_M = 70.0;         // antena DSN de 70 m
constexpr double ETA_TX = 0.55;             // eficiencia tipica (declarada)
constexpr double ETA_RX = 0.65;             // eficiencia tipica (declarada)
constexpr double C_MS = 299792458.0;
constexpr double AU_KM = 1.496e8;

std::once_flag gCurlInit;

// Dias desde 1970-01-01 (Howard Hinnant), misma aritmetica que los demas
// adaptadores del proyecto: evita depender de timegm/_mkgmtime.
long long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

// "Oct" / "OCT" / "oct" -> 10 (1..12). Exige exactamente 3 letras.
bool monthFromName(const char* p, int& month1) {
    static const char* const kNames[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; ++i) {
        bool eq = true;
        for (int k = 0; k < 3; ++k) {
            if (std::tolower((unsigned char)p[k]) !=
                std::tolower((unsigned char)kNames[i][k])) {
                eq = false;
                break;
            }
        }
        if (eq) {
            month1 = i + 1;
            return true;
        }
    }
    return false;
}

bool isSpaceCh(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Una fila "YYYY-Mon-DD HH:MM  <au>  <deldot> ...". Se acepta la fecha en
// cualquier punto de la linea (las cabeceras y la cola de documentacion no
// encajan en la forma) y se exige rango en (100, 200) AU.
bool parseRowLine(const std::string& line, VoyagerFix& out) {
    const size_t n = line.size();
    size_t i = 0;
    while (i < n && isSpaceCh(line[i])) ++i;
    if (i + 11 > n) return false;  // "YYYY-Mon-DD"
    int year = 0;
    for (int k = 0; k < 4; ++k) {
        if (!isDigit(line[i + (size_t)k])) return false;
        year = year * 10 + (line[i + (size_t)k] - '0');
    }
    if (line[i + 4] != '-') return false;
    int month1 = 0;
    if (!monthFromName(line.c_str() + i + 5, month1)) return false;
    if (line[i + 8] != '-') return false;
    if (!isDigit(line[i + 9]) || !isDigit(line[i + 10])) return false;
    const int day = (line[i + 9] - '0') * 10 + (line[i + 10] - '0');

    size_t p = i + 11;
    while (p < n && isSpaceCh(line[p])) ++p;
    if (p + 5 > n) return false;
    if (!isDigit(line[p]) || !isDigit(line[p + 1]) || line[p + 2] != ':' ||
        !isDigit(line[p + 3]) || !isDigit(line[p + 4]))
        return false;
    const int hour = (line[p] - '0') * 10 + (line[p + 1] - '0');
    const int minute = (line[p + 3] - '0') * 10 + (line[p + 4] - '0');
    p += 5;
    if (p >= n || !isSpaceCh(line[p])) return false;
    while (p < n && isSpaceCh(line[p])) ++p;
    if (p >= n) return false;

    if (year < 1900 || year > 2200 || day < 1 || day > 31 || hour > 23 || minute > 59)
        return false;

    // Columna 1 = rango en AU. strtod se para en el espacio siguiente.
    const char* start = line.c_str() + p;
    char* end = nullptr;
    const double au = std::strtod(start, &end);
    if (end == start || !std::isfinite(au)) return false;
    // Una fila sin numero o con basura detras ("n.a.") no es una fila.
    if (end < line.c_str() + n) {
        char c = *end;
        if (!isSpaceCh(c)) return false;
    }
    if (!(au > kAuMin && au < kAuMax)) return false;

    char date[32];
    std::snprintf(date, sizeof(date), "%04d-%c%c%c-%02d %02d:%02d", year,
                  (char)std::toupper((unsigned char)line[i + 5]),
                  (char)std::tolower((unsigned char)line[i + 6]),
                  (char)std::tolower((unsigned char)line[i + 7]), day, hour, minute);

    out = VoyagerFix{};
    out.au = au;
    out.timeUtc = (double)daysFromCivil(year, month1, day) * 86400.0 +
                  (double)(hour * 3600 + minute * 60);
    out.date = date;
    out.valid = true;
    return true;
}

std::string utcDayString(double epoch) {
    const std::time_t tt = (std::time_t)epoch;
    std::tm g{};
#if defined(_WIN32)
    if (gmtime_s(&g, &tt) != 0) return "1970-01-01";
#else
    if (gmtime_r(&tt, &g) == nullptr) return "1970-01-01";
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", g.tm_year + 1900, g.tm_mon + 1,
                  g.tm_mday);
    return buf;
}

// Cuerpo form-urlencoded del POST. Ventana: 3 dias antes (para tener siempre
// una fila pasada) y 1 dia despues (para que la mas cercana exista aunque el
// fetch llegue justo antes de medianoche).
std::string horizonsBody(const char* command, double utc) {
    const double base =
        (std::isfinite(utc) && utc > 0.0) ? utc : (double)std::time(nullptr);
    const std::string start = utcDayString(base - 3.0 * 86400.0);
    const std::string stop = utcDayString(base + 1.0 * 86400.0);
    std::string b;
    b += "format=text&COMMAND='";
    b += command;
    b += "'";
    b += "&OBJ_DATA='NO'&MAKE_EPHEM='YES'&EPHEM_TYPE='OBSERVER'&CENTER='500@399'";
    b += "&START_TIME='";
    b += start;
    b += "'";
    b += "&STOP_TIME='";
    b += stop;
    b += "'";
    b += "&STEP_SIZE='1d'&QUANTITIES='20'";
    return b;
}

// Magnitudes derivadas del rango (mismo modelo declarado que el demo web).
void fillDerived(VoyagerProbe& p) {
    if (!p.fix.valid || !(p.fix.au > 0.0)) {
        p.lightHours = 0.0;
        p.receivedW = 0.0;
        p.pulseFactor = 1.0;
        return;
    }
    const double lt = VoyagerAdapter::lightTimeSec(p.fix.au);
    p.lightHours = lt / 3600.0;
    p.receivedW = VoyagerAdapter::friisReceivedW(p.fix.au);
    p.pulseFactor = VoyagerAdapter::pulseLapse(lt, kSweepMs);
}

}  // namespace

void VoyagerAdapter::init() {
    std::call_once(gCurlInit, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
    VoyagerSnapshot cached;
    bool have = false;
    {
        // Cache joven: el panel tiene algo que ensenar antes del primer fetch,
        // declarado como "cached" (nunca como live).
        const double now = (double)std::time(nullptr);
        have = loadCache(kCachePath, now, cached);
    }
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (have) {
            latest_ = cached;
            lastFetchUtc_ = cached.timeUtc;
        }
    }
    printf("[OK] VoyagerAdapter initialized (JPL Horizons -31/-32%s)\n",
           have ? ", cache served" : "");
}

size_t VoyagerAdapter::writeCallback(void* contents, size_t size, size_t nmemb,
                                     std::string* userp) {
    const size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool VoyagerAdapter::httpPost(const std::string& body, std::string& out, std::string& err) {
    std::call_once(gCurlInit, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* curl = curl_easy_init();
    if (!curl) {
        err = "curl: init failed";
        return false;
    }
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_URL, kHorizonsUrl);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");

    const CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        err = std::string("horizons: ") + curl_easy_strerror(res);
        return false;
    }
    if (http != 200) {
        err = "horizons: HTTP " + std::to_string(http);
        return false;
    }
    if (out.empty()) {
        err = "horizons: empty body";
        return false;
    }
    return true;
}

bool VoyagerAdapter::parseRows(const std::string& text, std::vector<VoyagerFix>& rows) {
    rows.clear();
    if (text.empty()) return false;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        const size_t end = (nl == std::string::npos) ? text.size() : nl;
        if (end > pos) {
            const std::string line = text.substr(pos, end - pos);
            VoyagerFix fix;
            if (parseRowLine(line, fix)) {
                rows.push_back(fix);
                if (rows.size() >= kMaxRows) break;
            }
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return !rows.empty();
}

bool VoyagerAdapter::pickCurrentRow(const std::vector<VoyagerFix>& rows, double nowUtc,
                                    VoyagerFix& out) {
    const VoyagerFix* bestPast = nullptr;
    const VoyagerFix* earliest = nullptr;
    for (const VoyagerFix& r : rows) {
        if (!r.valid || !std::isfinite(r.timeUtc) || !(r.timeUtc > 0.0)) continue;
        if (!earliest || r.timeUtc < earliest->timeUtc) earliest = &r;
        if (r.timeUtc <= nowUtc && (!bestPast || r.timeUtc > bestPast->timeUtc))
            bestPast = &r;
    }
    // Nunca una fila futura. Si todas lo son, la mas temprana (la primera de
    // una tabla Horizons ascendente): declarado, no adivinado.
    const VoyagerFix* pick = bestPast ? bestPast : earliest;
    if (!pick) return false;
    out = *pick;
    out.valid = true;
    return true;
}

bool VoyagerAdapter::parseHorizons(const std::string& text, VoyagerSnapshot& out) {
    out = VoyagerSnapshot{};
    const double now = (double)std::time(nullptr);
    out.timeUtc = now;

    std::vector<VoyagerFix> rows;
    if (!parseRows(text, rows)) {
        out.note = "no Voyager row in the 100-200 AU band";
        return false;
    }
    VoyagerFix fix;
    if (!pickCurrentRow(rows, now, fix)) {
        out.note = "no usable row";
        return false;
    }
    out.v1.fix = fix;
    fillDerived(out.v1);
    out.valid = true;
    out.note = "Voyager 1 " + fix.date;
    return true;
}

double VoyagerAdapter::friisReceivedW(double rAu) {
    if (!std::isfinite(rAu) || !(rAu > 0.0)) return 0.0;
    const double lambda = C_MS / VOYAGER_FREQ_HZ;
    const double gt = ETA_TX * std::pow((3.14159265358979323846 * VOYAGER_DISH_M) / lambda, 2.0);
    const double gr = ETA_RX * std::pow((3.14159265358979323846 * DSN_DISH_M) / lambda, 2.0);
    const double r = rAu * AU_KM * 1000.0;
    const double path = lambda / (4.0 * 3.14159265358979323846 * r);
    return VOYAGER_TX_W * gt * gr * path * path;
}

double VoyagerAdapter::lightTimeSec(double rAu) {
    if (!std::isfinite(rAu) || !(rAu > 0.0)) return 0.0;
    return (rAu * AU_KM * 1000.0) / C_MS;
}

double VoyagerAdapter::pulseLapse(double lightSec, double sweepMs) {
    if (!std::isfinite(lightSec) || !(lightSec > 0.0) || !(sweepMs > 0.0)) return 1.0;
    const double n = std::round((lightSec * 1000.0) / sweepMs);
    return std::max(1.0, n);
}

VoyagerProbe VoyagerAdapter::fetchProbe(double utc, const char* command, std::string& err,
                                        std::string& raw) {
    VoyagerProbe p;
    err.clear();
    raw.clear();
    const std::string body = horizonsBody(command, utc);
    std::string text;
    if (!httpPost(body, text, err)) return p;
    raw = text;

    std::vector<VoyagerFix> rows;
    if (!parseRows(text, rows)) {
        err = "no row in the 100-200 AU band";
        return p;
    }
    VoyagerFix fix;
    if (!pickCurrentRow(rows, utc, fix)) {
        err = "no row at or before now";
        return p;
    }
    p.fix = fix;
    fillDerived(p);
    return p;
}

bool VoyagerAdapter::saveCache(const std::string& path, const VoyagerSnapshot& snap,
                               const std::string& rawV1, const std::string& rawV2) {
    if (!snap.valid) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);

    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        char head[96];
        std::snprintf(head, sizeof(head), "# voyager-cache v1 fetch=%.1f\n", snap.timeUtc);
        f << head;
        if (snap.v1.fix.valid) {
            char ln[128];
            std::snprintf(ln, sizeof(ln), "VOY1 %.1f %.6f %s\n", snap.v1.fix.timeUtc,
                          snap.v1.fix.au, snap.v1.fix.date.c_str());
            f << ln;
        }
        if (snap.v2.fix.valid) {
            char ln[128];
            std::snprintf(ln, sizeof(ln), "VOY2 %.1f %.6f %s\n", snap.v2.fix.timeUtc,
                          snap.v2.fix.au, snap.v2.fix.date.c_str());
            f << ln;
        }
        f << "# --- V1 raw\n";
        f << rawV1;
        if (!rawV1.empty() && rawV1.back() != '\n') f << '\n';
        f << "# --- V2 raw\n";
        f << rawV2;
        if (!rawV2.empty() && rawV2.back() != '\n') f << '\n';
        if (!f.good()) return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
    }
    return !ec;
}

bool VoyagerAdapter::loadCache(const std::string& path, double nowUtc, VoyagerSnapshot& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    const std::string text((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    if (text.empty()) return false;

    VoyagerSnapshot snap;
    bool haveHeader = false;
    double fetchEpoch = 0.0;
    double v1Au = 0.0, v1Epoch = 0.0;
    double v2Au = 0.0, v2Epoch = 0.0;
    std::string v1Date, v2Date;

    size_t pos = 0;
    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        const size_t end = (nl == std::string::npos) ? text.size() : nl;
        std::string line = text.substr(pos, end - pos);
        pos = (nl == std::string::npos) ? text.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.compare(0, 3, "# ---") == 0) break;  // empieza el bloque crudo
        if (!haveHeader) {
            if (line.compare(0, 19, "# voyager-cache v1 ") != 0) return false;
            const size_t ep = line.find("fetch=");
            if (ep == std::string::npos) return false;
            fetchEpoch = std::atof(line.c_str() + ep + 6);
            if (!(fetchEpoch > 0.0)) return false;
            haveHeader = true;
            continue;
        }
        if (line.empty() || line[0] == '#') continue;
        const bool isV1 = line.compare(0, 5, "VOY1 ") == 0;
        const bool isV2 = line.compare(0, 5, "VOY2 ") == 0;
        if (!isV1 && !isV2) continue;
        // "VOY1 <epoch> <au> <fecha...>"
        const char* q = line.c_str() + 5;
        char* e1 = nullptr;
        const double rowEpoch = std::strtod(q, &e1);
        if (e1 == q) continue;
        char* e2 = nullptr;
        const double au = std::strtod(e1, &e2);
        if (e2 == e1) continue;
        std::string date(e2);
        while (!date.empty() && (date.front() == ' ' || date.front() == '\t')) date.erase(0, 1);
        if (date.empty()) continue;
        if (!std::isfinite(rowEpoch) || !(rowEpoch > 0.0)) continue;
        if (!std::isfinite(au) || !(au > kAuMin && au < kAuMax)) continue;
        if (isV1) {
            v1Au = au;
            v1Epoch = rowEpoch;
            v1Date = date;
        } else {
            v2Au = au;
            v2Epoch = rowEpoch;
            v2Date = date;
        }
    }
    if (!haveHeader || (v1Date.empty() && v2Date.empty())) return false;

    // Caducidad: 6 h y nada del futuro (salvo el desfase de reloj tolerado).
    if (fetchEpoch > nowUtc + kFutureSlackSec) return false;
    const double age = nowUtc - fetchEpoch;
    if (age > kCacheTtlSec) return false;

    snap.timeUtc = fetchEpoch;
    if (!v1Date.empty()) {
        snap.v1.fix.au = v1Au;
        snap.v1.fix.timeUtc = v1Epoch;
        snap.v1.fix.date = v1Date;
        snap.v1.fix.valid = true;
        fillDerived(snap.v1);
    }
    if (!v2Date.empty()) {
        snap.v2.fix.au = v2Au;
        snap.v2.fix.timeUtc = v2Epoch;
        snap.v2.fix.date = v2Date;
        snap.v2.fix.valid = true;
        fillDerived(snap.v2);
    }
    snap.valid = snap.v1.fix.valid || snap.v2.fix.valid;
    if (!snap.valid) return false;
    snap.fromCache = true;
    char note[96];
    std::snprintf(note, sizeof(note), "cache: served %s, age %.1f h",
                  (snap.v1.fix.valid && snap.v2.fix.valid) ? "V1+V2"
                                                           : (snap.v1.fix.valid ? "V1" : "V2"),
                  age > 0.0 ? age / 3600.0 : 0.0);
    snap.note = note;
    out = snap;
    return true;
}

bool VoyagerAdapter::fetch(double utc) {
    const double base =
        (std::isfinite(utc) && utc > 0.0) ? utc : (double)std::time(nullptr);

    std::string errV1, errV2, rawV1, rawV2;
    VoyagerProbe p1 = fetchProbe(base, "-31", errV1, rawV1);   // Voyager 1
    VoyagerProbe p2 = fetchProbe(base, "-32", errV2, rawV2);   // Voyager 2

    const bool ok1 = p1.fix.valid;
    const bool ok2 = p2.fix.valid;

    if (!ok1 && !ok2) {
        const std::string both = "Voyager 1: " + errV1 + "; Voyager 2: " + errV2;
        VoyagerSnapshot cached;
        const bool haveCache = loadCache(kCachePath, base, cached);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!latest_.valid && haveCache) {
                // Sin datos previos en memoria: el cache joven es lo unico
                // honesto que se puede servir, y se rotula como tal.
                latest_ = cached;
                lastFetchUtc_ = cached.timeUtc;
                latest_.note += "; refresh failed: " + both;
            }
            // Con datos previos buenos NO se toca el snapshot (stale-while-
            // revalidate): solo se declara el fallo.
            error_ = both;
        }
        fprintf(stderr, "[Voyager] %s\n", both.c_str());
        return haveCache || getSnapshot().valid;
    }

    VoyagerSnapshot snap;
    snap.timeUtc = base;
    snap.v1 = p1;
    snap.v2 = p2;
    snap.valid = true;
    snap.fromCache = false;
    if (ok1 && ok2) {
        snap.note = "live: Voyager 1 + Voyager 2";
    } else if (ok1) {
        snap.note = "live: Voyager 1 only, no Voyager 2 range";
    } else {
        snap.note = "live: Voyager 2 only, no Voyager 1 range";
    }

    saveCache(kCachePath, snap, rawV1, rawV2);  // best-effort: fallar no es fatal

    {
        std::lock_guard<std::mutex> lk(mutex_);
        latest_ = snap;
        lastFetchUtc_ = base;
        // Si falta una sonda, el motivo queda en error_ para que el panel lo
        // pueda ensenar junto al hueco (no hay dato, no se rellena).
        error_ = ok1 && ok2 ? std::string() : (ok1 ? ("Voyager 2: " + errV2)
                                                   : ("Voyager 1: " + errV1));
    }

    if (ok1 && ok2) {
        printf("[Voyager] V1 %.6f AU (%s) | V2 %.6f AU (%s)\n", p1.fix.au,
               p1.fix.date.c_str(), p2.fix.au, p2.fix.date.c_str());
    } else {
        printf("[Voyager] %s\n", snap.note.c_str());
    }
    return true;
}

void VoyagerAdapter::fetchAsync(double utc) {
    if (fetching_.exchange(true)) return;  // ya hay una descarga en curso
    std::thread([this, utc]() {
        fetch(utc);
        fetching_.store(false);
    }).detach();
}

VoyagerSnapshot VoyagerAdapter::getSnapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return latest_;
}

double VoyagerAdapter::getLastFetchTime() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return lastFetchUtc_;
}

std::string VoyagerAdapter::getErrorMessage() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return error_;
}
