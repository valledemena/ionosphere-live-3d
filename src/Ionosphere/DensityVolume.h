#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include "Ionosphere/LayerProfile.h"
#include "Ionosphere/GridLayer.h"   // GridData (layout 72x72 compartido)

// Volumen de densidad electronica para raymarching.
// Layout: data[(altIdx*latRes + latIdx)*lonRes + lonIdx],
//   lonIdx 0 = lon -180, latIdx 0 = sur (lat -90) -- igual que GridData,
//   altIdx 0 = altMinKm (abajo). Se guarda log10(Ne) normalizado 0..1.
struct DensityVolume {
    int lonRes = 72, latRes = 72, altRes = 48;
    float altMinKm = 60.0f, altMaxKm = 500.0f;
    std::vector<float> data;
    double timestamp = 0.0;
    bool valid = false;
};

// Atenua E/F1 de noche sobre una rejilla de perfiles (las sondas no miden
// foE/foF1 nocturnos; sin esto el fondo mediano interpolaria densidad
// fantasma). F2 y D los gestiona el evaluador. sunDir en el marco del motor.
inline void applyDayNight(std::vector<LayerProfile>& profiles, int lonRes, int latRes,
                          float sunX, float sunY, float sunZ) {
    if ((int)profiles.size() != lonRes * latRes) return;
    const float kPi = 3.14159265f;
    auto sstep = [](float e0, float e1, float x) {
        float t = (x - e0) / (e1 - e0);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    };
    for (int latIdx = 0; latIdx < latRes; latIdx++) {
        float latDeg = -90.0f + 180.0f * (latIdx + 0.5f) / latRes;
        float phi = (90.0f - latDeg) * kPi / 180.0f;
        for (int lonIdx = 0; lonIdx < lonRes; lonIdx++) {
            float lonDeg = -180.0f + 360.0f * (lonIdx + 0.5f) / lonRes;
            float theta = kPi - lonDeg * kPi / 180.0f;  // convencion del motor
            float cx = sinf(phi) * cosf(theta);
            float cy = cosf(phi);
            float cz = sinf(phi) * sinf(theta);
            float e = sstep(-0.05f, 0.25f, cx * sunX + cy * sunY + cz * sunZ);
            LayerProfile& p = profiles[(size_t)latIdx * lonRes + lonIdx];
            p.NmE *= e;
            p.NmF1 *= e;
            if (e <= 0.0f) { p.hasE = false; p.hasF1 = false; }
        }
    }
}
//   h_i = altMin + (altMax-altMin) * (i/(n-1))^1.5
inline float volumeAltKm(int i, int n, float altMinKm, float altMaxKm) {
    float f = (n <= 1) ? 0.0f : (float)i / (float)(n - 1);
    return altMinKm + (altMaxKm - altMinKm) * powf(f, 1.5f);
}

// Direccion unitaria en el marco del motor (theta = pi - lon).
// OJO (historial de meridianos rotados 180°): NO usar radians(lon) a secas;
// esta es la convencion verificada de IonosondeLayer.cpp:249 + pixeles.
inline glm::vec3 densityDir(float latDeg, float lonDeg) {
    const float kPi = 3.14159265f;
    float phi = (90.0f - latDeg) * kPi / 180.0f;
    float theta = kPi - lonDeg * kPi / 180.0f;
    float sn = sinf(phi), cs = cosf(phi);
    return glm::vec3(sn * cosf(theta), cs, sn * sinf(theta));
}

// Picos por columna extraidos del volumen (Fase 1, plan volume-derived-shells).
//   logNe: log10(Ne) del pico (para color). hmKm: altura del pico en km
//   (refinada por parabola; umbral: picos bajo logNe 8.5 -> hmKm = 0 = hueco).
struct PeakGrids {
    GridData logNe;   // mismas dims que el grid 2D (lonRes x latRes)
    GridData hmKm;
};

