#pragma once
#include <glad/glad.h>

class Atmosphere {
public:
    void init();
    void render();
private:
    GLuint vao = 0, vbo = 0, ebo = 0;
    int indexCount = 0;
};
