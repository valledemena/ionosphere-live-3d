#include "CmeLayer.h"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

// Shader unico: posicion + color RGBA por vertice (mismo layout que
// GeospaceOverlays: 7 floats). El bloque de overlays de App.cpp fija el
// blend (aditivo) y el depth; aqui no se toca ninguno de los dos.
static const char* cmeVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 u_view, u_projection;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = u_projection * u_view * vec4(aPos, 1.0);
}
)";

static const char* cmeFragSrc = R"(
#version 400 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kFrontBase = 1.8f;    // R_E dibujadas al inicio del transito
constexpr float kFrontSpan = 17.7f;   // 1.8 + 17.7 = 19.5 R_E a la llegada
constexpr float kAxisMin = 1.5f;      // guia: no entra en el Sol
constexpr float kAxisMax = 19.5f;
constexpr float kDashOn = 0.35f;
constexpr float kDashOff = 0.25f;
constexpr int kFrontSeg = 64;         // circulo del casquete
constexpr int kMeridians = 4;         // arcos eje -> circulo
constexpr int kMeridianSeg = 12;

}  // namespace

CmeLayer::CmeLayer() {}
CmeLayer::~CmeLayer() { destroy(); }

void CmeLayer::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &cmeVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &cmeFragSrc, nullptr);
    glCompileShader(fs);

    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    dirty_ = true;
    printf("[OK] CmeLayer initialized\n");
}

void CmeLayer::setVisible(bool v) {
    if (visible_ == v) return;
    visible_ = v;
    dirty_ = true;
}

void CmeLayer::setEvent(const CmeEvent& ev, bool hasEvent) {
    bool changed = (hasEvent != hasEvent_);
    if (!changed && hasEvent) {
        changed = (ev.arrivalUtc != ev_.arrivalUtc) ||
                  (ev.halfAngleDeg != ev_.halfAngleDeg) ||
                  (ev.hasHalfAngle != ev_.hasHalfAngle) ||
                  (ev.simId != ev_.simId);
    }
    if (!changed) return;
    ev_ = ev;
    hasEvent_ = hasEvent;
    dirty_ = true;
}

void CmeLayer::setSunDirection(const glm::vec3& sunWorld) {
    float len = glm::length(sunWorld);
    if (len < 1e-6f) return;   // vector nulo: se conserva la ultima direccion
    glm::vec3 s = sunWorld / len;
    if (glm::dot(s, sun_) < 0.99995f) {   // ~0.57 grados
        sun_ = s;
        dirty_ = true;
    }
}

void CmeLayer::setProgress(float p01) {
    if (!std::isfinite(p01)) return;
    float p = p01 < 0.0f ? 0.0f : (p01 > 1.0f ? 1.0f : p01);
    if (fabsf(p - progress_) < 0.0001f) return;
    progress_ = p;
    dirty_ = true;
}

float CmeLayer::frontRadius() const {
    // Compresion DECLARADA, lineal sobre el progreso (no es fisica): el
    // transito real de 1 UA (~215 R_E) no cabe en el zoom maximo.
    return kFrontBase + kFrontSpan * progress_;
}

void CmeLayer::update() {
    if (dirty_ && vao_ != 0) rebuild();
}

