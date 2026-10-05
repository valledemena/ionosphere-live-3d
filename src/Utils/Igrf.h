#pragma once
// Igrf (M'-1, ruling 142 sec.4): campo geomagnetico IGRF-14 real.
// Puro: sin GL, sin red, sin hilos, sin persistencia. Coeficientes g/h
// embebidos (grado 8, epoca 2025.0 congelada, sin variacion secular).
// Fuente: IAGA/NOAA ngdc.noaa.gov/IAGA/vmod/coeffs/igrf14coeffs.txt
// (Schmidt semi-normalizados, nT). Evaluacion Schmidt estandar (receta IAGA),
// geodesico->geocentrico WGS-84, salida NED en nT.
// Mas rotacion de Faraday en camino vertical (v1): Omega = K/f^2 * Bpar * N_T
// con K derivado de constantes CODATA nombradas (cero literales magicas).
#include <cmath>

namespace igrf {

struct Ned {
    float n = 0.0f;  // norte (nT)
    float e = 0.0f;  // este (nT)
    float d = 0.0f;  // abajo/down (nT, positivo hacia la Tierra)
};

// Campo a (lat, lon, altKm). Alt clamp declarado [0, 1000] km.
Ned fieldNed(double latDeg, double lonDeg, double altKm);

inline float modNed(const Ned& b) {
    return sqrtf(b.n * b.n + b.e * b.e + b.d * b.d);
}
// Inclinacion (dip): positiva hacia abajo (norte), negativa (sur).
inline float dipDeg(const Ned& b) {
    const float m = modNed(b);
    if (m <= 0.0f) return 0.0f;
    float s = b.d / m;
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    return asinf(s) * 57.29577951f;
}

// --- Rotacion de Faraday (camino vertical v1) ---
// K = e^3/(8*pi^2*eps0*me^2*c) ~ 2.365e4 (B en T, N_T en m^-2, f en Hz).
double faradayK();  // derivada de CODATA en codigo, sin literal magico
// Omega en radianes. Geometria v1: camino vertical, Bpar = NED-Z a 300 km
// (FARADAY_EVAL_ALT_KM); integral colapsada a Bpar*N_T (B constante).
inline double omegaRad(double bParT, double tecM2, double freqHz) {
    const double f2 = freqHz * freqHz;
    if (f2 <= 0.0) return 0.0;
    return faradayK() / f2 * bParT * tecM2;
}
inline double omegaDeg(double bParT, double tecM2, double freqHz) {
    return omegaRad(bParT, tecM2, freqHz) * 57.29577951308232;
}

constexpr double FARADAY_REF_FREQ_HZ = 100.0e6;  // capa: referencia 100 MHz
constexpr double FARADAY_EVAL_ALT_KM = 300.0;    // centroide F
constexpr double FARADAY_FULL_DEG = 720.0;       // escala visual de la capa

}  // namespace igrf
