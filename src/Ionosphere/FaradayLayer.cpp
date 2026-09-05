#include "FaradayLayer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

static const char* farVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
uniform mat4 u_view, u_projection;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = u_projection * u_view * vec4(aPos, 1.0);
}
)";

static const char* farFragSrc = R"(
#version 400 core
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

FaradayLayer::FaradayLayer() {}
FaradayLayer::~FaradayLayer() { destroy(); }

glm::vec3 FaradayLayer::latLonToPos(float latDeg, float lonDeg, float r) const {
    float phi = 3.14159265f * 0.5f - glm::radians(latDeg);
    float theta = 3.14159265f - glm::radians(lonDeg);
    float sn = sinf(phi), cs = cosf(phi);
    return glm::vec3(r * sn * cosf(theta), r * cs, r * sn * sinf(theta));
}

void FaradayLayer::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &farVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &farFragSrc, nullptr);
    glCompileShader(fs);
    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    rebuild_ = true;
    printf("[OK] FaradayLayer initialized\n");
}

void FaradayLayer::updateData(const GridData& tecGrid, float fallbackTecu, const glm::vec3& sunWorld, int mode) {
    tecGrid_ = tecGrid;
    fallbackTecu_ = fallbackTecu;
    sunWorld_ = glm::normalize(sunWorld);
    mode_ = mode & 1;
    rebuild_ = true;
    if (vao_ != 0) rebuild();
}

static float sampleTec(const GridData& g, float latDeg, float lonDeg, float fallback) {
    if (!g.valid || g.width < 2 || g.height < 2 || g.values.empty()) return fallback;
    const int W = g.width, H = g.height;
    float gx = (lonDeg + 180.0f) / 360.0f * W - 0.5f;
    float gy = (latDeg + 90.0f) / 180.0f * H - 0.5f;
    int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
    float fx = gx - x0, fy = gy - y0;
    auto at = [&](int x, int y) -> float {
        x %= W; if (x < 0) x += W;
        y = y < 0 ? 0 : (y >= H ? H - 1 : y);
        return g.values[y * W + x];
    };
    float tec = at(x0, y0) * (1 - fx) * (1 - fy) + at(x0 + 1, y0) * fx * (1 - fy) +
                at(x0, y0 + 1) * (1 - fx) * fy + at(x0 + 1, y0 + 1) * fx * fy;
    return tec > 0.0f ? tec : fallback;
}

void FaradayLayer::rebuild() {
    if (vao_ == 0) return;
    std::vector<float> verts;
    float r = 1.0f + heightOffset_;
    // Rejilla 7 lat x 12 lon = 84 vectores (ligera para GL_LINES).
    const int NLAT = 7, NLON = 12;
    for (int i = 0; i < NLAT; i++) {
        float lat = -75.0f + (150.0f * i / (NLAT - 1));
        for (int j = 0; j < NLON; j++) {
            float lon = -180.0f + (360.0f * j / NLON);
            glm::vec3 P = latLonToPos(lat, lon, r);
            glm::vec3 radial = glm::normalize(P);
            // TEC local bajo el vector (del grid GloTEC) -> rotacion local.
            float tecu = sampleTec(tecGrid_, lat, lon, fallbackTecu_);
            float local = 2.2f * tecu * (0.4f + 0.6f * fabsf(cosf(lat * 3.14159265f / 180.0f)));
            // Rotacion local: mas de dia (subsolar) y en ecuador.
            float dayF = glm::clamp(glm::dot(radial, sunWorld_), -1.0f, 1.0f) * 0.5f + 0.5f;
            float eqF = expf(-powf(lat / 35.0f, 2.0f));
            local *= (0.35f + 0.9f * dayF * (0.5f + 0.5f * eqF));
            // Direccion: radial inclinado en azimut por el angulo (polarizacion).
            // Base tangencial este-oeste:
            glm::vec3 east = glm::normalize(glm::cross(glm::vec3(0, 1, 0), radial) +
                                            glm::vec3(1e-5f));
            float tilt = glm::radians(fmodf(local, 90.0f));
            glm::vec3 dir = glm::normalize(radial * cosf(tilt * 0.5f) + east * sinf(tilt * 0.5f));
            float len = (mode_ == 1)
                ? 0.06f * scale_
                : (0.02f + 0.0035f * fminf(local, 90.0f)) * scale_;
            glm::vec3 tail = P;
            glm::vec3 head = P + dir * len;
            // Color divergente por rotacion (azul=baja, rojo=alta).
            float u = glm::clamp(local / 60.0f, 0.0f, 1.0f);
            glm::vec3 col;
            if (u < 0.5f) col = glm::mix(glm::vec3(0.2f, 0.4f, 0.9f), glm::vec3(0.3f, 0.85f, 0.4f), u * 2.0f);
            else          col = glm::mix(glm::vec3(0.3f, 0.85f, 0.4f), glm::vec3(0.95f, 0.25f, 0.15f), (u - 0.5f) * 2.0f);
            verts.push_back(tail.x); verts.push_back(tail.y); verts.push_back(tail.z);
            verts.push_back(col.x);  verts.push_back(col.y);  verts.push_back(col.z);
            verts.push_back(head.x); verts.push_back(head.y); verts.push_back(head.z);
            verts.push_back(col.x);  verts.push_back(col.y);  verts.push_back(col.z);
        }
    }
    vectorCount_ = (int)verts.size() / 12;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    rebuild_ = false;
}

void FaradayLayer::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_ || vectorCount_ == 0) return;
    if (rebuild_) rebuild();
    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, &projection[0][0]);
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vectorCount_ * 2);
    glBindVertexArray(0);
}

void FaradayLayer::destroy() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (shader_) glDeleteProgram(shader_);
    vao_ = vbo_ = shader_ = 0;
    vectorCount_ = 0;
}
