#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

// DW.33 V11a: vista heliosferica. Espiral de Parker PARAMETRICA (no MHD):
//   r(phi) = r0 + k*phi, k = (v_sw/omega)*kCompression, con omega y AU_KM
//   tomados de parker:: (Data/ParkerMath.h), v_sw = 400 km/s declarado y
//   kCompression = 0.129 -> k_visual ~ 18 R_E/rad. La compresion es VISUAL
//   (1 UA no cabe en la vista en R_E) y se declara en el acta.
// 12 brazos x 64 segmentos en un unico VBO y UN draw (GL_LINES).
// Marco solar, rotado a la direccion del Sol viva en el vertex shader.
class HeliosphereView {
public:
    HeliosphereView() = default;
    ~HeliosphereView() { destroy(); }

    void init();
    void setSunDirection(const glm::vec3& sunWorld);
    void render(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    static constexpr int kArms = 12;
    static constexpr int kSegments = 64;
    static constexpr int kNewDrawsPerFrame = 1;
    int vertexCount() const { return vertexCount_; }

private:
    GLuint vao_ = 0, vbo_ = 0, shader_ = 0;
    int vertexCount_ = 0;
    glm::mat3 rot_ = glm::mat3(1.0f);
};
