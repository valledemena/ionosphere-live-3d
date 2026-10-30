#include "AeAdapter.h"

#include <curl/curl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Utils/Paths.h"  // cache configurable (Paths.h)

// Fuente, formato real y limites: ver la cabecera de AeAdapter.h.
// Resumen verificado 2026-10-27: walk-back desde el dia UTC -> el fichero mas
// reciente era 2026/10/11/ae261011 (dayOffset 16), 200 OK, 9624 bytes, 24
// lineas de 400 chars con 60 minutos + media horaria (61 campos de 6 chars).

namespace {

const char* const kBaseUrl = "https://wdc.kugi.kyoto-u.ac.jp/ae_realtime/data_dir";
const char* const kUserAgent = "IonosphereLive3D/1.0";

// Cache configurable (Paths.h): se resuelve EN EL PUNTO DE USO, no como
// constante de arranque, porque init() restaura ficheros y puede correr
// despues de que la app aplique el "cacheDir" de settings.cfg.
std::string aeCachePath() { return paths::cacheRootString() + "/ae.txt"; }

constexpr int kMaxBackDays = 35;          // tope de walk-back (dias hacia atras)
constexpr size_t kMaxSamples = 1440;      // 24 h de 1 minuto
constexpr size_t kMaxBytes = 1024u * 1024u;   // 1 MB
constexpr long kTimeoutSec = 20L;
constexpr long kConnectSec = 10L;

// ---- Aritmetica de fecha (Howard Hinnant, dias desde 1970-01-01) ----

long long daysFromCivil(int y, int m, int d) {
    y -= (m <= 2) ? 1 : 0;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

void civilFromDays(long long z, int& y, int& m, int& d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long yy = (long long)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yy + (m <= 2 ? 1 : 0));
}

void ymdFromEpoch(double utc, int& y, int& m, int& d) {
    const long long days = (long long)std::floor(utc / 86400.0);
    civilFromDays(days, y, m, d);
}

// ---- Utilidades de texto ----

void splitTokens(const std::string& line, std::vector<std::string>& toks) {
    toks.clear();
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        const size_t b = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
        if (i > b) toks.push_back(line.substr(b, i - b));
    }
}

bool parseIntStrict(const std::string& s, long& v) {
    if (s.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '+' || s[0] == '-') {
        neg = (s[0] == '-');
        i = 1;
        if (s.size() == 1) return false;
    }
    long acc = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        acc = acc * 10 + (s[i] - '0');
        if (acc > 99999999L) return false;   // el AE real vive en |x| < 3000 nT
    }
    v = neg ? -acc : acc;
    return true;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// "YYYY-MM-DD" -> y/m/d. false si no encaja.
bool parseYmd(const std::string& s, int& y, int& m, int& d) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
        if (!isDigit(s[i])) return false;
    }
    y = std::atoi(s.substr(0, 4).c_str());
    m = std::atoi(s.substr(5, 2).c_str());
    d = std::atoi(s.substr(8, 2).c_str());
    return m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

// Destino del callback de curl con tope de tamano (aborta la transferencia si
// el servidor intenta pasar de kMaxBytes).
struct Sink {
    std::string* out = nullptr;
    bool overflow = false;
};

}  // namespace

// ---------------------------------------------------------------------------
// API publica
// ---------------------------------------------------------------------------

void AeAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::lock_guard<std::mutex> lk(mutex_);
    latest_ = AeSnapshot{};
    latest_.note = "sin datos";
}

std::string AeAdapter::fileName(int year, int month, int day) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "ae%02d%02d%02d", year % 100, month, day);
    return buf;
}

std::string AeAdapter::buildUrl(int year, int month, int day) {
    char buf[192];
    std::snprintf(buf, sizeof(buf), "%s/%04d/%02d/%02d/%s", kBaseUrl, year, month, day,
                  fileName(year, month, day).c_str());
    return buf;
}

