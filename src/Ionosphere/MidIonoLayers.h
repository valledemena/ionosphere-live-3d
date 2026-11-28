#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

// DW.30 V10b: tres capas altas analiticas, cada una con su flag OFF.
// Un draw aditivo por capa sobre la misma esfera UV, sin FBOs nuevos.
// Parametrisaciones visuales declaradas: no son modelos fisicos.
class MidIonoLayers {
public:
    MidIonoLayers() = default;
    ~MidIonoLayers() { destroy(); }

    void init();
    void setParams(float kp, float tecNorm, const glm::vec3& sunDir,
                   const glm::vec3& fieldAxis, float timeSec);
    void renderPlasmasphere(const glm::mat4& view, const glm::mat4& projection);
    void renderAnomaly(const glm::mat4& view, const glm::mat4& projection);
    void renderToi(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    // Numero de draws nuevos por frame: 3 con todo ON, 0 con todo OFF.
    static constexpr int kNewDrawsPerFrame = 3;

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLuint plasma_ = 0, anomaly_ = 0, toi_ = 0;
    int indexCount_ = 0;
    float kp_ = 2.0f, tec_ = 0.0f, time_ = 0.0f;
    glm::vec3 sunDir_ = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 fieldAxis_ = glm::vec3(0.0f, 1.0f, 0.0f);
};
