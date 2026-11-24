#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/glm.hpp>
#include "Data/GiroAdapter.h"
#include "Data/RadioPropagationAdapter.h"
#include "Ionosphere/GridLayer.h"
#include "Ionosphere/LayerProfile.h"
#include "Ionosphere/DensityVolume.h"
#include "Utils/GiroHelpers.h"

// Interpola espacialmente las muestras GIRO (IDW) a una rejilla 72x72 con el
// mismo layout que GloTEC (compatible con GridLayer::updateFromGrid):
//   values[latIdx*72 + lonIdx], fila 0 = lat -90 (sur), col 0 = lon -180,
//   lat de -90..90 (paso 2.5), lon de -180..180 (paso 5).
// var: 0=foF2, 1=MUF, 2=hmF2, 3=NmF2.
static GridData giroInterpGrid(const std::vector<StationSample>& samples, int var, int w, int h) {
    const int W = w, H = h;
    GridData g;
    g.width = W;
    g.height = H;
    g.values.assign(W * H, 0.0f);
    g.valid = false;

    std::vector<StationSample> pts;
    // M4R-A: filas stale (kc2g 6-24 h / EG931-2024) fuera de los grids.
    for (const auto& s : samples) if (s.valid && !s.stale) pts.push_back(s);
    if (pts.empty()) return g;

    const float maxDist = 50.0f;   // radio de influencia en grados
    const float cellLat = 180.0f / H;   // 2.5
    const float cellLon = 360.0f / W;   // 5
    // Fondo = mediana global (en vez de 0): sin acantilados en oceanos.
    // Peso debil (~estacion a 16deg) para no aplanar los picos locales.
    const float wBg = 0.004f;
    std::vector<float> mvals;
    mvals.reserve(pts.size());
    for (const auto& p : pts) {
        float vv = giroValueOf(p, var);
        if (vv > 0.0f) mvals.push_back(vv);
    }
    float med = 0.0f;
    if (!mvals.empty()) {
        std::sort(mvals.begin(), mvals.end());
        med = mvals[mvals.size() / 2];
    }

    for (int latIdx = 0; latIdx < H; latIdx++) {
        float latG = -90.0f + cellLat * latIdx;
        for (int lonIdx = 0; lonIdx < W; lonIdx++) {
            float lonG = -180.0f + cellLon * lonIdx;
            double wSum = wBg, vSum = (double)wBg * med;
            for (const auto& p : pts) {
                float dlat = p.latDeg - latG;
                float dlon = p.lonDeg - lonG;
                if (dlon > 180.0f) dlon -= 360.0f;
                if (dlon < -180.0f) dlon += 360.0f;
                // distancia angular aproximada (grados)
                float d = sqrtf(dlat * dlat + dlon * dlon);
                if (d > maxDist) continue;
                float val = giroValueOf(p, var);
                if (val <= 0.0f) continue;
                float w = 1.0f / (d * d + 0.5f);   // IDW con suavizado
                wSum += w;
                vSum += (double)w * val;
            }
            g.values[latIdx * W + lonIdx] = (float)(vSum / wSum);
        }
    }
    g.valid = true;
    g.timestamp = 0.0;
    return g;
}

