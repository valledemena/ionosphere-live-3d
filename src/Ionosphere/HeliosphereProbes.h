#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

// DW.34 V11b: marcadores ILUSTRATIVOS. Las sondas reales estan a ~170 UA y
// no caben a escala: se colocan a 60/75 R_E SOLO si fix.valid, y la
// distancia real sigue en el panel Voyager. La Luna NO tiene efemérides
// en vivo: su fase es un MODELO SINTETICO declarado (angulo = t*omega).
// Dos flags OFF, dos draws, sin FBOs nuevos.
class HeliosphereProbes {
public:
    HeliosphereProbes() = default;
    ~HeliosphereProbes() { destroy(); }

    void init();
    void setProbeStates(bool v1, bool v2);
    void setMoonTime(float seconds);
    void renderProbes(const glm::mat4& view, const glm::mat4& projection);
    void renderMoon(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    int probeVertexCount() const { return probeCount_ * 2; }
    int moonVertexCount() const { return moonVerts_; }
    static constexpr int kNewDrawsPerFrame = 2;

private:
    GLuint probeVao_ = 0, probeVbo_ = 0, probeShader_ = 0;
    GLuint moonVao_ = 0, moonVbo_ = 0, moonEbo_ = 0, moonShader_ = 0;
    int probeCount_ = 0;
    int moonVerts_ = 0;
    float moonAngle_ = 0.0f;
};
