#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

// RefShells: anillos de referencia de altura (estilo imagen 3 educativa):
//   E  = 110 km (verde), F1 = 200 km (amarillo), F2 = 300 km (naranja).
// Cada altitud dibuja ecuador + paralelos +/-30/+/-60 + meridianos cada 60deg
// como GL_LINES tenues. Solo referencia visual (sin datos).
class RefShells {
public:
    RefShells();
    ~RefShells();

    void init();
    // Alturas reales (km): reconstruye los anillos. E suele ser fijo (110),
    // F1/F2 siguen la mediana medida (hmF2). Llamar cuando cambien >5 km.
    void updateAltitudes(float eKm, float f1Km, float f2Km);
    void render(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    void setVisible(bool v) { visible_ = v; }
    bool isVisible() const { return visible_; }

private:
    void rebuild();
    glm::vec3 latLonToPos(float latDeg, float lonDeg, float r) const;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;
    int vertexCount_ = 0;
    bool visible_ = false;
    float altE_ = 110.0f, altF1_ = 200.0f, altF2_ = 300.0f;
};