// Interpola LUF/FOT/Spread-F/MUF (Fase 5, via RadioPropagationAdapter) a una
// rejilla 72x72 con el mismo layout que GridLayer::updateFromGrid.
// var: 0=LUF, 1=FOT, 2=Spread-F, 3=MUF. kpProxy modula Spread-F; xrayFlux
// alimenta el termino SID del LUF (M7, plan 10 — Sauer-Wilkinson).
static GridData radioInterpGrid(const std::vector<StationSample>& samples, int var, int w, int h,
                                double utc, float kpProxy, float xrayFlux) {
    const int W = w, H = h;
    GridData g;
    g.width = W;
    g.height = H;
    g.values.assign(W * H, 0.0f);
    g.valid = false;

    std::vector<StationSample> pts;
    // M4R-A: filas stale (kc2g 6-24 h / EG931-2024) fuera de los grids.
    for (const auto& s : samples) if (s.valid && !s.stale) pts.push_back(s);
    if (pts.empty()) return g;

    const float maxDist = 50.0f;
    const float cellLat = 180.0f / H;
    const float cellLon = 360.0f / W;
    // Fondo = mediana global (sin acantilados en oceanos).
    const float wBg = 0.004f;
    std::vector<float> mvals;
    mvals.reserve(pts.size());
    for (const auto& p : pts) {
        float vv = RadioPropagationAdapter::stationValue(p, var, utc, kpProxy, xrayFlux);
        if (vv > 0.0f) mvals.push_back(vv);
    }
    float med = 0.0f;
    if (!mvals.empty()) {
        std::sort(mvals.begin(), mvals.end());
        med = mvals[mvals.size() / 2];
    }

    for (int latIdx = 0; latIdx < H; latIdx++) {
        float latG = -90.0f + cellLat * latIdx;
        for (int lonIdx = 0; lonIdx < W; lonIdx++) {
            float lonG = -180.0f + cellLon * lonIdx;
            double wSum = wBg, vSum = (double)wBg * med;
            for (const auto& p : pts) {
                float dlat = p.latDeg - latG;
                float dlon = p.lonDeg - lonG;
                if (dlon > 180.0f) dlon -= 360.0f;
                if (dlon < -180.0f) dlon += 360.0f;
                float d = sqrtf(dlat * dlat + dlon * dlon);
                if (d > maxDist) continue;
                float val = RadioPropagationAdapter::stationValue(p, var, utc, kpProxy, xrayFlux);
                if (val <= 0.0f) continue;
                float w = 1.0f / (d * d + 0.5f);
                wSum += w;
                vSum += (double)w * val;
            }
            g.values[latIdx * W + lonIdx] = (float)(vSum / wSum);
        }
    }
    g.valid = true;
    g.timestamp = utc;
    return g;
}

