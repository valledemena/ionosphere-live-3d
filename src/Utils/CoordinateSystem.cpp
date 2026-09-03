#include "CoordinateSystem.h"
#include <cmath>

namespace CoordSys {

static const float DEG2RAD = 3.14159265f / 180.0f;
static const float RAD2DEG = 180.0f / 3.14159265f;

glm::vec3 latLonToUnit(float lat, float lon) {
    float latR = lat * DEG2RAD;
    float lonR = lon * DEG2RAD;
    float x = cosf(latR) * cosf(lonR);
    float y = sinf(latR);
    float z = -cosf(latR) * sinf(lonR);
    return glm::vec3(x, y, z);
}

glm::vec3 latLonAltToPos(float lat, float lon, float altKm) {
    float r = (EARTH_RADIUS_KM + altKm) / EARTH_RADIUS_KM; // normalized to sphere radius 1
    return latLonToUnit(lat, lon) * r;
}

glm::vec2 posToLatLon(const glm::vec3& pos) {
    glm::vec3 n = glm::normalize(pos);
    float lat = asinf(n.y) * RAD2DEG;
    float lon = atan2f(-n.z, n.x) * RAD2DEG;
    return glm::vec2(lat, lon);
}

}
