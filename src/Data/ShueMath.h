#pragma once
#include <algorithm>
#include <cmath>

// Magnetopausa Shue-1998 [S98] (Shue et al., JGR 103, 17691, 1998), con los
// coeficientes pineados tal cual vienen de la demo web (magnetosphere.ts).
//
// Unidades: Bz en nT, Dp en nPa, r en R_E, theta = angulo cenital solar (rad)
// medido desde el eje +x (sol). Forma funcional:
//
//   r0(Bz,Dp)    = [10.22 + 1.29*tanh(0.184*(Bz+8.14))] / Dp^(1/6.6)
//   alpha(Bz,Dp) = (0.58 - 0.007*Bz) * (1 + 0.024*ln Dp)
//   r(theta)     = r0 * [2/(1+cos theta)]^alpha
//   Dp           = 1.6726e-6 * n[cm^-3] * v[km/s]^2   (mp*n*v^2)
//
// Vive en un header aparte porque lo comparten DOS consumidores que deben
// coincidir al decimal: la malla de la magnetopausa (MagnetosphereLayer) y el
// obstaculo que desvia el rio de viento solar (SolarWindRiverSim). El test
// tests/test_solar_wind_river.cpp pina estos numeros.
//
// Fuera de alcance: termino cuadrupolar, corriente de anillo, Tsyganenko y
// cualquier forma de cola real. Aqui solo hay cavidad diaside.
namespace shuemath {

// ---- Coeficientes Shue-1998 (nT, nPa, R_E) ----
constexpr float kR0C0 = 10.22f;        // r0 = (c0 + c1*tanh(k*(Bz+off))) / Dp^(1/6.6)
constexpr float kR0C1 = 1.29f;
constexpr float kR0K = 0.184f;
constexpr float kR0Offset = 8.14f;
constexpr float kR0ExpInv = 6.6f;
constexpr float kAlpha0 = 0.58f;       // alpha = (a0 + a1*Bz) * (1 + a2*ln Dp)
constexpr float kAlpha1 = -0.007f;     // (el original resta: a1 ya es negativo)
constexpr float kAlpha2 = 0.024f;
constexpr float kDpMin = 0.05f;        // nPa, suelo contra Dp=0
constexpr float kDpCoef = 1.6726e-6f;  // mp * (cm^-3 / m^-3) * (km/s)^2 -> nPa

// Dp[nPa] = 1.6726e-6 * n[cm^-3] * v[km/s]^2 (mp*n*v^2). Densidad negativa se
// trata como 0; no se aplica el suelo (lo aplican r0/alpha).
inline float dynamicPressure(float densityCm3, float speedKms) {
    const float n = std::max(densityCm3, 0.0f);
    return kDpCoef * n * speedKms * speedKms;
}

// Radio subsolar de la magnetopausa en R_E.
inline float r0(float bzNT, float dpNPa) {
    const float dp = std::max(dpNPa, kDpMin);
    return (kR0C0 + kR0C1 * std::tanh(kR0K * (bzNT + kR0Offset))) /
           std::pow(dp, 1.0f / kR0ExpInv);
}

// Apertura (flaring) adimensional.
inline float alpha(float bzNT, float dpNPa) {
    const float dp = std::max(dpNPa, kDpMin);
    return (kAlpha0 + kAlpha1 * bzNT) * (1.0f + kAlpha2 * std::log(dp));
}

// Radio de la superficie a un angulo cenital solar theta (rad, 0 = subsolar).
// theta -> pi diverge (la cola no cierra), que es justo lo que se quiere para
// el clamp del rio: alli r < r(theta) siempre y no se empuja nada.
inline float radius(float thetaRad, float bzNT, float dpNPa) {
    const float c = std::cos(thetaRad);
    const float flare = 2.0f / (1.0f + c);
    return r0(bzNT, dpNPa) * std::pow(flare, alpha(bzNT, dpNPa));
}

}  // namespace shuemath
