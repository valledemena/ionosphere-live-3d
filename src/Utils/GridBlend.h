#pragma once

#include <glm/glm.hpp>
#include "Ionosphere/GridLayer.h"

// Mezcla lineal de dos grids TEC (mismo tamano) para el replay temporal.
static GridData blendGrids(const GridData& a, const GridData& b, double f) {
    if (!a.valid) return b;
    if (!b.valid) return a;
    if (a.width != b.width || a.height != b.height || a.values.size() != b.values.size()) return a;
    GridData g;
    g.width = a.width;
    g.height = a.height;
    g.values.resize(a.values.size());
    float fb = (float)glm::clamp(f, 0.0, 1.0);
    for (size_t i = 0; i < a.values.size(); i++)
        g.values[i] = a.values[i] * (1.0f - fb) + b.values[i] * fb;
    g.timestamp = a.timestamp * (1.0 - f) + b.timestamp * f;
    g.valid = true;
    return g;
}
