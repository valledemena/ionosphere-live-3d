#pragma once
#include <glad/glad.h>

class Atmosphere {
public:
    void init();
    void render();
private:
    GLuint vao = 0, vbo = 0, ebo = 0;
    int indexCount = 0;
    // M6 (plan 9.4): LUT de transmittance 256x64 GL_RG32F — texel (odR, odM)
    // del camino punto->sol, horneada en init() (CPU, misma fisica que el
    // shader). render() la une a la unidad de textura 0.
    GLuint lutTex = 0;
};
