#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

class Terminator {
public:
    void init();
    // Reconstruye el anillo en el plano perpendicular a sunDir (linea real
    // dia/noche). Barato (129 vertices); llamar cuando cambie el sol.
    void update(const glm::vec3& sunDir);
    void render();
private:
    GLuint vao = 0, vbo = 0;
    int vertexCount = 0;
};
