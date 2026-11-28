#include "HeliosphereView.h"

#include "Data/ParkerMath.h"   // fuente real de omega sideral y AU_KM

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>
#include <cstdio>

static constexpr float kR0 = 8.0f;          // R_E, declarado (boca visible)
static constexpr float kVisualCompression = 0.129f;  // declarada, solo visual
static constexpr float kSpeedKms = 400.0f;  // v_sw declarado (no vivo en 3D)

// k = (v/omega) en R_E/rad, con omega sideral de parker:: y AU_KM real.
static float parkerPitchRe() {
    const double omega = 2.0 * 3.14159265358979323846 /
                         (parker::SIDEREAL_ROTATION_DAYS * 86400.0);
    const double kmPerRad = kSpeedKms / omega;
    return (float)(kmPerRad / parker::AU_KM) * kVisualCompression;
}

static const char* helioVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aR;
uniform mat4 u_view, u_projection;
uniform mat3 u_rot;
out float vR;
void main() {
    vR = aR;
    gl_Position = u_projection * u_view * vec4(u_rot * aPos, 1.0);
}
)";

static const char* helioFragSrc = R"(
#version 400 core
in float vR;
out vec4 fragColor;
void main() {
    vec3 c = mix(vec3(0.20, 0.70, 1.00), vec3(0.75, 0.35, 1.00), clamp(vR, 0.0, 1.0));
    fragColor = vec4(c, 0.22 * (1.0 - 0.6 * clamp(vR, 0.0, 1.0)));
}
)";

void HeliosphereView::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &helioVertSrc, nullptr);
    glShaderSource(fs, 1, &helioFragSrc, nullptr);
    glCompileShader(vs); glCompileShader(fs);
    shader_ = glCreateProgram();
    glAttachShader(shader_, vs); glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    GLint ok = GL_FALSE;
    glGetProgramiv(shader_, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) { glDeleteProgram(shader_); shader_ = 0; }
    glDeleteShader(vs); glDeleteShader(fs);

    const float kPitch = parkerPitchRe();
    std::vector<float> v;
    v.reserve(kArms * kSegments * 2 * 4);
    for (int a = 0; a < kArms; ++a) {
        const float base = 2.0f * glm::pi<float>() * (float)a / kArms;
        auto point = [base, kPitch](float phi) {
            const float r = kR0 + kPitch * phi;
            const float th = base + phi;
            return glm::vec3(r * std::cos(th), 0.0f, r * std::sin(th));
        };
        for (int s = 0; s < kSegments; ++s) {
            const float p0 = 2.0f * glm::pi<float>() * (float)s / kSegments;
            const float p1 = 2.0f * glm::pi<float>() * (float)(s + 1) / kSegments;
            const glm::vec3 q0 = point(p0), q1 = point(p1);
            const float r0 = (kR0 + kPitch * p0) / (kR0 + kPitch * 2.0f * glm::pi<float>());
            const float r1 = (kR0 + kPitch * p1) / (kR0 + kPitch * 2.0f * glm::pi<float>());
            v.push_back(q0.x); v.push_back(q0.y); v.push_back(q0.z); v.push_back(r0);
            v.push_back(q1.x); v.push_back(q1.y); v.push_back(q1.z); v.push_back(r1);
        }
    }
    vertexCount_ = (int)v.size() / 4;
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    fprintf(stderr, "[HeliosphereView] arms=%d segments=%d verts=%d draws=%d\n",
            kArms, kSegments, vertexCount_, kNewDrawsPerFrame);
}

void HeliosphereView::setSunDirection(const glm::vec3& sunWorld) {
    const float len = glm::length(sunWorld);
    if (!(len > 1e-5f)) return;
    const glm::vec3 s = sunWorld / len;
    const glm::vec3 from(1.0f, 0.0f, 0.0f);
    const float c = glm::clamp(glm::dot(from, s), -1.0f, 1.0f);
    if (c > 0.999999f) { rot_ = glm::mat3(1.0f); return; }
    if (c < -0.999999f) {
        rot_ = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::pi<float>(),
                                    glm::vec3(0.0f, 0.0f, 1.0f)));
        return;
    }
    const glm::vec3 axis = glm::normalize(glm::cross(from, s));
    rot_ = glm::mat3(glm::rotate(glm::mat4(1.0f), std::acos(c), axis));
}

void HeliosphereView::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!shader_ || vao_ == 0 || vertexCount_ == 0) return;
    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix3fv(glGetUniformLocation(shader_, "u_rot"), 1, GL_FALSE, glm::value_ptr(rot_));
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
}

void HeliosphereView::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (shader_) glDeleteProgram(shader_);
    vbo_ = vao_ = shader_ = 0;
    vertexCount_ = 0;
}
