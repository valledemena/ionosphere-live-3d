#include "MidIonoLayers.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>
#include <cstdio>

// Parametros declarados en el pre-registro v3.
static constexpr float kShellAltKm = 300.0f;
static constexpr float kKmPerEarthRadius = 6371.0f;
static constexpr float kAnomalyLatDeg = 15.0f;

static const char* sphereVert = R"(
#version 400 core
layout(location=0) in vec3 aPos;
uniform mat4 u_view, u_projection;
uniform float u_radius;
uniform vec3 u_fieldAxis;
uniform int u_mode;
out vec3 vDir;
out vec3 vWorld;
void main() {
    vec3 r = normalize(aPos);
    vDir = r;
    vec3 p = r * u_radius;
    vWorld = p;
    gl_Position = u_projection * u_view * vec4(p, 1.0);
}
)";

static const char* plasmaFrag = R"(
#version 400 core
in vec3 vDir;
uniform vec3 u_sunDir;
uniform float u_kp;
out vec4 fragColor;
void main() {
    float day = max(dot(vDir, u_sunDir), 0.0);
    float a = 0.035 * (0.35 + day) * (1.0 + 0.08 * u_kp);
    fragColor = vec4(0.30, 0.75, 0.95, a);
}
)";

static const char* anomalyFrag = R"(
#version 400 core
in vec3 vDir;
uniform vec3 u_fieldAxis;
uniform float u_tec;
out vec4 fragColor;
void main() {
    // Latitud MAGNETICA respecto al eje IGRF-14 (marco Tierra).
    float lat = degrees(asin(clamp(dot(vDir, u_fieldAxis), -1.0, 1.0)));
    float g1 = exp(-pow((lat - 15.0) / 4.0, 2.0));
    float g2 = exp(-pow((lat + 15.0) / 4.0, 2.0));
    float a = clamp(u_tec, 0.0, 1.0) * (g1 + g2) * 0.30;
    fragColor = vec4(0.95, 0.55, 0.85, a);
}
)";

static const char* toiFrag = R"(
#version 400 core
in vec3 vWorld;
uniform float u_kp, u_time;
out vec4 fragColor;
void main() {
    float lon = atan(vWorld.z, vWorld.x);
    float wave = sin(6.0 * lon - 0.5 * u_time);
    float a = 0.06 * (0.3 + 0.1 * u_kp) * (0.5 + 0.5 * wave);
    fragColor = vec4(0.55, 0.40, 1.00, a);
}
)";

static GLuint makeProgram(const char* fragSrc) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &sphereVert, nullptr);
    glShaderSource(fs, 1, &fragSrc, nullptr);
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

void MidIonoLayers::init() {
    plasma_ = makeProgram(plasmaFrag);
    anomaly_ = makeProgram(anomalyFrag);
    toi_ = makeProgram(toiFrag);

    constexpr int NU = 32, NV = 16;
    std::vector<float> v;
    std::vector<unsigned int> idx;
    for (int j = 0; j <= NV; ++j) {
        const float p = glm::pi<float>() * (float)j / NV;
        for (int i = 0; i <= NU; ++i) {
            const float t = 2.0f * glm::pi<float>() * (float)i / NU;
            v.push_back(std::sin(p) * std::cos(t));
            v.push_back(std::cos(p));
            v.push_back(std::sin(p) * std::sin(t));
        }
    }
    for (int j = 0; j < NV; ++j)
        for (int i = 0; i < NU; ++i) {
            const unsigned int a = j * (NU + 1) + i;
            idx.push_back(a); idx.push_back(a + NU + 1); idx.push_back(a + 1);
            idx.push_back(a + 1); idx.push_back(a + NU + 1); idx.push_back(a + NU + 2);
        }
    indexCount_ = (int)idx.size();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    fprintf(stderr, "[MidIonoLayers] init shell=%.0fkm anomaly=+-%.0fmag draws=%d\n",
            kShellAltKm, kAnomalyLatDeg, kNewDrawsPerFrame);
}

void MidIonoLayers::setParams(float kp, float tecNorm, const glm::vec3& sunDir,
                              const glm::vec3& fieldAxis, float timeSec) {
    kp_ = glm::clamp(kp, 0.0f, 9.0f);
    tec_ = glm::clamp(tecNorm, 0.0f, 1.0f);
    time_ = timeSec;
    const float ls = glm::length(sunDir), lf = glm::length(fieldAxis);
    if (ls > 1e-5f) sunDir_ = sunDir / ls;
    if (lf > 1e-5f) fieldAxis_ = fieldAxis / lf;
}

void MidIonoLayers::renderPlasmasphere(const glm::mat4& view, const glm::mat4& projection) {
    if (!plasma_ || vao_ == 0) return;
    glUseProgram(plasma_);
    glUniformMatrix4fv(glGetUniformLocation(plasma_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(plasma_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    // Proxy visual: escala L declarada (L_pp dependiente de Kp).
    const float lpp = glm::clamp(5.6f - 0.46f * kp_, 2.0f, 6.0f);
    glUniform1f(glGetUniformLocation(plasma_, "u_radius"), 1.0f + lpp * 0.35f);
    glUniform3fv(glGetUniformLocation(plasma_, "u_sunDir"), 1, glm::value_ptr(sunDir_));
    glUniform1f(glGetUniformLocation(plasma_, "u_kp"), kp_);
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, (void*)0);
    glBindVertexArray(0);
}

void MidIonoLayers::renderAnomaly(const glm::mat4& view, const glm::mat4& projection) {
    if (!anomaly_ || vao_ == 0) return;
    glUseProgram(anomaly_);
    glUniformMatrix4fv(glGetUniformLocation(anomaly_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(anomaly_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(anomaly_, "u_radius"), 1.0f + kShellAltKm / kKmPerEarthRadius);
    glUniform3fv(glGetUniformLocation(anomaly_, "u_fieldAxis"), 1, glm::value_ptr(fieldAxis_));
    glUniform1f(glGetUniformLocation(anomaly_, "u_tec"), tec_);
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, (void*)0);
    glBindVertexArray(0);
}

void MidIonoLayers::renderToi(const glm::mat4& view, const glm::mat4& projection) {
    if (!toi_ || vao_ == 0) return;
    glUseProgram(toi_);
    glUniformMatrix4fv(glGetUniformLocation(toi_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(toi_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(toi_, "u_radius"), 1.0f + (kShellAltKm + 20.0f) / kKmPerEarthRadius);
    glUniform1f(glGetUniformLocation(toi_, "u_kp"), kp_);
    glUniform1f(glGetUniformLocation(toi_, "u_time"), time_);
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, (void*)0);
    glBindVertexArray(0);
}

void MidIonoLayers::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (plasma_) glDeleteProgram(plasma_);
    if (anomaly_) glDeleteProgram(anomaly_);
    if (toi_) glDeleteProgram(toi_);
    vbo_ = ebo_ = vao_ = plasma_ = anomaly_ = toi_ = 0;
}
