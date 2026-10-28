#include "GridLayer.h"
#include "Utils/Colormap.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <algorithm>

static float sampleGridBilinear(const GridData& g, float u, float v);

// --- Vertex shader (compartido) ---
static const char* gridVertSrc = R"(
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

static const char* gridFragSrc = R"(
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
    vec4 gridColor = texture(u_tecTexture, vTexCoord);
    float lighting = 0.85 + 0.15 * sunAngle;
    vec3 col = gridColor.rgb * lighting;
    float alpha = gridColor.a * u_opacity;
    fragColor = vec4(col, alpha);
}
)";

GridLayer::GridLayer() {}
GridLayer::~GridLayer() { destroy(); }


void GridLayer::setDisplace(float a) {
    a = glm::clamp(a, 0.0f, 0.2f);
    if (a != displaceAmp_) { displaceAmp_ = a; meshDirty_ = true; }
}

void GridLayer::setDisplaceCentered(bool c) {
    if (c != displaceCentered_) { displaceCentered_ = c; meshDirty_ = true; }
}

float GridLayer::sampleRaw(float u, float v) const {
    if (grid_.values.empty() || grid_.width < 2 || grid_.height < 2) return 0.0f;
    const int W = grid_.width, H = grid_.height;
    // Misma correspondencia que updateTexture: texel(lonIdx, H-1-latIdx).
    float gx = u * W - 0.5f;
    float gy = (H - 0.5f) - v * H;
    int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
    float fx = gx - x0, fy = gy - y0;
    auto at = [&](int x, int y) -> float {
        x %= W; if (x < 0) x += W;          // wrap en longitud (continuidad)
        y = std::max(0, std::min(H - 1, y));
        return grid_.values[y * W + x];
    };
    float val = at(x0, y0) * (1 - fx) * (1 - fy) + at(x0 + 1, y0) * fx * (1 - fy) +
                at(x0, y0 + 1) * (1 - fx) * fy + at(x0 + 1, y0 + 1) * fx * fy;
    return val;
}

float GridLayer::sampleNorm(float u, float v) const {
    if (grid_.values.empty()) return 0.0f;
    float range = lastMax_ - lastMin_ + 1e-6f;
    return glm::clamp((sampleRaw(u, v) - lastMin_) / range, 0.0f, 1.0f);
}

void GridLayer::buildPositions(std::vector<float>& vertices) const {
    const int slices = 128;
    const int stacks = 64;
    float base = 1.0f + heightOffset_;
    vertices.clear();
    vertices.reserve((stacks + 1) * (slices + 1) * 5);
    for (int stack = 0; stack <= stacks; stack++) {
        float phi = 3.14159f * (float)stack / (float)stacks;
        for (int slice = 0; slice <= slices; slice++) {
            float theta = 2.0f * 3.14159f * (float)slice / (float)slices;
            float u = 1.0f - (float)slice / (float)slices;
            float v = (float)stack / (float)stacks;
            float r;
            if (absoluteHeights_) {
                // Alturas reales (km) -> radio, con clamp de seguridad.
                float h = sampleGridBilinear(heightGrid_, u, v);
                if (h < 0.0f) h = 0.0f;
                if (h > 1600.0f) h = 1600.0f;
                r = 1.0f + heightOffset_ + h / 6371.0f;
            } else {
                float n;
                if (displaceCentered_) {
                    // Alturas reales (hmF2): (valor - midRango)/rango -> [-0.5, 0.5].
                    float range = lastMax_ - lastMin_ + 1e-6f;
                    n = (sampleRaw(u, v) - rangeMid_) / range;
                } else {
                    n = sampleNorm(u, v);
                }
                r = base + displaceAmp_ * n;
            }
            float x = r * sinf(phi) * cosf(theta);
            float y = r * cosf(phi);
            float z = r * sinf(phi) * sinf(theta);
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertices.push_back(u);
            vertices.push_back(v);
        }
    }
}

void GridLayer::updatePositions() {
    if (vao_ == 0 || vbo_ == 0) return;
    std::vector<float> vertices;
    buildPositions(vertices);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(float), vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GridLayer::setColormap(const std::string& name) {
    colormapName_ = name;
    colormap_.resize(256 * 3);
    colormap::buildPalette(name, 256, colormap_.data());
    textureDirty_ = true;
    // Regenerar la textura de inmediato para que el cambio de colormap se vea al
    // instante (sin esperar al proximo updateFromGrid).
    if (grid_.width > 0 && grid_.height > 0 && texture_ != 0) updateTexture();
}

void GridLayer::init() {
    setColormap("viridis");

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &gridVertSrc, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &gridFragSrc, nullptr);
    glCompileShader(fs);

    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);

    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    createMesh();

    printf("[OK] GridLayer initialized\n");
}

