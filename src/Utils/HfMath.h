#pragma once
// =============================================================================
// HfMath — geometría de saltos HF (secante con altura virtual) + factor M(D).
//
// Matemática pura: sin GL, sin red, sin dependencias. Portado 1:1 desde la
// demo web validada visualmente (ionosphere-web-explorer, src/lib/iono/hf.ts)
// para que M8 (HFTraceLayer) y los tests compartan UNA implementación canónica.
//
//   - El salto DECRECE con la elevación: rayo casi tangente -> ~3897 km
//     (h'=310), rayo vertical -> ~0 km.
//   - elevationForHop(): bisección sobre [1,88] grados (60 iteraciones).
//   - Referencia de libro (test_hop_geometry): D=2511 km con h'=310 km
//     exige despegue ~7.9 grados (valor exacto 7.865).
// =============================================================================

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hfmath {

constexpr double kEarthRadiusKm = 6371.0;

struct HopGeometry {
    double hopKm;        // distancia en tierra cubierta por un salto
    double incidenceDeg; // incidencia desde la vertical en el punto de reflexión
};

// Geometría directa: dada la elevación de despegue y la altura virtual h',
// devuelve el salto en tierra y la incidencia en la capa.
inline HopGeometry hopGeometry(double elevDeg, double hVirtualKm) {
    const double R = kEarthRadiusKm;
    const double beta = elevDeg * (M_PI / 180.0);
    // Ley de senos del triángulo TX–centro–reflexión: sin(i) = R·cos(beta)/(R+h')
    const double arg = std::min(1.0, std::max(-1.0, (R * std::cos(beta)) / (R + hVirtualKm)));
    const double i = std::asin(arg);
    double halfCentral = (M_PI / 2.0) - beta - i;   // ángulo central de medio salto (>= 0)
    if (halfCentral < 0.0) halfCentral = 0.0;
    return { 2.0 * R * halfCentral, i * (180.0 / M_PI) };
}

// Inversa: elevación necesaria para un salto objetivo con esa altura virtual.
// Devuelve 1 si es inalcanzable incluso casi tangente (objetivo > máximo),
// 88 si el objetivo es diminuto (casi vertical).
inline double elevationForHop(double targetKm, double hVirtualKm) {
    double lo = 1.0, hi = 88.0;
    if (hopGeometry(lo, hVirtualKm).hopKm - targetKm < 0.0) return lo; // inalcanzable
    if (hopGeometry(hi, hVirtualKm).hopKm - targetKm > 0.0) return hi; // diminuto
    for (int it = 0; it < 60; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (hopGeometry(mid, hVirtualKm).hopKm - targetKm > 0.0) lo = mid; // aún largo -> más vertical
        else hi = mid;                                                     // corto -> más plano
    }
    return 0.5 * (lo + hi);
}

// M3000 (Dudeney) y M(D) estilo ITU-R P.1240: M(D) = M3000·(D/3000)^0.24
inline double m3000(double hmF2Km) { return 1490.0 / (hmF2Km + 216.0); }
inline double mFactor(double hmF2Km, double distanceKm) {
    return m3000(hmF2Km) * std::pow(std::max(distanceKm, 200.0) / 3000.0, 0.24);
}

// Número óptimo de saltos para una distancia total (apunta a ~3000 km/salto,
// máximo 4000 km por salto, 6 saltos como techo práctico del circuito).
inline int hopCountForDistance(double totalKm) {
    int best = 1;
    double bestErr = 1.0e30;
    for (int n = 1; n <= 6; ++n) {
        const double per = totalKm / n;
        if (per > 4000.0) continue;
        const double err = std::fabs(per - 3000.0);
        if (err < bestErr) { bestErr = err; best = n; }
    }
    return best;
}

} // namespace hfmath
