#pragma once
#include <glm/glm.hpp>

namespace CoordSys {
    // Convert lat/lon (degrees) to unit vector on sphere
    glm::vec3 latLonToUnit(float lat, float lon);

    // Convert lat/lon/altitude (km) to position (Earth radius = 6371 km)
    glm::vec3 latLonAltToPos(float lat, float lon, float altKm);

    // Convert unit vector back to lat/lon (degrees)
    glm::vec2 posToLatLon(const glm::vec3& pos);

    constexpr float EARTH_RADIUS_KM = 6371.0f;
}