inline PeakGrids extractPeakGrids(const DensityVolume& v) {
    PeakGrids out;
    out.logNe.width = v.lonRes; out.logNe.height = v.latRes;
    out.hmKm.width = v.lonRes;  out.hmKm.height = v.latRes;
    out.logNe.values.assign((size_t)v.lonRes * v.latRes, 0.0f);
    out.hmKm.values.assign((size_t)v.lonRes * v.latRes, 0.0f);
    out.logNe.valid = out.hmKm.valid = false;
    if (!v.valid || v.data.empty() || v.lonRes < 1 || v.latRes < 1 || v.altRes < 3) return out;
    const int W = v.lonRes, H = v.latRes, A = v.altRes;
    auto at = [&](int x, int y, int a) -> float {
        return v.data[((size_t)a * H + y) * W + x];
    };
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int bi = 0;
            for (int a = 1; a < A; a++)
                if (at(x, y, a) > at(x, y, bi)) bi = a;
            float peak = at(x, y, bi);
            float hm = volumeAltKm(bi, A, v.altMinKm, v.altMaxKm);
            if (bi > 0 && bi + 1 < A) {
                // Refinamiento sub-muestra: parabola por los 3 puntos con sus
                // alturas REALES (espaciado ^1.5 no uniforme; la version en
                // indice sesga hacia el lado denso). Vertice de Lagrange.
                float x0 = volumeAltKm(bi - 1, A, v.altMinKm, v.altMaxKm);
                float x1 = hm;
                float x2 = volumeAltKm(bi + 1, A, v.altMinKm, v.altMaxKm);
                float y0 = at(x, y, bi - 1), y1 = peak, y2 = at(x, y, bi + 1);
                float den = x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1);
                if (fabsf(den) > 1e-9f) {
                    float xs = 0.5f * (x0 * x0 * (y1 - y2) + x1 * x1 * (y2 - y0) +
                                       x2 * x2 * (y0 - y1)) / den;
                    if (xs < x0) xs = x0;
                    if (xs > x2) xs = x2;
                    // Valor en el vertice: t en [-1,1] sobre el tramo local.
                    float t = (xs - x1) / (0.5f * (x2 - x0) + 1e-9f);
                    if (t < -1.0f) t = -1.0f;
                    if (t > 1.0f) t = 1.0f;
                    hm = xs;
                    peak = y1 - 0.25f * (y0 - y2) * t;
                }
            }
            float logNe = 8.0f + 4.5f * peak;   // des-normalizar (mismo rango)
            out.logNe.values[(size_t)y * W + x] = logNe;
            out.hmKm.values[(size_t)y * W + x] = (logNe < 8.5f) ? 0.0f : hm;
        }
    }
    out.logNe.valid = out.hmKm.valid = true;
    out.logNe.timestamp = out.hmKm.timestamp = v.timestamp;
    return out;
}

// TEC[TECU] = (1/1e16) * integral trapezoidal de Ne[m^-3] dh[m].
// Des-normaliza (el volumen guarda log10 0..1) y usa el Δh real por tramo
// (alturas en ^1.5, NO uniformes).
inline float integrateTEC(const DensityVolume& v, int latIdx, int lonIdx) {
    if (!v.valid || v.data.empty()) return 0.0f;
    const int W = v.lonRes, H = v.latRes, A = v.altRes;
    if (latIdx < 0 || latIdx >= H || lonIdx < 0 || lonIdx >= W || A < 2) return 0.0f;
    double tec = 0.0;
    for (int a = 0; a + 1 < A; a++) {
        float n0 = v.data[((size_t)a * H + latIdx) * W + lonIdx];
        float n1 = v.data[((size_t)(a + 1) * H + latIdx) * W + lonIdx];
        double Ne0 = pow(10.0, 8.0 + 4.5 * n0);
        double Ne1 = pow(10.0, 8.0 + 4.5 * n1);
        double dh = (volumeAltKm(a + 1, A, v.altMinKm, v.altMaxKm) -
                     volumeAltKm(a, A, v.altMinKm, v.altMaxKm)) * 1000.0;
        tec += 0.5 * (Ne0 + Ne1) * dh;
    }
    return (float)(tec / 1.0e16);
}
inline float sampleVolume(const DensityVolume& v, float latDeg, float lonDeg, float altKm) {
    if (!v.valid || v.data.empty() || v.lonRes < 2 || v.latRes < 2 || v.altRes < 2) return 0.0f;
    const int W = v.lonRes, H = v.latRes, A = v.altRes;
    float gx = (lonDeg + 180.0f) / 360.0f * W - 0.5f;
    float gy = (latDeg + 90.0f) / 180.0f * H - 0.5f;
    int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
    float fx = gx - x0, fy = gy - y0;
    int a0 = 0;
    while (a0 + 1 < A && volumeAltKm(a0 + 1, A, v.altMinKm, v.altMaxKm) <= altKm) a0++;
    int a1 = (a0 + 1 < A) ? a0 + 1 : a0;
    float h0 = volumeAltKm(a0, A, v.altMinKm, v.altMaxKm);
    float h1 = volumeAltKm(a1, A, v.altMinKm, v.altMaxKm);
    float fa = (h1 > h0) ? (altKm - h0) / (h1 - h0) : 0.0f;
    fa = fa < 0.0f ? 0.0f : (fa > 1.0f ? 1.0f : fa);
    auto at = [&](int x, int y, int a) -> float {
        x %= W; if (x < 0) x += W;
        y = y < 0 ? 0 : (y >= H ? H - 1 : y);
        return v.data[((size_t)a * H + y) * W + x];
    };
    float lo = at(x0, y0, a0) * (1 - fx) * (1 - fy) + at(x0 + 1, y0, a0) * fx * (1 - fy) +
               at(x0, y0 + 1, a0) * (1 - fx) * fy + at(x0 + 1, y0 + 1, a0) * fx * fy;
    float hi = at(x0, y0, a1) * (1 - fx) * (1 - fy) + at(x0 + 1, y0, a1) * fx * (1 - fy) +
               at(x0, y0 + 1, a1) * (1 - fx) * fy + at(x0 + 1, y0 + 1, a1) * fx * fy;
    return lo * (1.0f - fa) + hi * fa;
}

