#pragma once
#include <glm/glm.hpp>

namespace SolarPosition {
    // Returns unit vector from Earth center to Sun (in Earth-fixed frame)
    glm::vec3 getDirection(double utcSeconds);
    // M11: longitud ecliptica aparente λs (deg [0,360)) — el interno (L0+C)
    // que ya computa getDirection, expuesto sin duplicar Meeus (receta GLM).
    double getApparentEclipticLonDeg(double utcSeconds);
}
