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
        p.B0 = (s.B0 > 0.0f) ? s.B0 : 100.0f;
        p.B1 = (s.B1 > 0.0f) ? s.B1 : 3.0f;   // 3 = valor clasico IRI de fallback
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
            // Epstein modificado, clampear x: fuera de rango "resucita" (ver spec).
            float x = std::max(0.0f, std::min(3.0f, (p.hmF2 - hKm) / p.B0));
            NeF2 = p.NmF2 * expf(-powf(x, p.B1) / coshf(x));
        } else {
            float Htop = 60.0f * (1.0f + (hKm - p.hmF2) / 300.0f);
            NeF2 = p.NmF2 * expf(-(hKm - p.hmF2) / Htop);
        }
        best = std::max(best, NeF2);
    }
    return best;
}
