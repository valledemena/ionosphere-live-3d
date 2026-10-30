#pragma once
// =============================================================================
// TleGroupStatus: vista HONESTA y TESTEABLE del estado de un grupo TLE, para
// el panel por grupo de la app (sin E/S: se alimenta de una copia que el
// adaptador actualiza al final de cada pase, bajo mutex, sin tocar disco).
//
// `formatRow(status, nowUtc)` es una funcion PURA: solo strings + fecha, sin
// GL, sin red, sin ioctl. La UI la llama con la copia de los grupos.
// =============================================================================
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <string>

namespace tlegroupstatus {

struct Status {
    std::string key;            // "amateur", "stations", "active"...
    double cacheEpoch = -1.0;   // epoch del cache (>0) o -1 si no hay
    double lastAttemptUtc = 0.0;
    double lastSuccessUtc = 0.0;
    double blockedUntilUtc = 0.0;
    int lastHttp = 0;
    int lastCurl = 0;
    std::string blockedBy;      // "none", "own", "host"
};

// Edad (segundos) calculada EN EL CAMP caller (formatRow), sin pieza real.
inline double cacheAgeSec(const Status& s, double nowUtc) {
    if (s.cacheEpoch <= 0.0 || !(nowUtc >= s.cacheEpoch)) return -1.0;
    return nowUtc - s.cacheEpoch;
}

inline std::string formatRow(const Status& s, double nowUtc) {
    using namespace std;
    const double age = cacheAgeSec(s, nowUtc);
    string out = s.key;
    if (age >= 0.0) {
        char ba[32];
        std::snprintf(ba, sizeof(ba), "%.1f h", age / 3600.0);
        out += ": cache " + string(ba) + " ago";
    } else {
        out += ": sin cache";
    }
    const double rem = s.blockedUntilUtc - nowUtc;
    if (rem > 0.0) {
        std::time_t tt = static_cast<std::time_t>(s.blockedUntilUtc);
        std::tm* tm = std::gmtime(&tt);
        char bt[8] = "";
        if (tm) std::snprintf(bt, sizeof(bt), "%02d:%02d", tm->tm_hour, tm->tm_min);
        out += ", bloqueado hasta " + string(bt);
        const char* why = nullptr;
        if (s.blockedBy == "host") why = " (host)";
        else if (s.blockedBy == "own") why = " (propio)";
        if (why) out += why;
        if (s.lastHttp > 0) {
            char hh[16];
            std::snprintf(hh, sizeof(hh), " (HTTP %d)", s.lastHttp);
            out += hh;
        }
    }
    return out;
}

}  // namespace tlegroupstatus