void GridLayer::createMesh() {
    const int slices = 128;
    const int stacks = 64;

    std::vector<float> vertices;
    buildPositions(vertices);
    std::vector<unsigned int> indices;

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
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    meshDirty_ = false;
}

void GridLayer::updateFromGrid(const GridData& grid) {
    grid_ = grid;
    absoluteHeights_ = false;
    textureDirty_ = true;
    updateTexture();
    // El relieve depende de los datos: refrescar posiciones si hay malla y amp.
    if (displaceAmp_ > 0.0f && vao_ != 0) updatePositions();
}

void GridLayer::updateFromGrids(const GridData& values, const GridData& heights) {
    grid_ = values;
    heightGrid_ = heights;
    absoluteHeights_ = true;
    textureDirty_ = true;
    updateTexture();
    if (vao_ != 0) updatePositions();
}

static float sampleGridBilinear(const GridData& g, float u, float v) {
    if (g.values.empty() || g.width < 2 || g.height < 2) return 0.0f;
    const int W = g.width, H = g.height;
    float gx = u * W - 0.5f;
    float gy = (H - 0.5f) - v * H;
    int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
    float fx = gx - x0, fy = gy - y0;
    auto at = [&](int x, int y) -> float {
        x %= W; if (x < 0) x += W;
        y = std::max(0, std::min(H - 1, y));
        return g.values[y * W + x];
    };
    return at(x0, y0) * (1 - fx) * (1 - fy) + at(x0 + 1, y0) * fx * (1 - fy) +
           at(x0, y0 + 1) * (1 - fx) * fy + at(x0 + 1, y0 + 1) * fx * fy;
}

void GridLayer::updateTexture() {
    if (grid_.width == 0 || grid_.height == 0) return;

    // Normalizacion: rango fijo si se configuro, si no min/max de los datos.
    float minVal, maxVal;
    if (hasFixedRange_) {
        minVal = rangeMin_;
        maxVal = rangeMax_;
    } else {
        minVal = 1e9f, maxVal = -1e9f;
        for (float v : grid_.values) { minVal = fminf(minVal, v); maxVal = fmaxf(maxVal, v); }
    }
    float range = maxVal - minVal + 1e-6f;
    lastMin_ = minVal;
    lastMax_ = maxVal;
    rangeMid_ = 0.5f * (minVal + maxVal);

    // El adapter entrega values[lat * width + lon] con fila 0 = lat -90 (sur),
    // columna 0 = lon -180. La esfera muestrea u=longitud (x) y v=latitud (y),
    // con v=1 en el norte y u=0 en lon -180. Reordenamos a [fila = lat (norte), col = lon].
    textureData_.resize(grid_.width * grid_.height * 4);
    for (int latIdx = 0; latIdx < grid_.height; latIdx++) {
        for (int lonIdx = 0; lonIdx < grid_.width; lonIdx++) {
            int src = latIdx * grid_.width + lonIdx;      // lat sur->norte, lon -180->180
            int texRow = grid_.height - 1 - latIdx;        // invertir lat: fila 0 = norte
            float normalized = (grid_.values[src] - minVal) / range;
            normalized = glm::clamp(normalized, 0.0f, 1.0f);
            int ci = (int)(normalized * 255.0f);

            int dst = (texRow * grid_.width + lonIdx) * 4;
            textureData_[dst + 0] = (unsigned char)(colormap_[ci * 3 + 0] * 255);
            textureData_[dst + 1] = (unsigned char)(colormap_[ci * 3 + 1] * 255);
            textureData_[dst + 2] = (unsigned char)(colormap_[ci * 3 + 2] * 255);
            textureData_[dst + 3] = (unsigned char)(normalized * 140 + 115);
        }
    }

    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, grid_.width, grid_.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, textureData_.data());
    textureDirty_ = false;
}

void GridLayer::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_ || !grid_.valid) return;

    // Reconstruir la malla si cambio la altitud
    if (meshDirty_ && vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        glDeleteBuffers(1, &vbo_);
        glDeleteBuffers(1, &ebo_);
        vao_ = vbo_ = ebo_ = 0;
    }
    if (vao_ == 0) createMesh();
    if (!visible_) return;

    glUseProgram(shader_);
    glm::mat4 model = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_model"), 1, GL_FALSE, &model[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, &projection[0][0]);
    glUniform1i(glGetUniformLocation(shader_, "u_tecTexture"), 0);
    glUniform1f(glGetUniformLocation(shader_, "u_opacity"), opacity_);
    glUniform3f(glGetUniformLocation(shader_, "u_sunDir"), 0.0f, 0.0f, 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void GridLayer::destroy() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (texture_) glDeleteTextures(1, &texture_);
    if (shader_) glDeleteProgram(shader_);
    vao_ = vbo_ = ebo_ = texture_ = shader_ = 0;
}
