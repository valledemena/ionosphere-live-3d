#pragma once
#include <glad/glad.h>

class Earth {
public:
    void init();
    void render();
    void destroy();

private:
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLuint texture = 0;
    GLuint nightTexture = 0;   // Black Marble (luces nocturnas, textures/earth-night.jpg)
    int indexCount = 0;
};
