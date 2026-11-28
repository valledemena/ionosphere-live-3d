#pragma once
#include <cmath>
#include <vector>
#include "Data/SolarWindAdapter.h"

// DW.20: retardo L1 -> Tierra segun OMNI (x/v con la velocidad de cada muestra).
namespace swdelay {

inline constexpr double kL1Km = 1.5e6;  // ~1,5 · 10^6 km

struct DelayedWind {
    SolarWindSample sample;     // valido solo si available
    bool available = false;
    double travelSec = 0.0;     // kL1Km / v de la muestra elegida (normalizado)
};

// nowUtc MUST be wall-clock (std::time(nullptr)), never the replay epoch:
// el retardo se mide contra el reloj real del dato, no contra el cursor.
inline DelayedWind sampleAt(const std::vector<SolarWindSample>& hist,
                            double nowUtc, double distanceKm = kL1Km) {
    DelayedWind out;
    if (!(distanceKm > 0.0) || !std::isfinite(distanceKm)) return out;
    if (!std::isfinite(nowUtc)) return out;
    bool firstValid = true;
    for (std::size_t i = hist.size(); i-- > 0;) {
        const SolarWindSample& s = hist[i];
        if (!s.valid || !std::isfinite(s.speed) || !(s.speed > 0.0f)) continue;
        const double travel = distanceKm / (double)s.speed;  // km / (km/s) = s
        if (!std::isfinite(travel) || travel <= 0.0) continue;
        if (firstValid) { out.travelSec = travel; firstValid = false; }
        // Nota 1 del veredicto: travelSec siempre finito aunque available=false.
        if (std::isfinite(s.timeUtc) && s.timeUtc + travel <= nowUtc) {
            out.sample = s;
            out.available = true;
            out.travelSec = travel;  // exactamente kL1Km / v de esta muestra
            return out;
        }
    }
    return out;  // sin muestra llegada: travelSec del primer valido visto
}

}  // namespace swdelay
