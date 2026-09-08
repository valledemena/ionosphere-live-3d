#pragma once
#include <cmath>
#include <algorithm>
#include <ctime>
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
//   winner: si no es null, recibe la rama ganadora del max() (0=D,1=E,2=F1,3=F2;
//   -1 si ninguna). Empates -> la mas baja (comparacion estricta en orden).
inline float evalNeTotal(const LayerProfile& p, float hKm, float cosChi, float flareFactor,
                         int* winner = nullptr) {
    float best = 0.0f;
    int win = -1;

    // D: sin fo medible; fondo Chapman Nm=1e9*sqrt(cosChi), H=10, hm=75.
    if (cosChi > 0.0f) {
        float NmD = 1.0e9f * sqrtf(cosChi) * flareFactor;
        float v = chapmanAlpha(NmD, 75.0f, 10.0f, hKm);
        if (v > best) { best = v; win = 0; }
    }
    if (p.hasE) {
        float v = chapmanAlpha(p.NmE, p.hmE, 10.0f, hKm);
        if (v > best) { best = v; win = 1; }
    }
    if (p.hasF1) {
        float v = chapmanAlpha(p.NmF1, p.hmF1, 25.0f, hKm);
        if (v > best) { best = v; win = 2; }
    }
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
        if (NeF2 > best) { best = NeF2; win = 3; }
    }
    if (winner) *winner = win;
    return best;
}
// ---------------------------------------------------------------------------
// M5 (plan 8, sellado): fondo climatologico + distancia gran circulo.
// Inline en LayerProfile.h (la opcion que el propio plan 8 ofrece para
// tests): test_model_foF2 llama a PRODUCCION sin vincular App.cpp. Sin
// glm — solo cmath/algorithm/ctime.
// ---------------------------------------------------------------------------

// smoothstep(a,b,x) = t=clamp((x-a)/(b-a),0,1); t*t*(3-2t) (plan 8.2).
// Bordes invertidos (a>b) validos: el plan 8.3 usa smoothstep(55,25,near).
inline double smoothstep(double a, double b, double x) {
    double t = (x - a) / (b - a);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return t * t * (3.0 - 2.0 * t);
}

// Distancia gran circulo haversine en GRADOS (plan 8.1; R no importa,
// solo el angulo). Sustituye la distancia plana del IDW de perfiles
// (la plana tiene error grande en latitudes altas).
inline double havDeg(double la1, double lo1, double la2, double lo2) {
    const double kPi = 3.14159265358979323846;
    const double d2r = kPi / 180.0;
    double dla = (la2 - la1) * d2r, dlo = (lo2 - lo1) * d2r;
    double a = std::sin(dla / 2.0) * std::sin(dla / 2.0) +
               std::cos(la1 * d2r) * std::cos(la2 * d2r) *
               std::sin(dlo / 2.0) * std::sin(dlo / 2.0);
    return 2.0 * std::asin(std::min(1.0, std::sqrt(a))) / d2r;
}

// Dia del ano UTC (1..366) para el termino estacional del modelo.
// Mismo patron gmtime que App.cpp (hilo principal / single-thread).
inline int dayOfYear(double epoch) {
    std::time_t t = (std::time_t)epoch;
    std::tm g;
#ifdef _WIN32
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    return g.tm_yday + 1;
}

// foF2 climatologico (MHz) — CCIR-like simplificado con F10.7 vivo
// (plan 8.2, port directo de la demo). Firma sellada P1 (correccion de
// MUSE): cosChi llega precalculado por el caller con la MISMA expresion
// que applyDayNight (dot(densityDir(lat,lon), sunDir), marco del motor:
// theta = pi - lon, -X = lon 0, ver DensityVolume.h) — el dia del modelo
// y el terminador comparten vector solar (M2/effEpoch) y no pueden
// divergir. lon no entra en la firma (solo via cosChi). latLonToUnit NO
// sirve aqui: convencion opuesta (+X = lon 0) — mediodia del modelo
// donde el renderer pone medianoche.
inline double modelFoF2(double cosChi, double latDeg, int doy, double f107) {
    const double kPi = 3.14159265358979323846;
    double lat = std::fabs(latDeg);
    // actividad: F10.7 70 -> ~x1.0, 200 -> ~x1.55
    double solar = 0.62 + 0.0046 * std::min(220.0, std::max(60.0, f107));
    // base diurna + reserva nocturna (recarga del vientre ecuatorial)
    double dayBase   = 3.2 + 7.8 * std::pow(std::max(cosChi, 0.0), 0.7);
    double nightBase = 1.6 + 1.8 * std::exp(-lat / 40.0);
    double t = smoothstep(-0.12, 0.15, cosChi);   // transicion crepuscular
    double fo = (1.0 - t) * nightBase + t * dayBase;
    // anomalia ecuatorial: cresta a +-15-16, valle en el ecuador geomagnetico
    double eq = std::exp(-std::pow((lat - 16.0) / 12.0, 2.0)) -
                0.55 * std::exp(-std::pow(lat / 20.0, 2.0));
    fo *= 1.0 + 0.32 * eq * t;
    // depresion invernal (N en invierno, S en verano) por inclinacion F2
    double seasonal = std::cos(((double)(doy - 172) / 365.0) * 2.0 * kPi) *
                      (latDeg > 0 ? 1.0 : -1.0);
    fo *= 1.0 + 0.06 * seasonal * (lat / 90.0);
    return fo * solar;
}

// hmF2 climatologico (km): sube de noche (plan 8.2).
inline double modelHmF2(double cosChi, double latDeg) {
    double lat = std::fabs(latDeg);
    double dayH = 250.0 - 0.35 * lat, nightH = 300.0 + 0.2 * lat;
    double t = smoothstep(-0.12, 0.15, cosChi);
    return (1.0 - t) * nightH + t * dayH;
}

