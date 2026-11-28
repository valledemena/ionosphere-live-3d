#include "AuroraCurtains.h"

#include "Data/IrtamGridEval.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

// Umbral declarado en el pre-registro: solo celdas Ovation > 8 generan
// cortina. Con el max real del epoch se registra el conteo exacto.
static constexpr float kThreshold = 8.0f;
static constexpr float kHalfWidthDeg = 2.0f;
static constexpr float kAltBaseKm = 100.0f;
static constexpr float kAltTopKm = 250.0f;
static constexpr float kKmPerEarthRadius = 6371.0f;
static constexpr float kNormMax = 40.0f;
static constexpr double kIgrfEpoch = 2025.0;

static const char* curtainVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aHeight;
layout(location=2) in float aVal;
uniform mat4 u_view, u_projection;
uniform vec3 u_fieldAxis;
out float vHeight;
out float vVal;
void main() {
    vec3 r = normalize(aPos);
    // Direccion del campo dipolar (fuente igrfFull NMAX=13, marco Tierra).
    vec3 b = 3.0 * dot(r, u_fieldAxis) * r - u_fieldAxis;
    float hKm = mix(100.0, 250.0, aHeight);
    float rr = 1.0 + hKm / 6371.0;
    vec3 p = r * rr + normalize(b) * (aHeight * 0.012);
    gl_Position = u_projection * u_view * vec4(p, 1.0);
    vHeight = aHeight;
    vVal = aVal;
}
)";

static const char* curtainFragSrc = R"(
#version 400 core
in float vHeight;
in float vVal;
uniform float u_intensity;
out vec4 fragColor;
void main() {
    vec3 green = vec3(0.10, 1.00, 0.35);   // 557,7 nm (O I)
    vec3 red   = vec3(1.00, 0.12, 0.18);   // 630,0 nm (O I)
    vec3 n2    = vec3(0.45, 0.25, 1.00);   // N2+ (borde inferior)
    vec3 col = mix(green, red, smoothstep(0.30, 1.0, vHeight));
    col += n2 * (1.0 - smoothstep(0.0, 0.25, vHeight)) * 0.6;
    float fade = smoothstep(0.0, 0.12, vHeight) * (1.0 - smoothstep(0.88, 1.0, vHeight));
    float a = clamp(vVal * u_intensity * fade, 0.0, 1.0);
    fragColor = vec4(col, a);   // blend aditivo SRC_ALPHA, ONE del llamante
}
)";

static glm::vec3 latLonToVec(float latDeg, float lonDeg) {
    const float lat = glm::radians(latDeg);
    const float lon = glm::radians(lonDeg);
    const float cl = std::cos(lat);
    return glm::vec3(cl * std::cos(lon), std::sin(lat), cl * std::sin(lon));
}

void AuroraCurtains::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &curtainVertSrc, nullptr);
    glShaderSource(fs, 1, &curtainFragSrc, nullptr);
    glCompileShader(vs); glCompileShader(fs);
    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    GLint ok = GL_FALSE;
    glGetProgramiv(shader_, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        fprintf(stderr, "[AuroraCurtains] shader link failed\n");
        glDeleteProgram(shader_);
        shader_ = 0;
    }
    glDeleteShader(vs); glDeleteShader(fs);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Eje dipolar real: IGRF-14 NMAX=13 en marco Tierra (mismo origen que
    // DW.21). Sin mezclar con GSM, donde vive la magnetopausa Shue.
    double bn = 0.0, be = 0.0, bd = 0.0, ba = 0.0;
    if (irtameval::igrfFull(kIgrfEpoch, 0.0, 0.0, 0.0, bn, be, bd, ba) &&
        std::isfinite(bn) && std::isfinite(be) && std::isfinite(bd) &&
        std::fabs(bn) > 1.0) {
        const float tilt = std::atan2((float)std::fabs(bd), (float)std::fabs(bn));
        const float sgn = (be >= 0.0f) ? 1.0f : -1.0f;
        fieldAxis_ = glm::normalize(glm::vec3(sgn * std::sin(tilt), std::cos(tilt), 0.0f));
    }
    fprintf(stderr, "[AuroraCurtains] init (IGRF-14 epoch 2025.0, frame Earth)\n");
}

void AuroraCurtains::updateFromGrid(const GridData& grid) {
    gridTimestamp_ = grid.timestamp;
    gridMax_ = 0.0f;
    activeCells_ = 0;
    if (!grid.valid || grid.width <= 0 || grid.height <= 0 ||
        (int)grid.values.size() < grid.width * grid.height) return;

    gridMax_ = *std::max_element(grid.values.begin(), grid.values.end());
    std::vector<float> verts;
    verts.reserve(grid.values.size() * 2);
    for (int lat = 0; lat < grid.height; ++lat) {
        const float latDeg = -90.0f + (lat + 0.5f) * 180.0f / (float)grid.height;
        for (int lon = 0; lon < grid.width; ++lon) {
            const float val = grid.values[lat * grid.width + lon];
            if (!(val > kThreshold)) continue;
            ++activeCells_;
            const float lonDeg = -180.0f + (lon + 0.5f) * 360.0f / (float)grid.width;
            const glm::vec3 p0 = latLonToVec(latDeg, lonDeg - kHalfWidthDeg);
            const glm::vec3 p1 = latLonToVec(latDeg, lonDeg + kHalfWidthDeg);
            const float v = glm::clamp(val / kNormMax, 0.0f, 1.0f);
            const float quad[6][5] = {
                {p0.x,p0.y,p0.z,0.0f,v}, {p1.x,p1.y,p1.z,0.0f,v}, {p0.x,p0.y,p0.z,1.0f,v},
                {p1.x,p1.y,p1.z,0.0f,v}, {p1.x,p1.y,p1.z,1.0f,v}, {p0.x,p0.y,p0.z,1.0f,v}
            };
            for (int k = 0; k < 6; ++k)
                for (int j = 0; j < 5; ++j) verts.push_back(quad[k][j]);
        }
    }
    curtainCount_ = activeCells_;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float),
                 verts.empty() ? nullptr : verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    fprintf(stderr, "[AuroraCurtains] epoch=%.0f max=%.1f cells>8=%d curtains=%d\n",
            grid.timestamp, gridMax_, activeCells_, curtainCount_);
}

void AuroraCurtains::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_ || shader_ == 0 || vao_ == 0 || curtainCount_ <= 0) return;
    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(shader_, "u_fieldAxis"), 1, glm::value_ptr(fieldAxis_));
    glUniform1f(glGetUniformLocation(shader_, "u_intensity"), intensity_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, curtainCount_ * 6);
    glBindVertexArray(0);
}

void AuroraCurtains::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (shader_) glDeleteProgram(shader_);
    vbo_ = vao_ = shader_ = 0;
    curtainCount_ = activeCells_ = 0;
    visible_ = false;
}
