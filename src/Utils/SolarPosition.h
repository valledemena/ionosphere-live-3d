#pragma once
#include <glm/glm.hpp>

namespace SolarPosition {
    // Returns unit vector from Earth center to Sun (in Earth-fixed frame)
    glm::vec3 getDirection(double utcSeconds);
}
