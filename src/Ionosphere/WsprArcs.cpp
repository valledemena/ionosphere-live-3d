#include "WsprArcs.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Shader unico: posicion + color RGBA por vertice (mismo contrato que
// GeospaceOverlays; App.cpp decide el estado de blend/depth del bloque).
static const char* wsprVertSrc = R"(
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

static const char* wsprFragSrc = R"(
#version 400 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kSegments = 32;       // segmentos por arco (2 vertices cada uno)
constexpr size_t kMaxArcs = 40;     // SPOT_CAP
constexpr float kBaseRadius = 1.03f;   // altura ionosferica dibujada (~190 km)
constexpr float kBulge = 0.06f;        // abombamiento maximo de trayectos largos
constexpr float kAlphaBase = 0.80f;
constexpr float kTtlSec = 600.0f;      // 10 min (vida declarada del arco)

}  // namespace

WsprArcs::WsprArcs() {}
WsprArcs::~WsprArcs() { destroy(); }

void WsprArcs::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &wsprVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &wsprFragSrc, nullptr);
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
    printf("[OK] WsprArcs initialized\n");
}

void WsprArcs::setVisible(bool v) {
    if (visible_ != v) {
        visible_ = v;
        dirty_ = true;   // el rebuild decide si hay algo que dibujar
    }
}

void WsprArcs::bandColor(const std::string& band, glm::vec3& rgb) {
    // Port literal del mapa de colores de wspr.ts (fijo, declarado en el TU).
    if (band == "160m")      rgb = glm::vec3(0.55f, 0.35f, 1.00f);
    else if (band == "80m")  rgb = glm::vec3(0.35f, 0.50f, 1.00f);
    else if (band == "60m")  rgb = glm::vec3(0.30f, 0.80f, 1.00f);
    else if (band == "40m")  rgb = glm::vec3(0.20f, 0.90f, 0.70f);
    else if (band == "30m")  rgb = glm::vec3(0.30f, 0.95f, 0.40f);
    else if (band == "20m")  rgb = glm::vec3(1.00f, 0.85f, 0.20f);
    else if (band == "17m")  rgb = glm::vec3(1.00f, 0.65f, 0.15f);
    else if (band == "15m")  rgb = glm::vec3(1.00f, 0.45f, 0.10f);
    else if (band == "12m")  rgb = glm::vec3(1.00f, 0.30f, 0.30f);
    else if (band == "10m")  rgb = glm::vec3(1.00f, 0.20f, 0.50f);
    else if (band == "6m")   rgb = glm::vec3(0.90f, 0.30f, 1.00f);
    else                     rgb = glm::vec3(0.60f, 0.60f, 0.60f);   // desconocida
}

glm::vec3 WsprArcs::latLonToUnit(float latDeg, float lonDeg) {
    // Misma convencion que RefShells/GridLayer/GeospaceOverlays.
    float phi = kPi * 0.5f - glm::radians(latDeg);
    float theta = kPi - glm::radians(lonDeg);
    float sn = sinf(phi);
    return glm::vec3(sn * cosf(theta), cosf(phi), sn * sinf(theta));
}

void WsprArcs::setSnapshot(const std::vector<WsprSpot>& spots, double nowUtc) {
    nowUtc_ = nowUtc;

    double maxT = 0.0;
    for (const WsprSpot& s : spots) maxT = std::max(maxT, s.timeUtc);

    // Tramo de fade de 1 min: permite que el decaimiento de 10 min se vea sin
    // reconstruir en cada frame (el rebuild es barato: <= 2560 vertices).
    int step = -1;
    if (!spots.empty()) {
        double age = std::max(0.0, nowUtc - maxT);
        step = (int)(age / 60.0);
    }

    bool changed = !haveSnapshot_ || spots.size() != lastSpotCount_ ||
                   maxT != lastSpotMax_ || step != lastFadeStep_;

    spots_ = spots;
    haveSnapshot_ = true;
    lastSpotCount_ = spots.size();
    lastSpotMax_ = maxT;
    lastFadeStep_ = step;
    if (changed) dirty_ = true;
}

void WsprArcs::update() {
    if (dirty_ && vao_ != 0) rebuild();
}

void WsprArcs::rebuild() {
    if (vao_ == 0) return;

    // Layout: pos(3) + rgba(4) = 7 floats por vertice.
    std::vector<float> verts;
    verts.reserve(kMaxArcs * kSegments * 2 * 7);

    auto push = [&](const glm::vec3& p, const glm::vec4& c) {
        verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
        verts.push_back(c.r); verts.push_back(c.g); verts.push_back(c.b); verts.push_back(c.a);
    };

    const size_t n = std::min(spots_.size(), kMaxArcs);
    for (size_t k = 0; k < n; k++) {
        const WsprSpot& s = spots_[k];
        glm::vec3 col;
        bandColor(s.band, col);

        // Alpha por edad: decae a lo largo de los 10 min declarados.
        float age = (float)std::max(0.0, nowUtc_ - s.timeUtc);
        float fade = 1.0f - age / kTtlSec;
        float alpha = kAlphaBase * std::max(0.12f, std::min(1.0f, fade));

        const glm::vec3 a = latLonToUnit(s.txLat, s.txLon);
        const glm::vec3 b = latLonToUnit(s.rxLat, s.rxLon);
        const float ang = acosf(std::max(-1.0f, std::min(1.0f, glm::dot(a, b))));
        const float sinAng = sinf(ang);
        const float bulge = std::min(1.0f, ang / 2.0f);

        auto pointAt = [&](float t) {
            glm::vec3 dir;
            if (fabsf(sinAng) < 1e-5f) {
                dir = glm::normalize(glm::mix(a, b, t));   // tramo nulo
            } else {
                dir = (sinf((1.0f - t) * ang) / sinAng) * a + (sinf(t * ang) / sinAng) * b;
            }
            // Altura dibujada: base ionosferica + bulge (eleccion de pantalla).
            float r = kBaseRadius + kBulge * sinf(kPi * t) * bulge;
            return dir * r;
        };

        const glm::vec4 c(col, alpha);
        for (int i = 0; i < kSegments; i++) {
            float t0 = (float)i / (float)kSegments;
            float t1 = (float)(i + 1) / (float)kSegments;
            push(pointAt(t0), c);
            push(pointAt(t1), c);
        }
    }

    vertexCount_ = (int)(verts.size() / 7);
    arcCount_ = (int)n;

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)),
                 verts.empty() ? nullptr : verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    dirty_ = false;
}

void WsprArcs::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_) return;
    if (dirty_) rebuild();
    if (vertexCount_ == 0) return;   // sin spots no se dibuja nada

    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));

    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
}

void WsprArcs::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (shader_) glDeleteProgram(shader_);
    vbo_ = vao_ = shader_ = 0;
    vertexCount_ = 0;
    arcCount_ = 0;
    dirty_ = true;
    haveSnapshot_ = false;
}
