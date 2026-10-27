#include "WestFordRing.h"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

// Shader unico: posicion + color RGBA por vertice. gl_PointSize es obligatorio
// en core 4.0 cuando GL_PROGRAM_POINT_SIZE esta activo; u_pointSize lo fija.
static const char* wfVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 u_view, u_projection;
uniform float u_pointSize;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_PointSize = u_pointSize;
    gl_Position = u_projection * u_view * vec4(aPos, 1.0);
}
)";

static const char* wfFragSrc = R"(
#version 400 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kEarthRadiusKm = 6371.0;

// Constantes portadas verbatim de ring1963.ts (declaradas, no datos vivos).
constexpr int kRingAltKmMin = 3600;
constexpr int kRingAltKmMax = 3680;
constexpr double kRingIncDeg = 87.4;
constexpr double kRingNodeDeg = 0.0;      // ilustrativo: no hay RAAN historico
constexpr double kRingJitterDeg = 0.6;
[[maybe_unused]] constexpr double kRingDipoles = 4.8e8;      // documentacion
[[maybe_unused]] constexpr double kRingDipoleLengthM = 0.0178;  // 1.78 cm
constexpr int kRingParticles = 4096;
constexpr uint32_t kRingSeed = 19630509u;

// Color y brillo de la banda (alpha = brillo con blend aditivo del llamante).
constexpr float kRingColorR = 0.62f;
constexpr float kRingColorG = 0.86f;
constexpr float kRingColorB = 1.00f;
constexpr float kRingColorA = 0.55f;

// LCG determinista mulberry32, identico al de la demo web: aritmetica de
// 32 bits con envolvente (a >>> 0 / Math.imul) y salida en [0, 1).
struct Mulberry32 {
    uint32_t a = kRingSeed;

    double next() {
        a += 0x6d2b79f5u;
        uint32_t t = a;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return (double)(t ^ (t >> 14)) / 4294967296.0;
    }
};

// Construye las 4096 muestras de la banda. El orden de llamadas al LCG debe
// ser exactamente el del TS: node, inc, altKm, u (nube reproducible byte a
// byte). Se calcula en double, como JS, y se guarda en float.
void buildParticles(std::vector<float>& out) {
    out.clear();
    out.reserve((size_t)kRingParticles * 7);

    Mulberry32 rnd;
    for (int i = 0; i < kRingParticles; i++) {
        const double node = (kRingNodeDeg + (rnd.next() - 0.5) * 2.0 * kRingJitterDeg) * kDeg;
        const double inc = (kRingIncDeg + (rnd.next() - 0.5) * 2.0 * kRingJitterDeg) * kDeg;
        const double altKm = (double)kRingAltKmMin +
                             rnd.next() * (double)(kRingAltKmMax - kRingAltKmMin);
        const double r = 1.0 + altKm / kEarthRadiusKm;
        const double u = rnd.next() * kPi * 2.0;

        // Normal del plano orbital (y = norte): inclinacion desde el eje polar.
        const double nx = sin(inc) * cos(node);
        const double ny = cos(inc);
        const double nz = sin(inc) * sin(node);

        // Base ortonormal del plano: e1 = y x n / |..|, e2 = n x e1.
        double e1x = nz;
        double e1y = 0.0;
        double e1z = -nx;
        const double e1n = std::hypot(e1x, e1y, e1z);
        const double e1d = (e1n != 0.0) ? e1n : 1.0;
        e1x /= e1d;
        e1y /= e1d;
        e1z /= e1d;
        const double e2x = ny * e1z - nz * e1y;
        const double e2y = nz * e1x - nx * e1z;
        const double e2z = nx * e1y - ny * e1x;

        const double cu = cos(u);
        const double su = sin(u);
        const double wx = r * (cu * e1x + su * e2x);
        const double wy = r * (cu * e1y + su * e2y);
        const double wz = r * (cu * e1z + su * e2z);

        // Marco web Y-up -> motor: engine = (-wx, wy, -wz).
        out.push_back((float)(-wx));
        out.push_back((float)wy);
        out.push_back((float)(-wz));
        out.push_back(kRingColorR);
        out.push_back(kRingColorG);
        out.push_back(kRingColorB);
        out.push_back(kRingColorA);
    }
}

}  // namespace

WestFordRing::WestFordRing() {}
WestFordRing::~WestFordRing() { destroy(); }

void WestFordRing::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &wfVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &wfFragSrc, nullptr);
    glCompileShader(fs);

    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    // Geometria estatica: se construye y sube una sola vez.
    std::vector<float> verts;
    buildParticles(verts);
    count_ = (int)(verts.size() / 7);

    // Layout: pos(3) + rgba(4) = 7 floats.
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Necesario para que el vertex shader controle el tamano de punto.
    glEnable(GL_PROGRAM_POINT_SIZE);

    printf("[OK] WestFordRing initialized\n");
}

void WestFordRing::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (shader_) glDeleteProgram(shader_);
    vbo_ = vao_ = shader_ = 0;
    visible_ = false;
    pointSize_ = 2.0f;
    count_ = 0;
}

void WestFordRing::setVisible(bool v) { visible_ = v; }
bool WestFordRing::visible() const { return visible_; }

void WestFordRing::setPointSize(float px) { pointSize_ = glm::clamp(px, 1.0f, 6.0f); }

int WestFordRing::particleCount() const { return count_; }

const char* WestFordRing::label() {
    return "West Ford 1963 - reconstruccion declarada (sin datos vivos)";
}

void WestFordRing::update() {
    // Sin animacion ni rebuild: la banda es estatica a proposito.
}

void WestFordRing::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_) return;
    if (count_ == 0 || vao_ == 0 || shader_ == 0) return;

    // Defensivo: el llamante no debe tener que fijar el tamano de punto.
    glPointSize(pointSize_);

    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(shader_, "u_pointSize"), pointSize_);

    glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, count_);
    glBindVertexArray(0);
}