void CmeLayer::rebuild() {
    if (vao_ == 0) return;

    // Layout: pos(3) + rgba(4) = 7 floats.
    std::vector<float> verts;
    verts.reserve(3000 * 7);

    auto push = [&](const glm::vec3& p, const glm::vec4& c) {
        verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
        verts.push_back(c.r); verts.push_back(c.g); verts.push_back(c.b); verts.push_back(c.a);
    };

    axisFirst_ = axisCount_ = 0;
    frontFirst_ = frontCount_ = 0;

    glm::vec3 axis = (glm::length(sun_) < 1e-6f) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                 : glm::normalize(sun_);

    // ---- 1. Guia del eje Sol-Tierra (discontinua, tenue) ----
    {
        axisFirst_ = (int)verts.size() / 7;
        const glm::vec4 guide(0.55f, 0.55f, 0.65f, 0.35f);
        const float step = kDashOn + kDashOff;
        for (float t = kAxisMin; t < kAxisMax; t += step) {
            float t1 = t + kDashOn;
            if (t1 > kAxisMax) t1 = kAxisMax;
            push(t * axis, guide);
            push(t1 * axis, guide);
        }
        axisCount_ = (int)verts.size() / 7 - axisFirst_;
    }

    // ---- 2. Frente de choque: casquete esferico centrado en +axis ----
    if (hasEvent_) {
        frontFirst_ = (int)verts.size() / 7;

        // Base del casquete: e1, e2 perpendiculares al eje solar.
        glm::vec3 helper = (fabsf(axis.y) < 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 e1 = glm::normalize(glm::cross(axis, helper));
        glm::vec3 e2 = glm::cross(axis, e1);

        // halfAngle publicado por ENLIL (45 grados si falta). Acotado para
        // que el casquete no degenere ni pase de hemisferio.
        float halfDeg = ev_.hasHalfAngle ? ev_.halfAngleDeg : 45.0f;
        if (!std::isfinite(halfDeg)) halfDeg = 45.0f;
        halfDeg = halfDeg < 5.0f ? 5.0f : (halfDeg > 90.0f ? 90.0f : halfDeg);
        const float a = glm::radians(halfDeg);

        auto capPoint = [&](float r, float ang, float b) {
            return r * (cosf(ang) * axis + sinf(ang) * (cosf(b) * e1 + sinf(b) * e2));
        };

        const bool arrived = progress_ >= 1.0f;
        const glm::vec4 hot(1.00f, 0.72f, 0.30f, arrived ? 0.30f : 0.65f);
        const float r = frontRadius();

        // Circulo limitrofe del casquete.
        for (int i = 0; i < kFrontSeg; i++) {
            float b0 = 2.0f * kPi * (float)i / (float)kFrontSeg;
            float b1 = 2.0f * kPi * (float)(i + 1) / (float)kFrontSeg;
            push(capPoint(r, a, b0), hot);
            push(capPoint(r, a, b1), hot);
        }

        // Segundo circulo apenas por delante: sugiere grosor del frente.
        if (!arrived) {
            const glm::vec4 shell(1.00f, 0.72f, 0.30f, 0.25f);
            for (int i = 0; i < kFrontSeg; i++) {
                float b0 = 2.0f * kPi * (float)i / (float)kFrontSeg;
                float b1 = 2.0f * kPi * (float)(i + 1) / (float)kFrontSeg;
                push(capPoint(r * 1.02f, a, b0), shell);
                push(capPoint(r * 1.02f, a, b1), shell);
            }
        }

        // Cuatro meridianos desde el eje hasta el circulo.
        for (int m = 0; m < kMeridians; m++) {
            float b = 2.0f * kPi * (float)m / (float)kMeridians;
            for (int i = 0; i < kMeridianSeg; i++) {
                float a0 = a * (float)i / (float)kMeridianSeg;
                float a1 = a * (float)(i + 1) / (float)kMeridianSeg;
                push(capPoint(r, a0, b), hot);
                push(capPoint(r, a1, b), hot);
            }
        }

        frontCount_ = (int)verts.size() / 7 - frontFirst_;
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    dirty_ = false;
}

void CmeLayer::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_ || !hasEvent_) return;
    if (dirty_) rebuild();
    if (shader_ == 0 || (axisCount_ == 0 && frontCount_ == 0)) return;

    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE,
                       glm::value_ptr(projection));

    glBindVertexArray(vao_);
    if (axisCount_ > 0) glDrawArrays(GL_LINES, axisFirst_, axisCount_);
    if (frontCount_ > 0) glDrawArrays(GL_LINES, frontFirst_, frontCount_);
    glBindVertexArray(0);
}

void CmeLayer::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (shader_) glDeleteProgram(shader_);
    vbo_ = vao_ = shader_ = 0;
    axisFirst_ = axisCount_ = 0;
    frontFirst_ = frontCount_ = 0;
}