bool AeAdapter::parseAeFile(const std::string& text, int year, int month, int day,
                            AeSnapshot& out) {
    out = AeSnapshot{};
    if (text.empty()) {
        out.note = "fichero vacio";
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        out.note = "fecha invalida";
        return false;
    }

    const double midnight = (double)daysFromCivil(year, month, day) * 86400.0;

    std::vector<AeSample> acc;
    acc.reserve(1500);
    std::vector<std::string> toks;
    bool headerSeen = false;
    int dropped = 0;

    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        const size_t end = (nl == std::string::npos) ? text.size() : nl;
        std::string line = text.substr(pos, end - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

        // CR de Windows y relleno final.
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (line.empty()) continue;

        // Cabecera: la primera linea con contenido debe empezar por "AEALAOAU".
        // En las siguientes, una cabecera distinta es una linea rota (se cuenta).
        if (!headerSeen) {
            if (line.size() < 8 || line.compare(0, 8, "AEALAOAU") != 0) {
                out.note = "cabecera distinta de AEALAOAU";
                return false;
            }
            headerSeen = true;
        } else if (line.size() < 8 || line.compare(0, 8, "AEALAOAU") != 0) {
            ++dropped;
            continue;
        }

        splitTokens(line, toks);
        // 3 campos de cabecera (AEALAOAU, yyMMddEHHAA, calidad) + valores.
        if (toks.size() < 4) {
            ++dropped;
            continue;
        }

        // Hora de "EHH" en el segundo campo ("261011E00AE").
        const std::string& stamp = toks[1];
        bool stampOk = stamp.size() >= 9;
        for (int k = 0; stampOk && k < 6; ++k) stampOk = isDigit(stamp[(size_t)k]);
        if (stampOk) stampOk = (stamp[6] == 'E') && isDigit(stamp[7]) && isDigit(stamp[8]);
        if (!stampOk) {
            ++dropped;
            continue;
        }
        const int hh = (stamp[7] - '0') * 10 + (stamp[8] - '0');
        // Kyoto publica 0..23; se toleran 24..47 como horas despues de las
        // 00:00 del dia dado (ficheros que cruzan medianoche).
        if (hh > 47) {
            ++dropped;
            continue;
        }

        // 60 minutos; 61 = 60 + media horaria (se descarta, NUNCA es un minuto).
        const size_t nvals = toks.size() - 3;
        if (nvals != 60 && nvals != 61) {
            ++dropped;
            continue;
        }

        int vals[60];
        bool valsOk = true;
        for (size_t k = 0; k < 60; ++k) {
            long v = 0;
            if (!parseIntStrict(toks[3 + k], v)) {
                valsOk = false;
                break;
            }
            vals[k] = (int)v;
        }
        if (!valsOk) {
            ++dropped;
            continue;
        }

        const double hourStart = midnight + (double)hh * 3600.0;
        for (int mi = 0; mi < 60; ++mi) {
            AeSample s;
            s.timeUtc = hourStart + (double)mi * 60.0;
            s.ae = (float)vals[mi];
            // AL/AO/AU no viajan en este producto: quedan a 0.
            s.valid = true;
            acc.push_back(s);
        }
    }

    if (!headerSeen) {
        out.note = "sin lineas de datos";
        return false;
    }
    if (acc.empty()) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "sin muestras validas (%d lineas descartadas)", dropped);
        out.note = buf;
        return false;
    }

    std::stable_sort(acc.begin(), acc.end(),
                     [](const AeSample& a, const AeSample& b) { return a.timeUtc < b.timeUtc; });
    if (acc.size() > kMaxSamples) {
        acc.erase(acc.begin(), acc.begin() + (std::ptrdiff_t)(acc.size() - kMaxSamples));
    }

    out.samples = std::move(acc);
    out.timeUtc = out.samples.back().timeUtc;
    out.valid = true;
    char buf[96];
    if (dropped > 0) {
        std::snprintf(buf, sizeof(buf), "%d muestras (%d lineas descartadas)",
                      (int)out.samples.size(), dropped);
    } else {
        std::snprintf(buf, sizeof(buf), "%d muestras", (int)out.samples.size());
    }
    out.note = buf;
    return true;
}

