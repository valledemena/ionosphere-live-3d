#include "VanAllenBelts.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// Shader unico: posicion + color RGBA por vertice. El bloque de overlays de
// App.cpp usa blend aditivo (SRC_ALPHA, ONE), asi que alpha modula el brillo.
static const char* vanAllenVertSrc = R"(
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

static const char* vanAllenFragSrc = R"(
#version 400 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Constantes fisicas del port (belts.ts): L interno fijo y L externo por Kp.
constexpr float kInnerL = 1.5f;
constexpr float kDriftRate = 0.05f;          // rad/s (driftAngle, rate=0.05)

// Radios de tubo: eleccion de DIBUJO, no fisica. Engordan el toro para que
// se lea como banda; L sigue siendo el radio real del eje.
constexpr float kInnerTube = 0.28f;
constexpr float kOuterTube = 0.85f;

// Malla: K_MINOR secciones transversales x M_MAJOR segmentos, cada una
// cerrada como line loop (M_MAJOR segmentos = 2*M_MAJOR vertices).
constexpr int kMinorRings = 6;
constexpr int kMajorSegments = 96;

// Colores de la demo web: protones plata-acero, electrones ambar.
const glm::vec3 kInnerColor(0.72f, 0.78f, 0.92f);
const glm::vec3 kOuterColor(1.00f, 0.70f, 0.28f);

// Un vertice del layout interleaved: pos(3) + rgba(4).
void pushPoint(std::vector<float>& verts, float L, float rho, float y,
               float u, const glm::vec3& c, float a) {
    // u = longitud geografica (crece hacia el este); theta = pi - u segun la
    // convencion del motor. rho es el desplazamiento radial del tubo e y el
    // vertical (latitud aparente del anillo de seccion).
    const float theta = kPi - u;
    const float r = L + rho;
    verts.push_back(r * cosf(theta));
    verts.push_back(y);
    verts.push_back(r * sinf(theta));
    verts.push_back(c.r);
    verts.push_back(c.g);
    verts.push_back(c.b);
    verts.push_back(a);
}

// Emite un toro completo (kMinorRings * kMajorSegments * 2 vertices) con el
// eje en +Y geografico y brillo viajero a lo largo del angulo mayor.
void appendBelt(std::vector<float>& verts, float L, float tube,
                const glm::vec3& color, float baseAlpha, float phase) {
    for (int k = 0; k < kMinorRings; ++k) {
        const float v = 2.0f * kPi * (float)k / (float)kMinorRings;
        const float rho = tube * cosf(v);
        const float y = tube * sinf(v);
        for (int i = 0; i < kMajorSegments; ++i) {
            const float u0 = 2.0f * kPi * (float)i / (float)kMajorSegments;
            const float u1 = 2.0f * kPi * (float)(i + 1) / (float)kMajorSegments;
            // Nudos de brillo: tres lobulos que viajan con la fase de deriva.
            const float uMid = 0.5f * (u0 + u1);
            const float alpha = baseAlpha *
                (0.30f + 0.70f * (0.5f + 0.5f * cosf(3.0f * (uMid - phase))));
            pushPoint(verts, L, rho, y, u0, color, alpha);
            pushPoint(verts, L, rho, y, u1, color, alpha);
        }
    }
}

}  // namespace

VanAllenBelts::VanAllenBelts() {}
VanAllenBelts::~VanAllenBelts() { destroy(); }

void VanAllenBelts::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vanAllenVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &vanAllenFragSrc, nullptr);
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
    printf("[OK] VanAllenBelts initialized\n");
}

void VanAllenBelts::setVisible(bool v) {
    if (v != visible_) {
        visible_ = v;
        dirty_ = true;
    }
}

bool VanAllenBelts::visible() const { return visible_; }

void VanAllenBelts::setKp(float kp) {
    if (!std::isfinite(kp)) return;
    // Throttle: solo reconstruye si el L externo cambia algo visible.
    const float k = glm::clamp(kp, 0.0f, 9.0f);
    if (fabsf(k - kp_) > 1e-3f) {
        kp_ = k;
        dirty_ = true;
    }
}

void VanAllenBelts::setRbeGlow(float glow01) {
    if (!std::isfinite(glow01)) return;
    const float g = glm::clamp(glow01, 0.0f, 1.0f);
    if (fabsf(g - rbeGlow_) > 1e-3f) {
        rbeGlow_ = g;
        dirty_ = true;
    }
}

void VanAllenBelts::setTimeSeconds(float t) {
    if (!std::isfinite(t)) return;
    // La animacion solo necesita ~50 Hz de reconstruccion como mucho.
    if (fabsf(t - time_) > 0.02f) {
        time_ = t;
        dirty_ = true;
    }
}

float VanAllenBelts::kp() const { return kp_; }

float VanAllenBelts::outerL() const { return outerLForKp(kp_); }

float VanAllenBelts::outerLForKp(float kp) {
    // Port de beltOuterL: Kp acotado 0..9 -> L = 4.2..4.92 R_E.
    const float k = std::isfinite(kp) ? glm::clamp(kp, 0.0f, 9.0f) : 0.0f;
    return 4.2f + 0.08f * k;
}

void VanAllenBelts::update() {
    if (dirty_ && vao_ != 0) rebuild();
}

void VanAllenBelts::rebuild() {
    if (vao_ == 0) return;

    // Layout: pos(3) + rgba(4) = 7 floats. Interno primero, externo despues.
    std::vector<float> verts;
    verts.reserve(2 * kMinorRings * kMajorSegments * 2 * 7);

    innerFirst_ = innerCount_ = 0;
    outerFirst_ = outerCount_ = 0;

    // Las capas invisibles no reservan vertices: el render sale antes.
    if (visible_) {
        // ---- 1. Cinturon interno (protones, deriva al OESTE: signo -) ----
        innerFirst_ = 0;
        appendBelt(verts, kInnerL, kInnerTube, kInnerColor, 0.85f,
                   -kDriftRate * time_);
        innerCount_ = (int)verts.size() / 7 - innerFirst_;

        // ---- 2. Cinturon externo (electrones, deriva al ESTE: signo +) ----
        outerFirst_ = (int)verts.size() / 7;
        const float outerAlpha = 0.70f * (0.45f + 0.55f * rbeGlow_);
        appendBelt(verts, outerLForKp(kp_), kOuterTube, kOuterColor, outerAlpha,
                   kDriftRate * time_);
        outerCount_ = (int)verts.size() / 7 - outerFirst_;
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

void VanAllenBelts::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_ || shader_ == 0 || vao_ == 0) return;
    if (dirty_) rebuild();
    if (innerCount_ == 0 && outerCount_ == 0) return;

    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));

    glBindVertexArray(vao_);
    if (innerCount_ > 0) glDrawArrays(GL_LINES, innerFirst_, innerCount_);
    if (outerCount_ > 0) glDrawArrays(GL_LINES, outerFirst_, outerCount_);
    glBindVertexArray(0);
}

void VanAllenBelts::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (shader_) glDeleteProgram(shader_);
    vbo_ = vao_ = shader_ = 0;
    innerFirst_ = innerCount_ = 0;
    outerFirst_ = outerCount_ = 0;
}
