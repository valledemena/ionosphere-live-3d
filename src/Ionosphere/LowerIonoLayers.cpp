#include "LowerIonoLayers.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

// Alturas declaradas en el pre-registro.
static constexpr float kDAltKm = 80.0f;
static constexpr float kEsAltKm = 110.0f;
static constexpr float kKmPerEarthRadius = 6371.0f;
static constexpr float kEsThreshold = 2.0f;  // enmienda: max real 2.7 MHz
static constexpr float kPatchSize = 0.012f;   // rad ~ 0.7 grados

static const char* dVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
uniform mat4 u_view, u_projection;
uniform vec3 u_sunDir;
out float vDay;
void main() {
    vec3 r = normalize(aPos);
    vDay = max(dot(r, u_sunDir), 0.0);
    gl_Position = u_projection * u_view * vec4(aPos, 1.0);
}
)";

static const char* dFragSrc = R"(
#version 400 core
in float vDay;
uniform float u_flare;
out vec4 fragColor;
void main() {
    float a = 0.12 * vDay * (1.0 + min(u_flare, 3.0));
    fragColor = vec4(0.85, 0.45, 0.18, a);   // blender aditivo SRC_ALPHA,ONE
}
)";

static const char* esVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aVal;
uniform mat4 u_view, u_projection;
out float vVal;
void main() {
    vVal = aVal;
    gl_Position = u_projection * u_view * vec4(aPos, 1.0);
}
)";

static const char* esFragSrc = R"(
#version 400 core
in float vVal;
out vec4 fragColor;
void main() {
    vec3 c = mix(vec3(0.20, 0.55, 1.00), vec3(1.00, 0.95, 0.70), clamp(vVal, 0.0, 1.0));
    fragColor = vec4(c, clamp(vVal, 0.0, 1.0) * 0.75);
}
)";

static GLuint makeProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &vsSrc, nullptr);
    glShaderSource(fs, 1, &fsSrc, nullptr);
    glCompileShader(vs); glCompileShader(fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) { glDeleteProgram(p); p = 0; }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