// Interpola perfiles de capa (IDW, patron/radio/peso como giroInterpGrid;
// M5: la distancia pasa a gran circulo — el grid 2D conserva la plana,
// fuera del 'Archivos' del plan 8, ver P6 del sello M5). Fondo = mediana
// por campo EXCEPTO foF2/hmF2, que mezclan el fondo climatologico
// modelFoF2/modelHmF2 por cercania (plan 8.2/8.3).
// sunDir: marco del motor, el MISMO vector que usan despues
// applyDayNight y buildDensityVolume (coherencia terminador-modelo).
static std::vector<LayerProfile> interpLayerProfiles(const std::vector<StationSample>& samples, int w, int h,
                                                     const glm::vec3& sunDir, double epoch, double f107) {
    std::vector<LayerProfile> out((size_t)w * h);
    std::vector<StationSample> pts;
    // M4R-A: filas stale (kc2g 6-24 h / EG931-2024) fuera del volumen.
    for (const auto& s : samples) if (s.valid && !s.stale) pts.push_back(s);
    if (pts.empty()) return out;

    const float maxDist = 50.0f;
    const float cellLat = 180.0f / h;
    const float cellLon = 360.0f / w;
    const float wBg = 0.004f;
    auto medOf = [&](float StationSample::*m) {
        std::vector<float> v;
        v.reserve(pts.size());
        // isfinite: el SAO trae 9999 = "sin dato" -> NaN (noche: foE/foF1),
        // y un NaN en la mediana envenenaria el fondo del volumen.
        for (const auto& p : pts) if (std::isfinite(p.*m) && p.*m > 0.0f) v.push_back(p.*m);
        if (v.empty()) return 0.0f;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    float fE = medOf(&StationSample::foE);
    float fF1 = medOf(&StationSample::foF1);
    float fF2 = medOf(&StationSample::foF2);
    const float mNmE  = fE  > 0.0f ? 1.24e10f * fE * fE : 0.0f;
    const float mNmF1 = fF1 > 0.0f ? 1.24e10f * fF1 * fF1 : 0.0f;
    const float mNmF2 = fF2 > 0.0f ? 1.24e10f * fF2 * fF2 : 0.0f;
    const float hmedF2 = medOf(&StationSample::hmF2);
    const float mB0 = sanitizeB0(medOf(&StationSample::B0));
    const float mB1 = sanitizeB1(medOf(&StationSample::B1));
    const int doy = dayOfYear(epoch);   // termino estacional del modelo (8.2)

    for (int latIdx = 0; latIdx < h; latIdx++) {
        float latG = -90.0f + cellLat * latIdx;
        for (int lonIdx = 0; lonIdx < w; lonIdx++) {
            float lonG = -180.0f + cellLon * lonIdx;
            double wNmE = wBg, vNmE = (double)wBg * mNmE;
            double wNmF1 = wBg, vNmF1 = (double)wBg * mNmF1;
            double wNmF2 = wBg, vNmF2 = (double)wBg * mNmF2;
            double wHm = wBg, vHm = (double)wBg * hmedF2;
            double wB0 = wBg, vB0 = (double)wBg * mB0;
            double wB1 = wBg, vB1 = (double)wBg * mB1;
            // M5/8.1: distancia gran circulo (haversine, en grados) en
            // lugar de la plana (error grande en latitudes altas). Radio
            // y peso del IDW intactos. nearDeg (8.3) se mide contra
            // TODAS las estaciones validas, no solo las de radio.
            double nearDeg = 180.0;
            for (const auto& p : pts) {
                double d = havDeg(latG, lonG, (double)p.latDeg, (double)p.lonDeg);
                if (d < nearDeg) nearDeg = d;
                if (d > maxDist) continue;
                double w = 1.0 / (d * d + 0.5);
                // usableFo (LayerProfile.h): finita y > 0 — NaN de noche no entra
                if (usableFo(p.foE))  { float v = 1.24e10f * p.foE * p.foE; wNmE += w; vNmE += w * v; }
                if (usableFo(p.foF1)) { float v = 1.24e10f * p.foF1 * p.foF1; wNmF1 += w; vNmF1 += w * v; }
                if (usableFo(p.foF2)) { float v = 1.24e10f * p.foF2 * p.foF2; wNmF2 += w; vNmF2 += w * v; }
                if (p.hmF2 > 0.0f) { wHm += w; vHm += w * p.hmF2; }
                if (p.B0 > 0.0f)   { wB0 += w; vB0 += w * p.B0; }
                if (p.B1 > 0.0f)   { wB1 += w; vB1 += w * p.B1; }
            }
            // M5/8.3: mezcla medicion<->modelo SOLO en foF2 y hmF2 (los
            // que gobiernan volumen y MUF). cosChi con la MISMA
            // expresion que applyDayNight / buildDensityVolume
            // (densityDir, marco del motor — P1 corregido por MUSE).
            double wG = smoothstep(55.0, 25.0, nearDeg);
            glm::vec3 nrm = densityDir(latG, lonG);
            double cosChi = (double)(nrm.x * sunDir.x + nrm.y * sunDir.y + nrm.z * sunDir.z);
            LayerProfile pr;
            pr.NmE = (float)(vNmE / wNmE);
            pr.hasE = pr.NmE > 0.0f;
            pr.NmF1 = (float)(vNmF1 / wNmF1);
            pr.hasF1 = pr.NmF1 > 0.0f;
            // P2 (sello): el IDW de F2 queda intacto (Nm, fondo mediana
            // wBg); la mezcla con el modelo es en MHz: sqrt -> blend ->
            // cuadrar. wG=1 deja la celda como hoy salvo ~1e-7 (V1).
            double nmF2Idw = vNmF2 / wNmF2;
            double hmF2Idw = vHm / wHm;
            double foF2Idw = (nmF2Idw > 0.0) ? std::sqrt(nmF2Idw / 1.24e10) : 0.0;
            double foF2Cell = wG * foF2Idw + (1.0 - wG) * modelFoF2(cosChi, (double)latG, doy, f107);
            pr.NmF2 = (float)(1.24e10 * foF2Cell * foF2Cell);
            double hmF2Cell = (hmF2Idw > 0.0)
                ? (wG * hmF2Idw + (1.0 - wG) * modelHmF2(cosChi, (double)latG))
                : modelHmF2(cosChi, (double)latG);
            pr.hmF2 = (float)hmF2Cell;
            pr.hasF2 = pr.NmF2 > 0.0f && pr.hmF2 > 0.0f;
            pr.hmF1 = pr.hmF2 > 0.0f ? std::max(150.0f, 0.8f * pr.hmF2) : 180.0f;
            pr.B0 = sanitizeB0((float)(vB0 / wB0));
            pr.B1 = sanitizeB1((float)(vB1 / wB1));
            out[(size_t)latIdx * w + lonIdx] = pr;
        }
    }
    return out;
}
