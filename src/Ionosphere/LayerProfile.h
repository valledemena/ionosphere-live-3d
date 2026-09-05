#pragma once
#include <cmath>
#include <algorithm>
#include "Ionosphere/IonosondeLayer.h"   // StationPoint

// Perfil vertical de capas por estacion, derivado de los parametros medidos.
// Fisica (ver docs/volumetric-density-spec.md):
//   - D/E/F1: Chapman-alfa (equilibrio fotoquimico).
//   - F2 bottomside: Epstein modificado de Bilitza&Rawer (difusion ambipolar),
//     valido SOLO cerca del pico -> clampear x a [0, 3].
//   - F2 topside: caida exponencial con escala creciente (difusivo).
//   - Combinacion por max() para no duplicar densidad en los valles.
struct LayerProfile {
    float NmE = 0.0f,  hmE = 110.0f;
    float NmF1 = 0.0f, hmF1 = 180.0f;
    float NmF2 = 0.0f, hmF2 = 300.0f, B0 = 100.0f, B1 = 3.0f;
    bool hasE = false, hasF1 = false, hasF2 = false;
};

// Saneado al ingerir (SAO ruidoso puede dar B1<1.9, donde ni xmax=1.6 es
// seguro: el minimo real cae a x~=1.2 para B1=1.0). Rangos amplios para no
// tocar datos sanos.
inline float sanitizeB0(float v) {
    if (!(v > 0.0f)) return 100.0f;
    if (v < 20.0f) return 20.0f;
    if (v > 300.0f) return 300.0f;
    return v;
}
inline float sanitizeB1(float v) {
    if (!(v > 0.0f)) return 3.0f;
    if (v < 1.7f) return 1.7f;
    if (v > 4.0f) return 4.0f;
    return v;
}

inline LayerProfile profileFromStation(const StationPoint& s) {
    LayerProfile p;
    if (s.foE > 0.0f) {
        p.NmE = 1.24e10f * s.foE * s.foE;
        p.hmE = 110.0f;
        p.hasE = true;
    }
    if (s.foF1 > 0.0f) {
        p.NmF1 = 1.24e10f * s.foF1 * s.foF1;
        // hF1 real no disponible (solo virtual hF); aproximacion de partida:
        p.hmF1 = (s.hmF2 > 0.0f) ? std::max(150.0f, 0.8f * s.hmF2) : 180.0f;
        p.hasF1 = true;
    }
    if (s.foF2 > 0.0f && s.hmF2 > 0.0f) {
        p.NmF2 = (s.nmF2 > 0.0f) ? s.nmF2 : 1.24e10f * s.foF2 * s.foF2;
        p.hmF2 = s.hmF2;
        p.B0 = sanitizeB0(s.B0);
        p.B1 = sanitizeB1(s.B1);
        p.hasF2 = true;
    }
    return p;
}

// Chapman-alfa: Ne(h) = Nm * exp(0.5 * (1 - z - exp(-z))), z = (h-hm)/H.
inline float chapmanAlpha(float Nm, float hm, float H, float h) {
    float z = (h - hm) / H;
    return Nm * expf(0.5f * (1.0f - z - expf(-z)));
}

// Densidad total [m^-3] a altura hKm.
//   cosChi: coseno del angulo cenital solar en la celda (<=0 de noche).
//   flareFactor: multiplicador de fulguracion (>=1, ver XrayAdapter).
inline float evalNeTotal(const LayerProfile& p, float hKm, float cosChi, float flareFactor) {
    float best = 0.0f;

    // D: sin fo medible; fondo Chapman Nm=1e9*sqrt(cosChi), H=10, hm=75.
    if (cosChi > 0.0f) {
        float NmD = 1.0e9f * sqrtf(cosChi) * flareFactor;
        best = std::max(best, chapmanAlpha(NmD, 75.0f, 10.0f, hKm));
    }
    if (p.hasE)  best = std::max(best, chapmanAlpha(p.NmE, p.hmE, 10.0f, hKm));
    if (p.hasF1) best = std::max(best, chapmanAlpha(p.NmF1, p.hmF1, 25.0f, hKm));
    if (p.hasF2) {
        float NeF2 = 0.0f;
        if (hKm <= p.hmF2) {
            // Epstein modificado. El minimo real de x^B1/cosh(x) esta en
            // x*tanh(x)=B1 (x*~=1.97 para B1=1.9): clampear a 3.0 YA resucita
            // con B1 realista. Clamp a 1.6 (seguro para B1>=1.7, saneado arriba)
            // + taper exponencial mas alla, en vez de meseta.
            float B0 = p.B0 > 0.0f ? p.B0 : 100.0f;
            float x = (p.hmF2 - hKm) / B0;
            const float xmax = 1.6f;
            float xc = x < xmax ? x : xmax;
            float base = p.NmF2 * expf(-powf(xc, p.B1) / coshf(xc));
            if (x > xmax) base *= expf(-(x - xmax) / 1.2f);
            NeF2 = base;
        } else {
            float Htop = 60.0f * (1.0f + (hKm - p.hmF2) / 300.0f);
            NeF2 = p.NmF2 * expf(-(hKm - p.hmF2) / Htop);
        }
        best = std::max(best, NeF2);
    }
    return best;
}
