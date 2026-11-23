#pragma once

#include <cmath>
#include <cstdio>
#include <string>

// Formatea un campo del perfil Ebro ("---" si NaN).
static std::string ebFmt(float v, int dec) {
    if (std::isnan(v)) return "---";
    char b[32];
    if (dec <= 0) snprintf(b, sizeof(b), "%.0f", v);
    else if (dec == 1) snprintf(b, sizeof(b), "%.1f", v);
    else snprintf(b, sizeof(b), "%.2f", v);
    return std::string(b);
}

// Igual para GIRO (ausente = 0).
static std::string giroFmt(float v, int dec) {
    if (!(v > 0.0f)) return "---";
    return ebFmt(v, dec);
}
