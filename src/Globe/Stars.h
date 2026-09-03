#pragma once
#include <glad/glad.h>

class Stars {
public:
    void init();
    void render();
private:
    GLuint vao = 0, vbo = 0;
    int count = 0;
};