void LowerIonoLayers::init() {
    dShader_ = makeProgram(dVertSrc, dFragSrc);
    esShader_ = makeProgram(esVertSrc, esFragSrc);

    // Carcasa D: esfera UV 32x16 a 80 km.
    constexpr int NU = 32, NV = 16;
    const float r = 1.0f + kDAltKm / kKmPerEarthRadius;
    std::vector<float> verts;
    verts.reserve(NU * NV * 6 * 3);
    for (int v = 0; v < NV; ++v) {
        const float p0 = glm::pi<float>() * (float)v / NV;
        const float p1 = glm::pi<float>() * (float)(v + 1) / NV;
        for (int u = 0; u < NU; ++u) {
            const float t0 = 2.0f * glm::pi<float>() * (float)u / NU;
            const float t1 = 2.0f * glm::pi<float>() * (float)(u + 1) / NU;
            auto pt = [r](float p, float t) {
                return glm::vec3(r * std::sin(p) * std::cos(t),
                                 r * std::cos(p),
                                 r * std::sin(p) * std::sin(t));
            };
            const glm::vec3 a = pt(p0, t0), b = pt(p0, t1);
            const glm::vec3 c = pt(p1, t0), d = pt(p1, t1);
            for (const glm::vec3& q : {a, b, c, b, d, c})
                for (int k = 0; k < 3; ++k) verts.push_back(q[k]);
        }
    }
    dVerts_ = (int)verts.size() / 3;
    glGenVertexArrays(1, &dVao_); glGenBuffers(1, &dVbo_);
    glBindVertexArray(dVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dVbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenVertexArrays(1, &esVao_); glGenBuffers(1, &esVbo_);
    fprintf(stderr, "[LowerIonoLayers] init D=%.0fkm Es=%.0fkm\n", kDAltKm, kEsAltKm);
}

void LowerIonoLayers::setDLayerParams(const glm::vec3& sunDir, float flare) {
    const float len = glm::length(sunDir);
    if (len > 1e-5f) sunDir_ = sunDir / len;
    flare_ = glm::clamp(flare, 0.0f, 3.0f);
}

void LowerIonoLayers::updateEs(const std::vector<StationPoint>& samples) {
    patchCount_ = 0;
    maxFoEs_ = 0.0f;
    std::vector<float> verts;
    verts.reserve(samples.size() * 6 * 4);
    for (const auto& s : samples) {
        if (!s.valid || !(s.foEs > kEsThreshold)) continue;
        maxFoEs_ = std::max(maxFoEs_, s.foEs);
        const float lat = glm::radians(s.latDeg);
        const float lon = glm::radians(s.lonDeg);
        const float rr = 1.0f + (s.hEs > 0.0f ? s.hEs : kEsAltKm) / kKmPerEarthRadius;
        const glm::vec3 rn(std::cos(lat) * std::cos(lon),
                           std::sin(lat),
                           std::cos(lat) * std::sin(lon));
        glm::vec3 east = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), rn);
        if (glm::length(east) < 1e-4f) east = glm::vec3(1.0f, 0.0f, 0.0f);
        east = glm::normalize(east);
        const glm::vec3 north = glm::normalize(glm::cross(rn, east));
        const float v = glm::clamp(s.foEs / 10.0f, 0.0f, 1.0f);
        glm::vec3 q[4];
        q[0] = glm::normalize(rn - east * kPatchSize - north * kPatchSize) * rr;
        q[1] = glm::normalize(rn + east * kPatchSize - north * kPatchSize) * rr;
        q[2] = glm::normalize(rn - east * kPatchSize + north * kPatchSize) * rr;
        q[3] = glm::normalize(rn + east * kPatchSize + north * kPatchSize) * rr;
        const int idx[6] = {0, 1, 2, 1, 3, 2};
        for (int k = 0; k < 6; ++k) {
            for (int j = 0; j < 3; ++j) verts.push_back(q[idx[k]][j]);
            verts.push_back(v);
        }
        ++patchCount_;
    }
    glBindBuffer(GL_ARRAY_BUFFER, esVbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float),
                 verts.empty() ? nullptr : verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    int validStations = 0;
    double epoch = 0.0;
    float maxFoEsAll = 0.0f;
    for (const auto& s : samples) {
        if (!s.valid) continue;
        ++validStations;
        if (s.timeUtc > epoch) epoch = s.timeUtc;
        if (std::isfinite(s.foEs)) maxFoEsAll = std::max(maxFoEsAll, s.foEs);
    }
    static int lastPatches = -1;
    static double lastEpoch = -1.0;
    if (patchCount_ != lastPatches || (validStations > 0 && epoch != lastEpoch)) {
        lastPatches = patchCount_;
        lastEpoch = epoch;
        fprintf(stderr, "[LowerIonoLayers] epoch=%.0f validStations=%d maxFoEsAll=%.1f "
                "maxFoEsUsed=%.1f patches=%d\n",
                epoch, validStations, maxFoEsAll, maxFoEs_, patchCount_);
    }
}

void LowerIonoLayers::renderD(const glm::mat4& view, const glm::mat4& projection) {
    if (!dShader_ || dVao_ == 0 || dVerts_ == 0) return;
    glUseProgram(dShader_);
    glUniformMatrix4fv(glGetUniformLocation(dShader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(dShader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(dShader_, "u_sunDir"), 1, glm::value_ptr(sunDir_));
    glUniform1f(glGetUniformLocation(dShader_, "u_flare"), flare_);
    glBindVertexArray(dVao_);
    glDrawArrays(GL_TRIANGLES, 0, dVerts_);
    glBindVertexArray(0);
}

void LowerIonoLayers::renderEs(const glm::mat4& view, const glm::mat4& projection) {
    if (!esShader_ || esVao_ == 0 || patchCount_ == 0) return;
    glUseProgram(esShader_);
    glUniformMatrix4fv(glGetUniformLocation(esShader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(esShader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glBindVertexArray(esVao_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, patchCount_ * 6);
    glBindVertexArray(0);
}

void LowerIonoLayers::destroy() {
    if (dVbo_) glDeleteBuffers(1, &dVbo_);
    if (dVao_) glDeleteVertexArrays(1, &dVao_);
    if (dShader_) glDeleteProgram(dShader_);
    if (esVbo_) glDeleteBuffers(1, &esVbo_);
    if (esVao_) glDeleteVertexArrays(1, &esVao_);
    if (esShader_) glDeleteProgram(esShader_);
    dVbo_ = dVao_ = dShader_ = esVbo_ = esVao_ = esShader_ = 0;
    dVerts_ = patchCount_ = 0;
}
