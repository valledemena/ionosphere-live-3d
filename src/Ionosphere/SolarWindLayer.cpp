#include "SolarWindLayer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdio>

// Vertices con color por vertice; dibujo tipo GL_LINES (pares tail/head).
static const char* lineVertSrc = R"(
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

static const char* lineFragSrc = R"(
#version 400 core
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

SolarWindLayer::SolarWindLayer() {}
SolarWindLayer::~SolarWindLayer() { destroy(); }

glm::vec3 SolarWindLayer::latLonToPos(float latDeg, float lonDeg, float r) const {
    float phi = 3.14159265f * 0.5f - glm::radians(latDeg);
    float theta = 3.14159265f - glm::radians(lonDeg);
    float sn = sinf(phi), cs = cosf(phi);
    return glm::vec3(r * sn * cosf(theta), r * cs, r * sn * sinf(theta));
}

void SolarWindLayer::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &lineVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &lineFragSrc, nullptr);
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
    printf("[OK] SolarWindLayer initialized\n");
}

void SolarWindLayer::updateArrowData(float speed, float bz, const glm::vec3& sunWorld, int mode) {
    speed_ = speed;
    bz_ = bz;
    sunWorld_ = glm::normalize(sunWorld);
    mode_ = mode & 1;
    rebuild_ = true;
    if (vao_ != 0) rebuild();
}

void SolarWindLayer::rebuild() {
    if (vao_ == 0) return;

    std::vector<float> verts;
    float r = 1.0f + heightOffset_;

    glm::vec3 antiSun = -sunWorld_;

    // Rejilla de puntos de anclaje: 9 latitudes x 18 longitudes.
    const int NLAT = 9;
    const int NLON = 18;
    for (int i = 0; i < NLAT; i++) {
        float lat = -80.0f + (160.0f * i / (NLAT - 1));
        for (int j = 0; j < NLON; j++) {
            float lon = -180.0f + (360.0f * j / NLON);
            glm::vec3 P = latLonToPos(lat, lon, r);

            // Direccion del flujo: proyeccion tangencial de -sunDir en P.
            glm::vec3 t = antiSun - glm::dot(antiSun, P) * P;
            float tlen = glm::length(t);
            if (tlen < 1e-5f) {
                // En el subsolar/antisol el tangente es ~0; darle una orientacion
                // perpendicular al sol (este-oeste) para no tener punta degenerada.
                t = glm::normalize(glm::cross(sunWorld_, P));
                tlen = 1.0f;
            } else {
                t = t / tlen;
            }

            // Longitud de la flecha: proporcional a la velocidad, modulada para
            // que cerca del subsolar (flujo tangente pequeno) no desaparezcan.
            float flowFactor = glm::clamp(1.0f - glm::dot(P, sunWorld_), 0.15f, 1.0f);
            float len = (0.02f + 0.00006f * speed_) * arrowScale_;

            glm::vec3 tail = P;
            glm::vec3 head = P + t * (len * (0.4f + 0.6f * flowFactor));

            // Color.
            glm::vec3 col;
            if (mode_ == 1) {
                // Bz divergente: rojo = norte (Bz>0), azul = sur (Bz<0).
                float s = glm::clamp(bz_ / 20.0f, -1.0f, 1.0f);
                if (s >= 0.0f) col = glm::vec3(0.2f, 0.2f, 0.2f) + glm::vec3(0.8f, 0.0f, 0.0f) * s;
                else           col = glm::vec3(0.2f, 0.2f, 0.2f) + glm::vec3(0.0f, 0.0f, 0.9f) * (-s);
            } else {
                // Velocidad en colormap Turbo simplificado (frio->caliente).
                float u = glm::clamp((speed_ - 250.0f) / 500.0f, 0.0f, 1.0f);
                if (u < 0.5f) col = glm::mix(glm::vec3(0.13f, 0.31f, 0.85f), glm::vec3(0.38f, 0.84f, 0.30f), u * 2.0f);
                else          col = glm::mix(glm::vec3(0.38f, 0.84f, 0.30f), glm::vec3(0.93f, 0.23f, 0.10f), (u - 0.5f) * 2.0f);
            }

            verts.push_back(tail.x); verts.push_back(tail.y); verts.push_back(tail.z);
            verts.push_back(col.x);  verts.push_back(col.y);  verts.push_back(col.z);
            verts.push_back(head.x); verts.push_back(head.y); verts.push_back(head.z);
            verts.push_back(col.x);  verts.push_back(col.y);  verts.push_back(col.z);
        }
    }
    arrowCount_ = (int)verts.size() / 12;   // pares de vertices

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

void SolarWindLayer::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_ || arrowCount_ == 0) return;
    if (rebuild_) rebuild();

    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, &projection[0][0]);

    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, arrowCount_ * 2);
    glBindVertexArray(0);
}

void SolarWindLayer::destroy() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (shader_) glDeleteProgram(shader_);
    vao_ = vbo_ = shader_ = 0;
    arrowCount_ = 0;
}
