#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

class Terminator {
public:
    void init();
    void render();
private:
    GLuint vao = 0, vbo = 0;
    int vertexCount = 0;
};
