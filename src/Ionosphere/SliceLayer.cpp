#include "SliceLayer.h"
#include "Utils/Colormap.h"
#include <cmath>
#include <cstdio>

static const char* sliceVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoord;
uniform mat4 u_model, u_view, u_projection;
out vec2 vTexCoord;
void main() {
    vTexCoord = aTexCoord;
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
}
)";

static const char* sliceFragSrc = R"(
#version 400 core
in vec2 vTexCoord;
uniform sampler2D u_tex;
uniform float u_opacity;
out vec4 fragColor;
void main() {
    vec4 c = texture(u_tex, vTexCoord);
    fragColor = vec4(c.rgb, c.a * u_opacity);
}
)";

SliceLayer::SliceLayer() {}
SliceLayer::~SliceLayer() { destroy(); }

void SliceLayer::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &sliceVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &sliceFragSrc, nullptr);
    glCompileShader(fs);
    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    printf("[OK] SliceLayer initialized\n");
}

void SliceLayer::updateSlice(const glm::vec3& a, const glm::vec3& b,
                             const std::vector<float>& values,
                             const std::string& cmap,
                             float altMinKm, float altMaxKm) {
    if (vao_ == 0) return;
    if ((int)values.size() != SEG * LVL) return;
    glm::vec3 na = glm::normalize(a), nb = glm::normalize(b);
    float dot = glm::clamp(glm::dot(na, nb), -1.0f, 1.0f);
    float omega = acosf(dot);
    bool useSlerp = omega > 1e-4f && omega < 3.14159265f - 1e-4f;

    std::vector<float> verts;
    verts.reserve(SEG * LVL * 5);
    for (int j = 0; j < LVL; j++) {
        float f = (LVL <= 1) ? 0.0f : (float)j / (float)(LVL - 1);
        float altKm = altMinKm + (altMaxKm - altMinKm) * powf(f, 1.5f);
        float r = 1.0f + altKm / 6371.0f;
        for (int i = 0; i < SEG; i++) {
            float s = (SEG <= 1) ? 0.0f : (float)i / (float)(SEG - 1);
            glm::vec3 dir;
            if (useSlerp) {
                float so = sinf(omega);
                dir = (na * sinf((1.0f - s) * omega) + nb * sinf(s * omega)) / so;
            } else {
                dir = glm::normalize(na * (1.0f - s) + nb * s);
            }
            verts.push_back(dir.x * r);
            verts.push_back(dir.y * r);
            verts.push_back(dir.z * r);
            verts.push_back(s);
            verts.push_back(f);
        }
    }
    std::vector<unsigned int> idx;
    for (int j = 0; j < LVL - 1; j++) {
        for (int i = 0; i < SEG - 1; i++) {
            unsigned a0 = j * SEG + i, b0 = a0 + SEG;
            idx.push_back(a0); idx.push_back(b0); idx.push_back(a0 + 1);
            idx.push_back(b0); idx.push_back(b0 + 1); idx.push_back(a0 + 1);
        }
    }
    indexCount_ = (int)idx.size();

    float pal[256 * 3];
    colormap::buildPalette(cmap, 256, pal);
    std::vector<unsigned char> tex(SEG * LVL * 4);
    for (int k = 0; k < SEG * LVL; k++) {
        float d = glm::clamp(values[k], 0.0f, 1.0f);
        int ci = (int)(d * 255.0f);
        tex[k * 4 + 0] = (unsigned char)(pal[ci * 3 + 0] * 255.0f);
        tex[k * 4 + 1] = (unsigned char)(pal[ci * 3 + 1] * 255.0f);
        tex[k * 4 + 2] = (unsigned char)(pal[ci * 3 + 2] * 255.0f);
        tex[k * 4 + 3] = (unsigned char)(glm::clamp(d * 1.2f, 0.0f, 1.0f) * 255.0f);
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SEG, LVL, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data());
    hasData_ = true;
}

void SliceLayer::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_ || !hasData_) return;
    glUseProgram(shader_);
    glm::mat4 model = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_model"), 1, GL_FALSE, &model[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, &projection[0][0]);
    glUniform1i(glGetUniformLocation(shader_, "u_tex"), 0);
    glUniform1f(glGetUniformLocation(shader_, "u_opacity"), opacity_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void SliceLayer::destroy() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (texture_) glDeleteTextures(1, &texture_);
    if (shader_) glDeleteProgram(shader_);
    vao_ = vbo_ = ebo_ = texture_ = shader_ = 0;
    indexCount_ = 0;
    hasData_ = false;
}
