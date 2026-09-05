#include "GridLayer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <algorithm>

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

static void buildPalette(const char* name, size_t n, float* out) {
    // Paletas como gradientes lineales entre stops (vec3 RGB). 
    struct Stop { float pos, r, g, b; };
    const Stop* stops = nullptr;
    int nStops = 0;

    // Viridis
    static const Stop vir[] = {
        {0.00f, 0.267f, 0.004f, 0.329f}, {0.13f, 0.282f, 0.140f, 0.458f},
        {0.25f, 0.253f, 0.265f, 0.530f}, {0.38f, 0.191f, 0.407f, 0.557f},
        {0.50f, 0.127f, 0.566f, 0.551f}, {0.63f, 0.206f, 0.718f, 0.473f},
        {0.75f, 0.478f, 0.824f, 0.318f}, {0.88f, 0.741f, 0.873f, 0.150f},
        {1.00f, 0.993f, 0.906f, 0.144f},
    };
    // Inferno (aproximado)
    static const Stop inf[] = {
        {0.00f, 0.000f, 0.000f, 0.040f}, {0.13f, 0.108f, 0.031f, 0.310f},
        {0.25f, 0.358f, 0.045f, 0.451f}, {0.38f, 0.595f, 0.083f, 0.405f},
        {0.50f, 0.786f, 0.153f, 0.203f}, {0.63f, 0.932f, 0.318f, 0.131f},
        {0.75f, 0.984f, 0.500f, 0.147f}, {0.88f, 0.988f, 0.711f, 0.300f},
        {1.00f, 0.988f, 0.998f, 0.645f},
    };
    // Plasma (aproximado)
    static const Stop plm[] = {
        {0.00f, 0.050f, 0.030f, 0.527f}, {0.13f, 0.240f, 0.017f, 0.678f},
        {0.25f, 0.471f, 0.028f, 0.696f}, {0.38f, 0.681f, 0.107f, 0.594f},
        {0.50f, 0.838f, 0.264f, 0.443f}, {0.63f, 0.944f, 0.446f, 0.320f},
        {0.75f, 0.986f, 0.654f, 0.258f}, {0.88f, 0.989f, 0.852f, 0.354f},
        {1.00f, 0.940f, 0.975f, 0.131f},
    };
    // Turbo (approximation multi-hue)
    static const Stop trb[] = {
        {0.00f, 0.190f, 0.071f, 0.232f}, {0.13f, 0.214f, 0.357f, 0.770f},
        {0.25f, 0.165f, 0.604f, 0.843f}, {0.38f, 0.196f, 0.784f, 0.582f},
        {0.50f, 0.450f, 0.866f, 0.314f}, {0.63f, 0.762f, 0.784f, 0.042f},
        {0.75f, 0.954f, 0.555f, 0.036f}, {0.88f, 0.966f, 0.256f, 0.052f},
        {1.00f, 0.648f, 0.033f, 0.112f},
    };
    // Aurora (NOAA Ovation overlay): transparent black -> deep green -> neon.
    static const Stop aur[] = {
        {0.00f, 0.000f, 0.010f, 0.000f}, {0.25f, 0.000f, 0.250f, 0.050f},
        {0.50f, 0.050f, 0.650f, 0.150f}, {0.75f, 0.300f, 1.000f, 0.350f},
        {1.00f, 0.850f, 1.000f, 0.550f},
    };

    std::string nm = name;
    if (nm == "inferno") { stops = inf; nStops = sizeof(inf)/sizeof(inf[0]); }
    else if (nm == "plasma") { stops = plm; nStops = sizeof(plm)/sizeof(plm[0]); }
    else if (nm == "turbo") { stops = trb; nStops = sizeof(trb)/sizeof(trb[0]); }
    else if (nm == "aurora") { stops = aur; nStops = sizeof(aur)/sizeof(aur[0]); }
    else { stops = vir; nStops = sizeof(vir)/sizeof(vir[0]); }

    for (size_t i = 0; i < n; i++) {
        float t = (float)i / (float)(n - 1);
        int s = 0;
        for (int j = 0; j < nStops - 1; j++) {
            if (t >= stops[j].pos && t <= stops[j + 1].pos) { s = j; break; }
        }
        float f = (t - stops[s].pos) / (stops[s + 1].pos - stops[s].pos + 1e-6f);
        out[i * 3 + 0] = stops[s].r + f * (stops[s + 1].r - stops[s].r);
        out[i * 3 + 1] = stops[s].g + f * (stops[s + 1].g - stops[s].g);
        out[i * 3 + 2] = stops[s].b + f * (stops[s + 1].b - stops[s].b);
    }
}

void GridLayer::setDisplace(float a) {
    a = glm::clamp(a, 0.0f, 0.2f);
    if (a != displaceAmp_) { displaceAmp_ = a; meshDirty_ = true; }
}

float GridLayer::sampleNorm(float u, float v) const {
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
    float range = lastMax_ - lastMin_ + 1e-6f;
    return glm::clamp((val - lastMin_) / range, 0.0f, 1.0f);
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
            float r = base + displaceAmp_ * sampleNorm(u, v);
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
    buildPalette(name.c_str(), 256, colormap_.data());
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
    textureDirty_ = true;
    updateTexture();
    // El relieve depende de los datos: refrescar posiciones si hay malla y amp.
    if (displaceAmp_ > 0.0f && vao_ != 0) updatePositions();
}

void GridLayer::updateTexture() {
    if (grid_.width == 0 || grid_.height == 0) return;

    // Normalización: rango fijo si se configuró, si no min/max de los datos.
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
