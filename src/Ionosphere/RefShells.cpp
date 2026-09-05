#include "RefShells.h"
#include <cmath>
#include <cstdio>
#include <vector>

static const char* refVertSrc = R"(
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

static const char* refFragSrc = R"(
#version 400 core
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

RefShells::RefShells() {}
RefShells::~RefShells() { destroy(); }

glm::vec3 RefShells::latLonToPos(float latDeg, float lonDeg, float r) const {
    // OJO (historial de meridianos rotados 180°): TODO el motor usa
    // theta = pi - lonRad (ver IonosondeLayer.cpp:249, malla igual que
    // GridLayer/Earth verificada por pixeles). NO usar radians(lon) a secas.
    float phi = 3.14159265f * 0.5f - glm::radians(latDeg);
    float theta = 3.14159265f - glm::radians(lonDeg);
    float sn = sinf(phi), cs = cosf(phi);
    return glm::vec3(r * sn * cosf(theta), r * cs, r * sn * sinf(theta));
}

void RefShells::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &refVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &refFragSrc, nullptr);
    glCompileShader(fs);
    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    rebuild();
    printf("[OK] RefShells initialized (%d verts)\n", vertexCount_);
}

void RefShells::rebuild() {
    struct Shell { float altKm; glm::vec3 col; };
    glm::vec3 colE(0.10f, 0.45f, 0.20f);
    glm::vec3 colF1(0.50f, 0.42f, 0.10f);
    glm::vec3 colF2(0.50f, 0.22f, 0.07f);
    const Shell shells[] = {
        {altE_, colE},     // E  (verde tenue)
        {altF1_, colF1},   // F1 (amarillo tenue)
        {altF2_, colF2},   // F2 (naranja tenue)
    };
    static const float parallels[] = {0.0f, 30.0f, -30.0f, 60.0f, -60.0f};

    std::vector<float> verts;
    auto seg = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        verts.push_back(a.x); verts.push_back(a.y); verts.push_back(a.z);
        verts.push_back(c.x); verts.push_back(c.y); verts.push_back(c.z);
        verts.push_back(b.x); verts.push_back(b.y); verts.push_back(b.z);
        verts.push_back(c.x); verts.push_back(c.y); verts.push_back(c.z);
    };

    for (const auto& sh : shells) {
        float r = 1.0f + sh.altKm / 6371.0f;
        for (float lat : parallels) {
            const int N = 72;
            for (int k = 0; k < N; k++) {
                float l0 = -180.0f + 360.0f * k / N;
                float l1 = -180.0f + 360.0f * (k + 1) / N;
                seg(latLonToPos(lat, l0, r), latLonToPos(lat, l1, r), sh.col);
            }
        }
        for (int m = 0; m < 6; m++) {
            float lon = -180.0f + 60.0f * m;
            const int N = 48;
            for (int k = 0; k < N; k++) {
                float a0 = -90.0f + 180.0f * k / N;
                float a1 = -90.0f + 180.0f * (k + 1) / N;
                seg(latLonToPos(a0, lon, r), latLonToPos(a1, lon, r), sh.col);
            }
        }
    }
    vertexCount_ = (int)verts.size() / 6;
    if (vao_ == 0 || vbo_ == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RefShells::updateAltitudes(float eKm, float f1Km, float f2Km) {
    if (fabsf(eKm - altE_) < 0.5f && fabsf(f1Km - altF1_) < 0.5f && fabsf(f2Km - altF2_) < 0.5f) return;
    altE_ = eKm; altF1_ = f1Km; altF2_ = f2Km;
    rebuild();
}

void RefShells::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_ || vertexCount_ == 0) return;
    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, &projection[0][0]);
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
}

void RefShells::destroy() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (shader_) glDeleteProgram(shader_);
    vao_ = vbo_ = shader_ = 0;
    vertexCount_ = 0;
}
