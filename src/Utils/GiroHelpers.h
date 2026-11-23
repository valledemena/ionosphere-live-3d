#pragma once

#include <vector>
#include "Data/GiroAdapter.h"
#include "Ionosphere/IonosondeLayer.h"

// Convierte muestras GIRO a puntos de render para la capa de ionosondas.
static std::vector<StationPoint> giroPointsFrom(const std::vector<StationSample>& src) {
    std::vector<StationPoint> out;
    for (const auto& s : src) {
        StationPoint p;
        p.latDeg = s.latDeg;
        p.lonDeg = s.lonDeg;
        p.foF2 = s.foF2;
        p.muf = s.muf;
        p.hmF2 = s.hmF2;
        p.nmF2 = s.nmF2;
        p.foF1 = s.foF1;
        p.foE = s.foE;
        p.foEs = s.foEs;
        p.fxI = s.fxI;
        p.hF2 = s.hF2;
        p.hF = s.hF;
        p.hE = s.hE;
        p.hEs = s.hEs;
        p.B0 = s.B0;
        p.B1 = s.B1;
        p.fmin = s.fmin;
        p.code = s.code;
        p.name = s.name;
        p.valid = s.valid;
        p.timeUtc = s.timeUtc;   // M4R-A: edad de la fila (marca en paneles)
        p.stale = s.stale;       // M4R-A: kc2g 6-24 h o EG931 (panel si, grids no)
        out.push_back(p);
    }
    return out;
}

// Valor de una magnitud GIRO por indice de variable (0=foF2,1=MUF,2=hmF2,3=NmF2).
static float giroValueOf(const StationSample& s, int var) {
    switch (var) {
        case 1: return s.muf;
        case 2: return s.hmF2;
        case 3: return s.nmF2;
        default: return s.foF2;
    }
}
