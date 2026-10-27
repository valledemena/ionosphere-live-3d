#include "SolarWindRiver.h"
#include "Data/SolarWindRamp.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdio>

// Puntos con color por vertice. El tamano se calcula en el shader con la
// distancia de vista, replicando sizeAttenuation de three.js (world units):
//   gl_PointSize = size_world * (H / (2*tan(fov/2))) / (-viewZ)
static const char* riverVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
uniform mat4 u_view, u_projection;
uniform float u_size, u_pixelScale;
out vec3 vColor;
void main() {
    vec4 vp = u_view * vec4(aPos, 1.0);
    gl_Position = u_projection * vp;
    float d = max(0.05, -vp.z);
    gl_PointSize = clamp(u_size * u_pixelScale / d, 1.0, 48.0);
    vColor = aColor;
}
)";

// Sprite redondo con borde suave: el blend aditivo del llamante multiplica por
// alpha, asi que u_alpha modula el brillo (nucleo 0.9, halo 0.16).
static const char* riverFragSrc = R"(
#version 400 core
in vec3 vColor;
uniform float u_alpha;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, u_alpha);
}
)";

namespace {

// Tamanos del web (size 0.17 / 0.38 R_E) y sus opacidades.
constexpr float kCoreSize = 0.17f;
constexpr float kCoreAlpha = 0.90f;
constexpr float kGlowSize = 0.38f;
constexpr float kGlowAlpha = 0.16f;

float roundKey(float v, float scale) { return std::round(v * scale) / scale; }

}  // namespace

SolarWindRiver::SolarWindRiver() {}
SolarWindRiver::~SolarWindRiver() { destroy(); }

void SolarWindRiver::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &riverVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &riverFragSrc, nullptr);
    glCompileShader(fs);

    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    // Layout: pos(3) + color(3) = 6 floats.
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts_), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Necesario para que el vertex shader controle el tamano de punto.
    glEnable(GL_PROGRAM_POINT_SIZE);

    colorsDirty_ = true;
    colorKeyValid_ = false;
    refreshRotation();
    fprintf(stderr, "[OK] SolarWindRiver initialized (%d particles)\n", swriver::kCount);
}

void SolarWindRiver::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (shader_) glDeleteProgram(shader_);
    vbo_ = vao_ = shader_ = 0;
    visible_ = false;
    colorsDirty_ = true;
    colorKeyValid_ = false;
}

void SolarWindRiver::setVisible(bool v) {
    if (v == visible_) return;
    visible_ = v;
    // Al encender hay que garantizar colores y posiciones en GPU.
    if (v) colorsDirty_ = true;
}

void SolarWindRiver::setSolarWind(float speedKms, float densityCm3, float bzNT) {
    if (std::isfinite(speedKms) && speedKms > 0.0f) speedKms_ = speedKms;
    if (std::isfinite(densityCm3) && densityCm3 >= 0.0f) densityCm3_ = densityCm3;
    if (std::isfinite(bzNT)) bzNT_ = bzNT;

    // Clave redondeada (0.1 km/s, 0.01 nT, 0.01 cm^-3): el color solo se
    // recalcula cuando el viento cambia de verdad.
    const float ks = roundKey(speedKms_, 10.0f);
    const float kb = roundKey(bzNT_, 100.0f);
    const float kd = roundKey(densityCm3_, 100.0f);
    if (!colorKeyValid_ || ks != keySpeed_ || kb != keyBz_ || kd != keyDensity_) {
        keySpeed_ = ks;
        keyBz_ = kb;
        keyDensity_ = kd;
        colorKeyValid_ = true;
        colorsDirty_ = true;
    }
}

void SolarWindRiver::setSunDirection(const glm::vec3& sunWorld) {
    const float len = glm::length(sunWorld);
    if (!std::isfinite(len) || len < 1e-6f) return;
    const glm::vec3 s = sunWorld / len;
    if (glm::dot(s, sun_) < 0.99995f) {   // ~0.57 grados, umbral de las otras capas
        sun_ = s;
        refreshRotation();
    }
}

// Rotacion que lleva +x (eje solar del sim) a la direccion del Sol viva.
void SolarWindRiver::refreshRotation() {
    const glm::vec3 from(1.0f, 0.0f, 0.0f);
    const float c = glm::clamp(glm::dot(from, sun_), -1.0f, 1.0f);
    if (c > 0.999999f) {
        rot_ = glm::mat3(1.0f);
        return;
    }
    if (c < -0.999999f) {
        // Antiparalelo: media vuelta sobre z (el eje es degenerado).
        rot_ = glm::mat3(glm::rotate(glm::mat4(1.0f), swriver::kPi, glm::vec3(0.0f, 0.0f, 1.0f)));
        return;
    }
    const glm::vec3 axis = glm::normalize(glm::cross(from, sun_));
    rot_ = glm::mat3(glm::rotate(glm::mat4(1.0f), std::acos(c), axis));
}

void SolarWindRiver::refreshColors() {
    for (int i = 0; i < swriver::kCount; ++i) {
        // Desfase aureo por particula, igual que la web: (i*0.618034) mod 1.
        const float seed = std::fmod((float)i * 0.61803398875f, 1.0f);
        const glm::vec3 c = windramp::webColor(speedKms_, bzNT_, densityCm3_, seed);
        verts_[i * 6 + 3] = c.r;
        verts_[i * 6 + 4] = c.g;
        verts_[i * 6 + 5] = c.b;
    }
    colorsDirty_ = false;
}

void SolarWindRiver::update(float dt) {
    if (!visible_ || vao_ == 0) return;
    if (colorsDirty_) refreshColors();
    sim_.step(glm::clamp(dt, 0.0f, 0.1f), speedKms_, densityCm3_, bzNT_);
    upload();
}

void SolarWindRiver::upload() {
    const float* p = sim_.positions();
    for (int i = 0; i < swriver::kCount; ++i) {
        const glm::vec3 w = rot_ * glm::vec3(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
        verts_[i * 6 + 0] = w.x;
        verts_[i * 6 + 1] = w.y;
        verts_[i * 6 + 2] = w.z;
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts_), verts_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SolarWindRiver::render(const glm::mat4& view, const glm::mat4& projection, float pixelScale) {
    if (!visible_ || vao_ == 0 || shader_ == 0) return;

    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(shader_, "u_pixelScale"),
                pixelScale > 0.0f ? pixelScale : 1000.0f);

    const GLint sizeLoc = glGetUniformLocation(shader_, "u_size");
    const GLint alphaLoc = glGetUniformLocation(shader_, "u_alpha");

    glBindVertexArray(vao_);
    // Nucleo (core): puntos pequeños, opacidad alta.
    glUniform1f(sizeLoc, kCoreSize);    // 0.17 R_E (tamaño del web)
    glUniform1f(alphaLoc, kCoreAlpha);  // 0.90
    glDrawArrays(GL_POINTS, 0, swriver::kCount);
    // Halo (glow): puntos más grandes, baja opacidad, mismo color base.
    glUniform1f(sizeLoc, kGlowSize);    // 0.38 R_E
    glUniform1f(alphaLoc, kGlowAlpha);  // 0.16
    glDrawArrays(GL_POINTS, 0, swriver::kCount);
    glBindVertexArray(0);
}
