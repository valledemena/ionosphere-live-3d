#include "SolarPosition.h"
#include <cmath>

namespace SolarPosition {

static const double PI = 3.14159265358979323846;
static const double DEG2RAD = PI / 180.0;

glm::vec3 getDirection(double utcSeconds) {
    // Day of year from J2000 epoch
    double j2000 = utcSeconds / 86400.0 + 10957.5; // days since J2000.0
    double T = j2000 / 36525.0; // centuries

    // Mean longitude (degrees)
    double L0 = fmod(280.46646 + 36000.76983 * T + 0.0003032 * T * T, 360.0);

    // Mean anomaly (degrees)
    double M = fmod(357.52911 + 35999.05029 * T - 0.0001537 * T * T, 360.0);
    double Mrad = M * DEG2RAD;

    // Equation of center
    double C = (1.914602 - 0.004817 * T) * sin(Mrad)
             + (0.019993 - 0.000101 * T) * sin(2.0 * Mrad)
             + 0.000289 * sin(3.0 * Mrad);

    // Sun's ecliptic longitude
    double lon = (L0 + C) * DEG2RAD;

    // Obliquity of ecliptic
    double obliquity = (23.439291 - 0.0130042 * T) * DEG2RAD;

    // Sun's direction in ecliptic coordinates
    float x = (float)cos(lon);
    float y = (float)(sin(lon) * cos(obliquity));
    float z = (float)(sin(lon) * sin(obliquity));

    // Earth-fixed frame: rotate by -GMST about z (Earth has turned +GMST
    // eastward underneath the Sun), then map equatorial (x, y, z-up) to our
    // mesh frame (-X = lon 0, +Z = lon 90E, +Y = north).
    // (El codigo anterior rotaba +GMST: longitudes espejadas.)
    double GMST = fmod(280.46061837 + 360.98564736629 * j2000 + 0.000387933 * T * T, 360.0);
    double rotation = GMST * DEG2RAD;
    float fx = x * (float)cos(rotation) + y * (float)sin(rotation);
    float fy = -x * (float)sin(rotation) + y * (float)cos(rotation);

    return glm::normalize(glm::vec3(-fx, z, fy));
}

}
