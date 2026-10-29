#pragma once
#include <glm/glm.hpp>

namespace CoordSys {
    // DEPRECATED (bug 180 del bucket en vivo, post-M8): este namespace usa
    // +X=lon0; el marco del motor es densityDir (DensityVolume.h, theta =
    // pi - lon) — 180 grados de diferencia sobre Y. Cero llamadas desde el
    // fix del ribbon/marcadores HF; posiciones mundo = densityDir *
    // (1 + alt/6371). No usar en codigo nuevo.
    // Convert lat/lon (degrees) to unit vector on sphere
    glm::vec3 latLonToUnit(float lat, float lon);

    // Convert lat/lon/altitude (km) to position (Earth radius = 6371 km)
    glm::vec3 latLonAltToPos(float lat, float lon, float altKm);

    // Convert unit vector back to lat/lon (degrees)
    glm::vec2 posToLatLon(const glm::vec3& pos);

    constexpr float EARTH_RADIUS_KM = 6371.0f;
}
