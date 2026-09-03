#include "TecLayer.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <algorithm>

static const char* tecVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoord;

uniform mat4 u_model, u_view, u_projection;

out vec2 vTexCoord;
out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vTexCoord = aTexCoord;
    vNormal = normalize(aPos);
    vWorldPos = vec3(u_model * vec4(aPos, 1.0));
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
}
)";

static const char* tecFragSrc = R"(
#version 400 core
in vec2 vTexCoord;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D u_tecTexture;
uniform vec3 u_sunDir;
uniform float u_opacity;

out vec4 fragColor;

void main() {
    vec3 n = normalize(vNormal);
    float sunAngle = max(dot(n, normalize(u_sunDir)), 0.0);
    float lighting = 0.2 + 0.8 * sunAngle;

    vec4 tecColor = texture(u_tecTexture, vTexCoord);
    vec3 col = tecColor.rgb * lighting;

    // Alpha based on TEC value (transparent where low)
    float alpha = tecColor.a * u_opacity * lighting;

    fragColor = vec4(col, alpha);
}
)";

TecLayer::TecLayer() {}

TecLayer::~TecLayer() { destroy(); }

void TecLayer::createViridisColormap() {
    // Viridis colormap (256 entries, RGB)
    // Pre-computed from matplotlib viridis
    colormap_.resize(256 * 3);

    // Key control points
    struct { float pos; float r; float g; float b; } stops[] = {
        {0.00f, 0.267f, 0.004f, 0.329f},
        {0.13f, 0.282f, 0.140f, 0.458f},
        {0.25f, 0.253f, 0.265f, 0.530f},
        {0.38f, 0.191f, 0.407f, 0.557f},
        {0.50f, 0.127f, 0.566f, 0.551f},
        {0.63f, 0.206f, 0.718f, 0.473f},
        {0.75f, 0.478f, 0.824f, 0.318f},
        {0.88f, 0.741f, 0.873f, 0.150f},
        {1.00f, 0.993f, 0.906f, 0.144f},
    };
    int nStops = sizeof(stops) / sizeof(stops[0]);

    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f;
        int s = 0;
        for (int j = 0; j < nStops - 1; j++) {
            if (t >= stops[j].pos && t <= stops[j + 1].pos) {
                s = j;
                break;
            }
        }
        float f = (t - stops[s].pos) / (stops[s + 1].pos - stops[s].pos + 1e-6f);
        colormap_[i * 3 + 0] = stops[s].r + f * (stops[s + 1].r - stops[s].r);
        colormap_[i * 3 + 1] = stops[s].g + f * (stops[s + 1].g - stops[s].g);
        colormap_[i * 3 + 2] = stops[s].b + f * (stops[s + 1].b - stops[s].b);
    }
}

void TecLayer::init() {
    createViridisColormap();

    // Create shader
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &tecVertSrc, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &tecFragSrc, nullptr);
    glCompileShader(fs);

    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);

    glDeleteShader(vs);
    glDeleteShader(fs);

    // Create texture
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    createMesh();

    printf("[OK] TecLayer initialized\n");
}

void TecLayer::createMesh() {
    int slices = 128;
    int stacks = 64;
    float radius = 1.0f + altitude_;

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    for (int stack = 0; stack <= stacks; stack++) {
        float phi = 3.14159f * (float)stack / (float)stacks;
        for (int slice = 0; slice <= slices; slice++) {
            float theta = 2.0f * 3.14159f * (float)slice / (float)slices;

            float x = radius * sinf(phi) * cosf(theta);
            float y = radius * cosf(phi);
            float z = radius * sinf(phi) * sinf(theta);

            float u = (float)slice / (float)slices;
            float v = (float)stack / (float)stacks;

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertices.push_back(u);
            vertices.push_back(v);
        }
    }

    for (int stack = 0; stack < stacks; stack++) {
        for (int slice = 0; slice < slices; slice++) {
            int first = stack * (slices + 1) + slice;
            int second = first + slices + 1;

            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }

    indexCount_ = (int)indices.size();

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    meshDirty_ = false;
}

void TecLayer::updateFromGrid(const TECGrid& grid) {
    grid_ = grid;
    textureDirty_ = true;
    updateTexture();
}

void TecLayer::updateTexture() {
    if (grid_.width == 0 || grid_.height == 0) return;

    // Find min/max for normalization
    float minVal = 1e9f, maxVal = -1e9f;
    for (float v : grid_.values) {
        minVal = fminf(minVal, v);
        maxVal = fmaxf(maxVal, v);
    }
    float range = maxVal - minVal + 1e-6f;

    // Create RGBA texture data
    textureData_.resize(grid_.width * grid_.height * 4);

    for (int i = 0; i < grid_.width * grid_.height; i++) {
        float normalized = (grid_.values[i] - minVal) / range;
        int ci = (int)(normalized * 255.0f);
        ci = std::max(0, std::min(255, ci));

        textureData_[i * 4 + 0] = (unsigned char)(colormap_[ci * 3 + 0] * 255);
        textureData_[i * 4 + 1] = (unsigned char)(colormap_[ci * 3 + 1] * 255);
        textureData_[i * 4 + 2] = (unsigned char)(colormap_[ci * 3 + 2] * 255);
        textureData_[i * 4 + 3] = (unsigned char)(normalized * 200 + 55);  // Alpha based on intensity
    }

    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, grid_.width, grid_.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, textureData_.data());

    textureDirty_ = false;
}

void TecLayer::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_ || !grid_.valid || vao_ == 0) return;

    glUseProgram(shader_);
    glm::mat4 model = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_model"), 1, GL_FALSE, &model[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, &projection[0][0]);
    glUniform1i(glGetUniformLocation(shader_, "u_tecTexture"), 0);
    glUniform1f(glGetUniformLocation(shader_, "u_opacity"), 0.7f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void TecLayer::destroy() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (texture_) glDeleteTextures(1, &texture_);
    if (shader_) glDeleteProgram(shader_);
    vao_ = vbo_ = ebo_ = texture_ = shader_ = 0;
}