bool AeAdapter::writeCacheFile(const std::string& path, double epoch,
                               const std::string& sourceDay, const std::string& body) {
    if (path.empty() || body.empty() || !std::isfinite(epoch) || !(epoch > 0.0)) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);

    // "AE1 <epoch> <YYYY-MM-DD>\n" + cuerpo crudo.
    char num[48];
    std::snprintf(num, sizeof(num), "%.3f", epoch);
    std::string head = "AE1 ";
    head += num;
    if (!sourceDay.empty()) {
        head += ' ';
        head += sourceDay;
    }
    head += '\n';

    // Temporal + rename: nunca dejar un cache a medias.
    const std::string tmp = path + ".tmp";
    FILE* fp = std::fopen(tmp.c_str(), "wb");
    if (!fp) return false;
    bool ok = std::fwrite(head.data(), 1, head.size(), fp) == head.size();
    ok = ok && std::fwrite(body.data(), 1, body.size(), fp) == body.size();
    if (std::fclose(fp) != 0) ok = false;
    if (!ok) {
        fs::remove(tmp, ec);
        return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool AeAdapter::readCacheFile(const std::string& path, std::string& body,
                              double& epoch, std::string& sourceDay) {
    body.clear();
    epoch = 0.0;
    sourceDay.clear();

    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    std::string all;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
    std::fclose(fp);

    if (all.compare(0, 4, "AE1 ") != 0) return false;
    const size_t nl = all.find('\n');
    if (nl == std::string::npos) return false;
    const std::string head = all.substr(4, nl - 4);

    char* endp = nullptr;
    const double ep = std::strtod(head.c_str(), &endp);
    if (endp == head.c_str() || !std::isfinite(ep) || !(ep > 0.0)) return false;
    if (*endp != '\0' && *endp != ' ') return false;
    while (*endp == ' ') ++endp;
    const std::string dayStr(endp);
    if (!dayStr.empty()) {
        int y = 0, m = 0, d = 0;
        if (!parseYmd(dayStr, y, m, d)) return false;
    }

    body = all.substr(nl + 1);
    if (body.empty()) return false;
    epoch = ep;
    sourceDay = dayStr;
    return true;
}

// ---------------------------------------------------------------------------
// Red
// ---------------------------------------------------------------------------

size_t AeAdapter::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    Sink* sink = static_cast<Sink*>(userp);
    if (sink == nullptr || sink->out == nullptr) return 0;
    if (sink->out->size() + total > kMaxBytes) {
        sink->overflow = true;
        return 0;   // aborta la transferencia (demasiado grande)
    }
    sink->out->append((const char*)contents, total);
    return total;
}

bool AeAdapter::fetchUrl(const std::string& url, std::string& body, long& httpCode) {
    body.clear();
    httpCode = 0;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        httpCode = -1;
        return false;
    }
    Sink sink;
    sink.out = &body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectSec);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, (long)kMaxBytes);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);

    const CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        httpCode = -1;   // fallo de transporte (DNS, timeout, tope 1 MB...)
        body.clear();
        return false;
    }
    httpCode = http;
    if (http != 200) {
        body.clear();
        return false;
    }
    return true;
}

