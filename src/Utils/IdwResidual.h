#pragma once

// DW.35 F4: IDW de RESIDUO sobre un fondo climatologico ANALITICO.
//
// El fondo NO es un dato medido: es una parametrizacion declarada que
// usa MODIP (irtameval::igrfDip, IGRF-14 NMAX=13, marco Tierra) y el
// cenit solar calculado del UTC. Coeficientes declarados aqui:
//   A(F10.7) = 4.0 + 0.03*(F107-100)   [MHz]
//   g(|modip|) = 0.6 + 0.4*sat(|modip|/90)
//   foF2_bg = A * g * sqrt(max(0, cos(chi)))
// El residuo observado-fondo se interpola IDW (radio 50 grados, peso de
// fondo debil 0.004 como en giroInterpGrid) y se suma al fondo.
//
// La metrica es leave-one-out sobre las MISMAS estaciones: validacion
// interna, NO un producto operativo ni una medida de exactitud global.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <limits>
#include <vector>

#include "Data/GiroAdapter.h"
#include "Data/IrtamGridEval.h"
#include "Ionosphere/GridLayer.h"

namespace idwres {

constexpr float kRadiusDeg = 50.0f;
constexpr float kBgWeight = 0.004f;

inline float declinationDeg(double utcSeconds) {
    std::time_t tt = (std::time_t)utcSeconds;
    std::tm tm{};
#if defined(_WIN32)
    if (gmtime_s(&tm, &tt) != 0) return 0.0f;
#else
    if (gmtime_r(&tt, &tm) == nullptr) return 0.0f;
#endif
    const int doy = tm.tm_yday + 1;
    return 23.44f * (float)std::sin(2.0 * 3.14159265358979323846 * (doy - 81) / 365.0);
}

inline float solarZenithCos(double utcSeconds, double latDeg, double lonDeg) {
    const float dec = declinationDeg(utcSeconds);
    const double daySec = std::fmod(std::max(0.0, utcSeconds), 86400.0);
    const double subsolarLon = 180.0 - daySec / 240.0;   // 15 grados/hora
    const double lat = latDeg * 3.14159265358979323846 / 180.0;
    const double d = dec * 3.14159265358979323846 / 180.0;
    const double h = (lonDeg - subsolarLon) * 3.14159265358979323846 / 180.0;
    const float c = (float)(std::sin(lat) * std::sin(d) +
                            std::cos(lat) * std::cos(d) * std::cos(h));
    return std::max(0.0f, c);
}

inline float modipAt(double latDeg, double lonDeg, double utcSeconds) {
    std::time_t tt = (std::time_t)utcSeconds;
    std::tm tm{};
    double year = 2025.0;
#if defined(_WIN32)
    if (gmtime_s(&tm, &tt) == 0) year = 1900.0 + tm.tm_year + (tm.tm_yday + 1) / 365.0;
#else
    if (gmtime_r(&tt, &tm) != nullptr) year = 1900.0 + tm.tm_year + (tm.tm_yday + 1) / 365.0;
#endif
    double x = 0.0;
    if (!irtameval::igrfDip(latDeg, lonDeg, year, 300.0, x) || !std::isfinite(x)) return 0.0f;
    return (float)x;
}

inline float climatologyFoF2(double latDeg, double lonDeg, double utcSeconds, float f107) {
    const float a = 4.0f + 0.03f * (f107 - 100.0f);
    const float g = 0.6f + 0.4f * std::min(1.0f, std::fabs(modipAt(latDeg, lonDeg, utcSeconds)) / 90.0f);
    const float cz = solarZenithCos(utcSeconds, latDeg, lonDeg);
    return a * g * std::sqrt(cz);
}

// Reglas declaradas (DW.35): exclusion de estaciones !valid o stale;
// minimo 3 estaciones para bias/RMSE global y minimo 2 por estrato;
// estrato vacio o por debajo del minimo = NO MEDIBLE (NaN), nunca 0.
// Cortes: |MODIP| < 45 bajo, >= 45 alto; cos(chi) > 0.2 dia, <= 0.2 noche.
struct Metrics {
    int n = 0;
    int nLowModip = 0;
    int nHighModip = 0;
    int nDay = 0;
    int nNight = 0;
    float bias = 0.0f;
    float rmse = 0.0f;
    float rmseLowModip = 0.0f;
    float rmseHighModip = 0.0f;
    float rmseDay = 0.0f;
    float rmseNight = 0.0f;
};

inline Metrics residualMetrics(const std::vector<StationSample>& samples,
                               double utcSeconds, float f107) {
    Metrics m;
    double sum = 0.0, sum2 = 0.0;
    double lo = 0.0, lo2 = 0.0, loN = 0.0;
    double hi = 0.0, hi2 = 0.0, hiN = 0.0;
    double day = 0.0, day2 = 0.0, dayN = 0.0;
    double night = 0.0, night2 = 0.0, nightN = 0.0;
    for (const auto& s : samples) {
        if (!s.valid || s.stale || !(s.foF2 > 0.0f)) continue;
        const float bg = climatologyFoF2(s.latDeg, s.lonDeg, utcSeconds, f107);
        const float e = s.foF2 - bg;
        ++m.n; sum += e; sum2 += e * e;
        if (std::fabs(modipAt(s.latDeg, s.lonDeg, utcSeconds)) < 45.0f) {
            lo += e; lo2 += e * e; ++loN;
        } else {
            hi += e; hi2 += e * e; ++hiN;
        }
        if (solarZenithCos(utcSeconds, s.latDeg, s.lonDeg) > 0.2f) {
            day += e; day2 += e * e; ++dayN;
        } else {
            night += e; night2 += e * e; ++nightN;
        }
    }
    m.nLowModip = (int)loN;
    m.nHighModip = (int)hiN;
    m.nDay = (int)dayN;
    m.nNight = (int)nightN;
    if (m.n >= 3) {
        m.bias = (float)(sum / m.n);
        m.rmse = (float)std::sqrt(std::max(0.0, sum2 / m.n));
    } else {
        m.bias = std::numeric_limits<float>::quiet_NaN();
        m.rmse = std::numeric_limits<float>::quiet_NaN();   // NO MEDIBLE
    }
    auto sq = [](double s2, double n) {
        return n >= 2.0 ? (float)std::sqrt(std::max(0.0, s2 / n))
                        : std::numeric_limits<float>::quiet_NaN();  // NO MEDIBLE
    };
    m.rmseLowModip = sq(lo2, loN);
    m.rmseHighModip = sq(hi2, hiN);
    m.rmseDay = sq(day2, dayN);
    m.rmseNight = sq(night2, nightN);
    return m;
}

inline GridData giroResidualGrid(const std::vector<StationSample>& samples,
                                 double utcSeconds, float f107, int w, int h) {
    GridData g;
    g.width = w; g.height = h;
    g.values.assign(w * h, 0.0f);
    g.timestamp = utcSeconds;
    g.valid = false;
    std::vector<const StationSample*> pts;
    for (const auto& s : samples) if (s.valid && !s.stale && s.foF2 > 0.0f) pts.push_back(&s);
    if (pts.empty() || w <= 0 || h <= 0) return g;

    std::vector<float> resi(pts.size());
    for (size_t i = 0; i < pts.size(); ++i)
        resi[i] = pts[i]->foF2 - climatologyFoF2(pts[i]->latDeg, pts[i]->lonDeg, utcSeconds, f107);

    const float cellLat = 180.0f / h;
    const float cellLon = 360.0f / w;
    for (int latIdx = 0; latIdx < h; ++latIdx) {
        const float lat = -90.0f + (latIdx + 0.5f) * cellLat;
        for (int lonIdx = 0; lonIdx < w; ++lonIdx) {
            const float lon = -180.0f + (lonIdx + 0.5f) * cellLon;
            float wsum = kBgWeight;
            float rsum = 0.0f;
            for (size_t i = 0; i < pts.size(); ++i) {
                float dlat = lat - pts[i]->latDeg;
                float dlon = lon - pts[i]->lonDeg;
                while (dlon > 180.0f) dlon -= 360.0f;
                while (dlon < -180.0f) dlon += 360.0f;
                const float d = std::sqrt(dlat * dlat + dlon * dlon * 0.25f);
                if (d > kRadiusDeg) continue;
                const float wt = 1.0f / (1.0f + d * d);
                wsum += wt;
                rsum += wt * resi[i];
            }
            g.values[latIdx * w + lonIdx] = climatologyFoF2(lat, lon, utcSeconds, f107) + rsum / wsum;
        }
    }
    g.valid = true;
    return g;
}

}  // namespace idwres
