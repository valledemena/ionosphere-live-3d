#pragma once
// S-3 jitter del punto de entrada (ruling 130 sec.6.3): espejo CPU del
// offset espacial del shader del volumen. IGN determinista (misma formula
// en ambos lados): offset en [0,1) SOLO del punto de entrada del rayo.
// SIN componente temporal (el motor no tiene TAA; acumulador temporal
// PROHIBIDO por la spec): mismos coords -> mismo offset siempre.
#include <cmath>

namespace volumejitter {

// Offset en [0,1) para el pixel (x, y). IGN (Jimenez 2014).
inline float entryOffset(float x, float y) {
    const float d = x * 0.06711056f + y * 0.00583715f;
    const float f = d - std::floor(d);
    const float g = 52.9829189f * f;
    return g - std::floor(g);
}

}  // namespace volumejitter