bool AeAdapter::fetch(double utc) {
    int y = 0, m = 0, d = 0;
    ymdFromEpoch(utc, y, m, d);
    const long long todayIdx = daysFromCivil(y, m, d);

    std::string lastErr = "sin respuesta";
    std::string lastDay;

    for (int off = 0; off < kMaxBackDays; ++off) {
        int yy = 0, mm = 0, dd = 0;
        civilFromDays(todayIdx - off, yy, mm, dd);
        char dayStr[16];
        std::snprintf(dayStr, sizeof(dayStr), "%04d-%02d-%02d", yy, mm, dd);
        lastDay = dayStr;

        std::string body;
        long http = 0;
        if (!fetchUrl(buildUrl(yy, mm, dd), body, http)) {
            if (http < 0) lastErr = std::string("curl fail (") + dayStr + ")";
            else lastErr = "HTTP " + std::to_string(http) + " (" + dayStr + ")";
            continue;
        }

        AeSnapshot snap;
        if (!parseAeFile(body, yy, mm, dd, snap)) {
            // 200 con formato distinto: la fuente cambio. No se sigue bajando
            // dias a ciegas (eso escondería el problema).
            lastErr = "parse " + std::string(dayStr) + ": " + snap.note;
            break;
        }

        snap.dayOffset = off;
        snap.sourceDay = dayStr;
        snap.fromCache = false;
        snap.note += ", dia ";
        snap.note += dayStr;
        if (off == 0) {
            snap.note += " (hoy)";
        } else {
            snap.note += " (" + std::to_string(off) + " d atras)";
        }

        // Cache best-effort: nunca fatal.
        writeCacheFile(aeCachePath(), utc, dayStr, body);

        {
            std::lock_guard<std::mutex> lk(mutex_);
            latest_ = snap;
            lastFetchUtc_ = utc;
            error_.clear();
        }
        return true;
    }

    // Red caida o sin fichero en 35 dias: cache de disco declarada, solo como
    // sustituto si no habia datos buenos previos (stale-while-revalidate).
    {
        std::string cached, cachedDay;
        double cachedEpoch = 0.0;
        if (readCacheFile(aeCachePath(), cached, cachedEpoch, cachedDay) && !cachedDay.empty()) {
            int cy = 0, cm = 0, cd = 0;
            if (parseYmd(cachedDay, cy, cm, cd)) {
                AeSnapshot cs;
                if (parseAeFile(cached, cy, cm, cd, cs)) {
                    cs.fromCache = true;
                    cs.sourceDay = cachedDay;
                    cs.dayOffset = (int)(todayIdx - daysFromCivil(cy, cm, cd));
                    double ageSec = utc - cachedEpoch;
                    if (ageSec < 0.0) ageSec = 0.0;
                    char age[48];
                    std::snprintf(age, sizeof(age), "%.1f h", ageSec / 3600.0);
                    cs.note += ", cache ";
                    cs.note += cachedDay;
                    cs.note += " (descargada hace ";
                    cs.note += age;
                    cs.note += ")";
                    {
                        std::lock_guard<std::mutex> lk(mutex_);
                        if (!latest_.valid) latest_ = cs;
                        error_ = lastErr + " (cache " + age + ")";
                        lastFetchUtc_ = utc;
                    }
                    std::printf("[Ae] sin fichero nuevo (%s); sirvo cache %s\n",
                                lastErr.c_str(), cachedDay.c_str());
                    return true;
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!latest_.valid) {
            AeSnapshot bad;
            bad.valid = false;
            bad.fromCache = false;
            bad.dayOffset = -1;
            bad.note = "error: " + lastErr;
            latest_ = bad;
        }
        error_ = lastErr + " tras " + std::to_string(kMaxBackDays) + " dias";
        lastFetchUtc_ = utc;
    }
    std::fprintf(stderr, "[Ae] %s (probados %d dias, ultimo %s)\n", lastErr.c_str(),
                 kMaxBackDays, lastDay.c_str());
    return false;
}

void AeAdapter::fetchAsync(double utc) {
    if (fetching_.exchange(true)) return;   // ya en curso
    std::thread([this, utc]() {
        fetch(utc);
        fetching_.store(false);
    }).detach();
}

AeSnapshot AeAdapter::getSnapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return latest_;
}

double AeAdapter::getLastFetchTime() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return lastFetchUtc_;
}

std::string AeAdapter::getErrorMessage() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return error_;
}
