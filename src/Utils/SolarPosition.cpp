#include "SolarPosition.h"
#include <cmath>

namespace SolarPosition {

static const double PI = 3.14159265358979323846;
static const double DEG2RAD = PI / 180.0;

// Meeus compartido: longitud ecliptica aparente (rad, sin normalizar).
// H1-261 (espejo web P1-261, veredicto 262): Unix->J2000 son MENOS
// 10957.5 dias. El + computaba el sol 60 anos julianos adelante;
// lambda y GMST se cancelaban en longitud pero la declinacion quedaba
// ~0.17 desviada. Mismo fix en getDirection (ambos sitios).
static double eclipticLonRad(double utcSeconds) {
    double j2000 = utcSeconds / 86400.0 - 10957.5;
    double T = j2000 / 36525.0;
    double L0 = fmod(280.46646 + 36000.76983 * T + 0.0003032 * T * T, 360.0);
    double M = fmod(357.52911 + 35999.05029 * T - 0.0001537 * T * T, 360.0);
    double Mrad = M * DEG2RAD;
    double C = (1.914602 - 0.004817 * T) * sin(Mrad)
             + (0.019993 - 0.000101 * T) * sin(2.0 * Mrad)
             + 0.000289 * sin(3.0 * Mrad);
    return (L0 + C) * DEG2RAD;
}

double getApparentEclipticLonDeg(double utcSeconds) {
    double lon = eclipticLonRad(utcSeconds) / DEG2RAD;
    lon = fmod(lon, 360.0);
    if (lon < 0.0) lon += 360.0;
    return lon;
}

glm::vec3 getDirection(double utcSeconds) {
    // Day of year from J2000 epoch (H1-261 espejo: MENOS, ver arriba).
    double j2000 = utcSeconds / 86400.0 - 10957.5; // days since J2000.0
    double T = j2000 / 36525.0; // centuries

    // Sun's ecliptic longitude (M11: helper compartido, mismo Meeus)
    double lon = eclipticLonRad(utcSeconds);

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
