// M-IRTAM-F2 (P4): adaptador gambit-coeffs (implementacion; ver .h).
#include "Data/IrtamCoeffAdapter.h"

#include "Data/IrtamCoeffCache.h"  // P2: persistencia familia irtamc_
#include "Data/IrtamCoeffParse.h"  // P1a: validacion estructural
#include "Data/LgdcPacing.h"       // P3: gate lgdc compartido (C1)
#include "Data/LgdcTrace.h"        // Ciclo traza: log stderr con timestamp (solo-log)

#include <curl/curl.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace irtamfetch {

// Reloj del gate: ms desde Unix epoch (misma base que GiroAdapter::msNow).
static long long wallMs() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

double quantizeSlot(double epoch) {
    return std::floor(epoch / kSlotSec) * kSlotSec;
}

double windowEndFor(double nowUtc) {
    return quantizeSlot(nowUtc - kGambitLagSec);
}

std::string formatGambitTime(double epoch) {
    const std::time_t tt = (std::time_t)(long long)epoch;
    std::tm g{};
#ifdef _WIN32
    gmtime_s(&g, &tt);
#else
    gmtime_r(&tt, &g);
#endif
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d.%02d.%02dT%02d:%02d",
                  g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
                  g.tm_hour, g.tm_min);
    return std::string(buf);
}

double dataAgeMin(double dataEpoch, double nowUtc) {
    if (dataEpoch <= 0.0) return -1.0;
    return (nowUtc - dataEpoch) / 60.0;
}

std::vector<FetchTarget> planMissing(const CacheHave& have, double windowEnd) {
    std::vector<FetchTarget> out;
    for (int pi = 0; pi < 4; pi++) {  // F2-B0B1-R1: 4 params param-major
        const std::string param(kParams[pi]);
        const std::vector<double>* present = nullptr;
        const auto it = have.epochs.find(param);
        if (it != have.epochs.end()) present = &it->second;
        for (int s = kSlots - 1; s >= 0; s--) {  // oldest-first
            const double slot = windowEnd - (double)s * kSlotSec;
            bool found = false;
            if (present) {
                for (double e : *present) {
                    if (std::fabs(e - slot) < 1.0) { found = true; break; }
                }
            }
            if (!found) out.push_back(FetchTarget{param, slot});
        }
    }
    return out;
}

void IrtamCoeffAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    printf("[OK] IrtamCoeffAdapter initialized\n");
}

std::string IrtamCoeffAdapter::urlFor(const std::string& param, double epoch) {
    return "https://lgdc.uml.edu/rix/gambit-coeffs?time=" +
           formatGambitTime(epoch) + "&charName=" + param;
}

size_t IrtamCoeffAdapter::writeCallback(void* contents, size_t size,
                                        size_t nmemb, std::string* userp) {
    const size_t total = size * nmemb;
    userp->append((const char*)contents, total);
    return total;
}

bool IrtamCoeffAdapter::fetchURL(const std::string& url,
                                 std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
    lgdcpacing::recordLaunch(wallMs());  // P3 gate: REGISTRO al lanzar (C1)
    const CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || http != 200) return false;
    return irtamccache::shouldCacheBucket(response.size());  // pre-gate P2 (256 B-1 MB)
}

CacheHave IrtamCoeffAdapter::scan() {
    CacheHave have;
    const irtamccache::ScanResult sweep = irtamccache::loadCached(dir_);
    for (const irtamccache::Bucket& b : sweep.buckets) {
        // Nombre "irtamc_<param>_<epoch>.txt": param en minusculas; el plan
        // usa forma display ("foF2"): se mapea de vuelta aqui.
        std::string disp = b.param;
        if (disp == "fof2") disp = "foF2";
        else if (disp == "hmf2") disp = "hmF2";
        else if (disp == "b0") disp = "B0";  // F2-B0B1-R1
        else if (disp == "b1") disp = "B1";  // F2-B0B1-R1
        else continue;  // otros: fuera de scope
        have.epochs[disp].push_back(b.epoch);
        if (b.tovEpoch > have.freshestTov) have.freshestTov = b.tovEpoch;
    }
    freshestTov_ = have.freshestTov;
    return have;
}

std::vector<FetchTarget> IrtamCoeffAdapter::planBackfill(double nowUtc) {
    return planMissing(scan(), windowEndFor(nowUtc));
}

bool IrtamCoeffAdapter::fetchOnce(const FetchTarget& t) {
    // P3 gate: CONSULTA antes de lanzar (C1). Sin espera activa aqui: el
    // hilo de backfill reintenta en su siguiente iteracion.
    // Ciclo traza (solo-log): la decision ya esta tomada; la lectura del
    // gate aqui es solo display (no cambia comportamiento).
    if (!lgdcpacing::canLaunchGambit(wallMs())) {
        error_ = "gate: <15 s desde request lgdc previo (reintento proximo ciclo)";
        lgdctrace::trace(
            "gate-consult",
            "deny gambit " + t.param + " " + formatGambitTime(t.epoch) +
                " ms-since-last=" +
                std::to_string(lgdcpacing::msSinceLastLaunch(wallMs())));
        return false;
    }
    std::string body;
    if (!fetchURL(urlFor(t.param, t.epoch), body)) {
        error_ = "http/curl o pre-gate de tamano";
        lgdctrace::trace("result", "fail gambit " + t.param + " " +
                                        formatGambitTime(t.epoch) +
                                        " red/pre-gate");
        printf("[Irtam] fetch %s %s: fallo red/pre-gate\n",
               t.param.c_str(), formatGambitTime(t.epoch).c_str());
        return false;
    }
    irtamcoeff::Message m;
    std::string perr;
    if (!irtamcoeff::parseMessage(body, m, perr)) {
        error_ = "no-estructural: " + perr;
        lgdctrace::trace("result", "fail gambit " + t.param + " " +
                                        formatGambitTime(t.epoch) +
                                        " estructural");
        printf("[Irtam] fetch %s %s: rechazo estructural (%s)\n",
               t.param.c_str(), formatGambitTime(t.epoch).c_str(), perr.c_str());
        return false;  // fail-soft: el cache se conserva, sin re-burst (C1)
    }
    const std::vector<uint8_t> bytes(body.begin(), body.end());
    // serverLastMod "": el sidecar lo admite (fallback honesto P2); la edad
    // del dato (DATA-age, red line) viene del TOV del mensaje, no del HTTP.
    if (!irtamccache::saveBucket(dir_, t.param, t.epoch, bytes.data(),
                                 bytes.size(), "")) {
        error_ = "cache-reject (gate tamano o escritura)";
        return false;
    }
    if (m.header.tovEpoch > freshestTov_) freshestTov_ = m.header.tovEpoch;
    error_.clear();
    // Ciclo traza (solo-log): instante de REGISTRO leido del gate (el
    // record ocurrio dentro de fetchURL, mismo tick en la practica).
    lgdctrace::trace("launch", "gambit " + t.param + " " +
                                    formatGambitTime(t.epoch));
    lgdctrace::trace("result", "ok gambit " + t.param + " " +
                                   formatGambitTime(t.epoch));
    printf("[Irtam] bucket %s %s OK (TOV %.0f)\n",
           t.param.c_str(), formatGambitTime(t.epoch).c_str(),
           m.header.tovEpoch);
    return true;
}

}  // namespace irtamfetch