// Construye el volumen muestreando evalNeTotal sobre una rejilla de perfiles.
//   profiles: un LayerProfile por celda (lonRes*latRes, mismo layout que GridData).
//   sunDir:   direccion solar normalizada en el marco del motor
//             (-X=lon0, +Z=lon90E, +Y=norte) para el coseno cenital.
//   flareFactor: multiplicador de fulguracion global (>=1).
// Rango log10 [8, 12.5] -> [0,1] (ver spec: 4+ ordenes de magnitud).
inline DensityVolume buildDensityVolume(const std::vector<LayerProfile>& profiles,
                                        int lonRes, int latRes,
                                        int altRes, float altMinKm, float altMaxKm,
                                        float sunX, float sunY, float sunZ,
                                        float flareFactor) {
    DensityVolume vol;
    vol.lonRes = lonRes;
    vol.latRes = latRes;
    vol.altRes = altRes;
    vol.altMinKm = altMinKm;
    vol.altMaxKm = altMaxKm;
    vol.data.assign((size_t)lonRes * latRes * altRes, 0.0f);
    vol.valid = false;
    if ((int)profiles.size() != lonRes * latRes) return vol;

    const float kPi = 3.14159265f;
    for (int latIdx = 0; latIdx < latRes; latIdx++) {
        float latDeg = -90.0f + 180.0f * (latIdx + 0.5f) / latRes;
        float phi = (90.0f - latDeg) * kPi / 180.0f;
        for (int lonIdx = 0; lonIdx < lonRes; lonIdx++) {
            float lonDeg = -180.0f + 360.0f * (lonIdx + 0.5f) / lonRes;
            // Convencion del motor: theta = pi - lon (ver IonosondeLayer.cpp:249).
            float theta = kPi - lonDeg * kPi / 180.0f;
            float cx = sinf(phi) * cosf(theta);
            float cy = cosf(phi);
            float cz = sinf(phi) * sinf(theta);
            float cosChi = cx * sunX + cy * sunY + cz * sunZ;
            const LayerProfile& p = profiles[(size_t)latIdx * lonRes + lonIdx];
            for (int a = 0; a < altRes; a++) {
                float h = volumeAltKm(a, altRes, altMinKm, altMaxKm);
                float ne = evalNeTotal(p, h, cosChi, flareFactor);
                float l10 = (ne > 0.0f) ? log10f(ne) : 0.0f;
                float n = (l10 - 8.0f) / (12.5f - 8.0f);
                n = std::max(0.0f, std::min(1.0f, n));
                vol.data[((size_t)a * latRes + latIdx) * lonRes + lonIdx] = n;
            }
        }
    }
    vol.valid = true;
    return vol;
}
