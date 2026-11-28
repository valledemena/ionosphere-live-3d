#include "HeliosphereProbes.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>
#include <cstdio>

// Escala comprimida DECLARADA (no es la distencia real).
static constexpr float kProbe1Re = 60.0f;
static constexpr float kProbe2Re = 75.0f;
static constexpr float kMoonRe = 60.0f;
static constexpr float kMoonOmega = 0.02f;  // rad/s, MODELO, no efemérides

static const char* probeVert = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aCol;
uniform mat4 u_view, u_projection;
uniform float u_pixelScale;
out vec3 vCol;
void main() {
    vec4 vp = u_view * vec4(aPos, 1.0);
    gl_Position = u_projection * vp;
    gl_PointSize = clamp(u_pixelScale * 0.6 / max(0.1, -vp.z), 4.0, 18.0);
    vCol = aCol;
}
)";

static const char* probeFrag = R"(
#version 400 core
in vec3 vCol;
out vec4 fragColor;
void main() {
    vec2 d = gl_PointCoord - 0.5;
    float a = 1.0 - smoothstep(0.25, 0.5, length(d));
    fragColor = vec4(vCol, a);
}
)";

static const char* moonVert = R"(
#version 400 core
layout(location=0) in vec3 aPos;
uniform mat4 u_view, u_projection;
uniform float u_angle;
void main() {
    float c = cos(u_angle), s = sin(u_angle);
    vec3 p = vec3(aPos.x * c - aPos.z * s, aPos.y, aPos.x * s + aPos.z * c);
    gl_Position = u_projection * u_view * vec4(p, 1.0);
}
)";

static const char* moonFrag = R"(
#version 400 core
out vec4 fragColor;
void main() { fragColor = vec4(0.65, 0.67, 0.70, 1.0); }
)";

static GLuint makeProgram(const char* vs, const char* fs) {
    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v, 1, &vs, nullptr);
    glShaderSource(f, 1, &fs, nullptr);
    glCompileShader(v); glCompileShader(f);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) { glDeleteProgram(p); p = 0; }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

void HeliosphereProbes::init() {
    probeShader_ = makeProgram(probeVert, probeFrag);
    moonShader_ = makeProgram(moonVert, moonFrag);
    glGenVertexArrays(1, &probeVao_); glGenBuffers(1, &probeVbo_);
    glGenVertexArrays(1, &moonVao_); glGenBuffers(1, &moonVbo_); glGenBuffers(1, &moonEbo_);

    // Esfera lunar low-poly 12x8 a 60 R_E, centrada en el origen del shader.
    constexpr int NU = 12, NV = 8;
    std::vector<float> v;
    std::vector<unsigned int> idx;
    for (int j = 0; j <= NV; ++j) {
        const float p = glm::pi<float>() * (float)j / NV;
        for (int i = 0; i <= NU; ++i) {
            const float t = 2.0f * glm::pi<float>() * (float)i / NU;
            v.push_back(kMoonRe * std::sin(p) * std::cos(t));
            v.push_back(kMoonRe * std::cos(p));
            v.push_back(kMoonRe * std::sin(p) * std::sin(t));
        }
    }
    for (int j = 0; j < NV; ++j)
        for (int i = 0; i < NU; ++i) {
            const unsigned int a = j * (NU + 1) + i;
            idx.push_back(a); idx.push_back(a + NU + 1); idx.push_back(a + 1);
            idx.push_back(a + 1); idx.push_back(a + NU + 1); idx.push_back(a + NU + 2);
        }
    moonVerts_ = (int)idx.size();
    glBindVertexArray(moonVao_);
    glBindBuffer(GL_ARRAY_BUFFER, moonVbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, moonEbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    fprintf(stderr, "[HeliosphereProbes] probes=60/75 R_E (illustrative) moon=60 R_E "
            "(model-only) draws=%d\n", kNewDrawsPerFrame);
}

void HeliosphereProbes::setProbeStates(bool v1, bool v2) {
    probeCount_ = (v1 ? 1 : 0) + (v2 ? 1 : 0);
    float data[12] = {
        kProbe1Re, 0.0f, 0.0f, 0.35f, 0.85f, 1.00f,
        0.0f, 0.0f, kProbe2Re, 1.00f, 0.72f, 0.30f
    };
    glBindBuffer(GL_ARRAY_BUFFER, probeVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(probeVao_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void HeliosphereProbes::setMoonTime(float seconds) { moonAngle_ = seconds * kMoonOmega; }

void HeliosphereProbes::renderProbes(const glm::mat4& view, const glm::mat4& projection) {
    if (!probeShader_ || probeVao_ == 0 || probeCount_ <= 0) return;
    glUseProgram(probeShader_);
    glUniformMatrix4fv(glGetUniformLocation(probeShader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(probeShader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(probeShader_, "u_pixelScale"), 1000.0f);
    glBindVertexArray(probeVao_);
    glDrawArrays(GL_POINTS, 0, probeVertexCount());
    glBindVertexArray(0);
}

void HeliosphereProbes::renderMoon(const glm::mat4& view, const glm::mat4& projection) {
    if (!moonShader_ || moonVao_ == 0) return;
    glUseProgram(moonShader_);
    glUniformMatrix4fv(glGetUniformLocation(moonShader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(moonShader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(moonShader_, "u_angle"), moonAngle_);
    glBindVertexArray(moonVao_);
    glDrawElements(GL_TRIANGLES, moonVerts_, GL_UNSIGNED_INT, (void*)0);
    glBindVertexArray(0);
}

void HeliosphereProbes::destroy() {
    if (probeVbo_) glDeleteBuffers(1, &probeVbo_);
    if (probeVao_) glDeleteVertexArrays(1, &probeVao_);
    if (probeShader_) glDeleteProgram(probeShader_);
    if (moonVbo_) glDeleteBuffers(1, &moonVbo_);
    if (moonEbo_) glDeleteBuffers(1, &moonEbo_);
    if (moonVao_) glDeleteVertexArrays(1, &moonVao_);
    if (moonShader_) glDeleteProgram(moonShader_);
    probeVbo_ = probeVao_ = probeShader_ = moonVbo_ = moonEbo_ = moonVao_ = moonShader_ = 0;
    probeCount_ = moonVerts_ = 0;
}
