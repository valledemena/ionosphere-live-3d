#pragma once

#include <vector>
#include "Data/GiroAdapter.h"

// Fusion GIRO+DIAS para puntos (solo live; en replay solo GIRO con historia).
// Regla: GIRO valido gana (primaria probada); DIAS rellena huecos con coords
// resueltas contra el catalogo GIRO (el pager no trae lat/lon).
static std::vector<StationSample> mergeDias(const std::vector<StationSample>& giro,
                                            const std::vector<StationSample>& dias) {
    std::vector<StationSample> out = giro;
    for (const auto& d : dias) {
        if (!d.valid) continue;
        const StationSample* cat = nullptr;
        bool covered = false;
        for (const auto& g : giro) {
            if (g.code != d.code) continue;
            cat = &g;
            if (g.valid) covered = true;
            break;
        }
        if (covered || !cat) continue;
        StationSample c = d;
        c.latDeg = cat->latDeg;
        c.lonDeg = cat->lonDeg;
        c.name = cat->name;
        if (c.latDeg == 0.0f && c.lonDeg == 0.0f) continue;  // sin posicion
        out.push_back(c);
    }
    return out;
}

// M4R-A (FASE A): fusion GIRO+kc2g (solo en modo fallback). El catalogo
// GIRO conserva posiciones/nombres; kc2g rellena o actualiza datos por
// codigo cuando su fila es mas fresca (o la entrada no tiene dato).
// DIAS se fusiona encima despues (mergeDias: mismo patron de relleno).
static std::vector<StationSample> mergeKc2g(const std::vector<StationSample>& giro,
                                            const std::vector<StationSample>& kc2g) {
    std::vector<StationSample> out = giro;
    for (const auto& k : kc2g) {
        if (!k.valid) continue;
        bool replaced = false;
        for (auto& e : out) {
            if (e.code != k.code) continue;
            if (!e.valid || k.timeUtc >= e.timeUtc) e = k;   // kc2g fresco gana
            replaced = true;
            break;
        }
        if (!replaced) out.push_back(k);   // estacion nueva para el catalogo
    }
    return out;
}
