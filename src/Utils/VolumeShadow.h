#pragma once
// S-2 sombra del planeta (ruling 130 sec.6.2): espejo CPU del test
// rayo-esfera del shader del volumen. Misma matematica que raySphere() del
// GLSL: si el rayo desde la muestra P hacia el sol cruza la esfera interior
// (radio innerR), la muestra esta en sombra dura. Multiplicativo: el dimming
// cosChi de CPU (DensityVolume) se conserva y se combina.
// P vive en [innerR, outerR] por construccion del march (fuera de la esfera
// interior): cualquier interseccion queda delante (t0 > 0). Guarda 1e-4
// contra tangencias numericas (frontera = luz, como el gate estricto 126).
#include <cmath>

namespace volumeshadow {

// true = en sombra (emision extinguida). L debe estar normalizada.
inline bool shadowed(float px, float py, float pz,
                     float lx, float ly, float lz, float innerR) {
    const float b = px * lx + py * ly + pz * lz;
    const float c = px * px + py * py + pz * pz - innerR * innerR;
    const float disc = b * b - c;
    if (disc < 0.0f) return false;
    return (-b - std::sqrt(disc)) > 1e-4f;
}

}  // namespace volumeshadow
