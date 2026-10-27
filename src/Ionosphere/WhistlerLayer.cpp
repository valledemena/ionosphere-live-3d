#include "WhistlerLayer.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// Shader unico: posicion + color RGBA por vertice. El llamante usa blend
// aditivo (SRC_ALPHA, ONE), asi que alpha modula el brillo de cada linea.
static const char* whVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 u_view, u_projection;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = u_projection * u_view * vec4(aPos, 1.0);
}
)";

static const char* whFragSrc = R"(
#version 400 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

// 1 unidad = 1 R_E (misma escala real que MagnetosphereLayer/VanAllenBelts).
constexpr float kEarthRadiusUnits = 1.0f;

// Recorte declarado: como mucho 24 celdas dibujadas (las mismas que guarda
// StormAdapter), para acotar vertices y ruido visual en avisos masivos.
constexpr size_t kMaxCells = 24;
constexpr int kPathSamples = 48;          // 48 segmentos = 49 muestras
constexpr float kSpriteRadius = 1.005f;   // centroide justo sobre la superficie
constexpr float kSpriteHalfSize = 0.022f; // semitamano del eje en R_E

// ---- IGRF-14 grado 1, epoca 2025.0 congelada (sin SV), nT: identicos a
// MagnetosphereLayer, luego el eje dipolar coincide en ambas capas. ----
constexpr float kG10 = -29350.0f;
constexpr float kG11 = -1410.30f;
constexpr float kH11 = 4545.50f;

// Convencion del motor (RefShells/GeospaceOverlays/GridLayer).
glm::vec3 latLonToEngine(float latDeg, float lonDeg, float r) {
    const float phi = kPi * 0.5f - latDeg * kDeg2Rad;
    const float theta = kPi - lonDeg * kDeg2Rad;
    const float sn = std::sin(phi);
    const float cs = std::cos(phi);
    return glm::vec3(r * sn * std::cos(theta), r * cs, r * sn * std::sin(theta));
}

// Momento dipolar unitario en marco web Y-up: m ~ (g11, g10, -h11)/|..|.
glm::vec3 dipoleMoment() {
    return glm::normalize(glm::vec3(kG11, kG10, -kH11));
}

// Base ortonormal magnetica (u, v, m): m = eje dipolar; mismo criterio de
// referencia que la web (|m.z| < 0.9 -> (0,0,1), si no (1,0,0)).
void magneticBasis(glm::vec3& u, glm::vec3& v, glm::vec3& m) {
    m = dipoleMoment();
    const glm::vec3 ref = (std::fabs(m.z) < 0.9f) ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                  : glm::vec3(1.0f, 0.0f, 0.0f);
    u = glm::normalize(glm::cross(m, ref));
    v = glm::cross(m, u);
}

// Linea de campo dipolar REAL que pasa por (lat, lon): 49 muestras en
// coordenadas del motor. Port literal de whistlerPath (web):
//   p_web -> coordenadas magneticas q -> latM/lonM -> L = 1/cos^2(latM)
//   -> r = L*cos^2(lambda) -> qp magnetico -> geo_web -> motor (-x, y, -z).
void whistlerPath(float latDeg, float lonDeg, std::vector<glm::vec3>& out) {
    const float lat = latDeg * kDeg2Rad;
    const float lon = lonDeg * kDeg2Rad;
    // Punto unidad en marco web Y-up (latLonToVec3 de geo.ts).
    const glm::vec3 p(std::cos(lat) * std::cos(lon), std::sin(lat),
                      -std::cos(lat) * std::sin(lon));

    glm::vec3 u, v, m;
    magneticBasis(u, v, m);
    const float qx = glm::dot(p, u);
    const float qy = glm::dot(p, v);
    const float qz = glm::dot(p, m);

    const float latM = std::atan2(qz, std::hypot(qx, qy));
    const float lonM = std::atan2(qy, qx);
    // Cascara L de la propia celda (dipolo exacto, igual que traceConjugate).
    const float cl = std::cos(latM);
    const float L = 1.0f / (cl * cl + 1e-9f);
    const float lamMax = std::acos(std::sqrt(1.0f / std::max(L, 1.0f)));

    const float cosLon = std::cos(lonM);
    const float sinLon = std::sin(lonM);

    out.clear();
    out.reserve((size_t)kPathSamples + 1);
    for (int i = 0; i <= kPathSamples; ++i) {
        const float lam = -lamMax + (2.0f * lamMax * (float)i) / (float)kPathSamples;
        const float cosLam = std::cos(lam);
        const float r = L * cosLam * cosLam;
        const float rho = r * cosLam;
        // qp en marco magnetico (z = eje dipolar), escala real en R_E.
        const glm::vec3 qp(rho * cosLon, rho * sinLon, r * std::sin(lam));
        const glm::vec3 geo = qp.x * u + qp.y * v + qp.z * m;
        out.push_back(glm::vec3(-geo.x, geo.y, -geo.z) * kEarthRadiusUnits);
    }
}

}  // namespace

WhistlerLayer::WhistlerLayer() {}
WhistlerLayer::~WhistlerLayer() { destroy(); }

void WhistlerLayer::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &whVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &whFragSrc, nullptr);
    glCompileShader(fs);

    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    dirty_ = true;
    printf("[OK] WhistlerLayer initialized\n");
}

bool WhistlerLayer::visible() const { return visible_; }

void WhistlerLayer::setVisible(bool v) {
    if (v == visible_) return;
    visible_ = v;
    // Al encender hay que garantizar geometria en GPU (pudo no construirse
    // nunca, p.ej. si las celdas llegaron con la capa apagada).
    if (v) dirty_ = true;
}

void WhistlerLayer::setCells(const std::vector<StormCell>& cells) {
    // Dirty solo si cambia el numero de celdas o sus ids: el refresco
    // periodico de NWS con los mismos avisos no regenera la malla.
    bool changed = (cells.size() != cells_.size());
    if (!changed) {
        for (size_t i = 0; i < cells.size(); ++i) {
            if (cells[i].id != cells_[i].id) {
                changed = true;
                break;
            }
        }
    }
    cells_ = cells;
    if (changed) dirty_ = true;
}

int WhistlerLayer::cellCount() const {
    return (int)std::min(cells_.size(), kMaxCells);
}

void WhistlerLayer::update() {
    if (dirty_ && vao_ != 0) rebuild();
}

void WhistlerLayer::rebuild() {
    if (vao_ == 0) return;

    const size_t drawn = std::min(cells_.size(), kMaxCells);
    // Por celda: 12 vertices de marcador (6 segmentos) + 96 de trayectoria.
    std::vector<float> verts;
    verts.reserve(drawn * (12 + 2 * (size_t)kPathSamples) * 7);

    auto push = [&](const glm::vec3& p, const glm::vec4& c) {
        verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
        verts.push_back(c.r); verts.push_back(c.g); verts.push_back(c.b); verts.push_back(c.a);
    };

    // ---- (a) Marcador sprite: estrella de 3 ejes (6 segmentos) ----
    const glm::vec4 spriteCol(1.00f, 0.55f, 0.95f, 0.95f);
    markerFirst_ = 0;
    const glm::vec3 axisX(kSpriteHalfSize, 0.0f, 0.0f);
    const glm::vec3 axisY(0.0f, kSpriteHalfSize, 0.0f);
    const glm::vec3 axisZ(0.0f, 0.0f, kSpriteHalfSize);
    for (size_t k = 0; k < drawn; ++k) {
        const glm::vec3 c0 = latLonToEngine(cells_[k].lat, cells_[k].lon, kSpriteRadius);
        push(c0, spriteCol); push(c0 + axisX, spriteCol);
        push(c0, spriteCol); push(c0 - axisX, spriteCol);
        push(c0, spriteCol); push(c0 + axisY, spriteCol);
        push(c0, spriteCol); push(c0 - axisY, spriteCol);
        push(c0, spriteCol); push(c0 + axisZ, spriteCol);
        push(c0, spriteCol); push(c0 - axisZ, spriteCol);
    }
    markerCount_ = (int)verts.size() / 7 - markerFirst_;

    // ---- (b) Linea de campo dipolar (ducto del whistler) por celda ----
    const glm::vec4 pathCol(0.72f, 0.45f, 1.00f, 0.50f);
    pathFirst_ = (int)verts.size() / 7;
    std::vector<glm::vec3> pts;
    for (size_t k = 0; k < drawn; ++k) {
        whistlerPath(cells_[k].lat, cells_[k].lon, pts);
        for (size_t i = 1; i < pts.size(); ++i) {
            push(pts[i - 1], pathCol);
            push(pts[i], pathCol);
        }
    }
    pathCount_ = (int)verts.size() / 7 - pathFirst_;

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)),
                 verts.data(), GL_DYNAMIC_DRAW);
    // Layout: pos(3) + rgba(4) = 7 floats por vertice.
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    dirty_ = false;
}

void WhistlerLayer::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_) return;
    if (dirty_) rebuild();
    if (markerCount_ == 0 && pathCount_ == 0) return;

    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE,
                       glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE,
                       glm::value_ptr(projection));

    glBindVertexArray(vao_);
    if (markerCount_ > 0) glDrawArrays(GL_LINES, markerFirst_, markerCount_);
    if (pathCount_ > 0) glDrawArrays(GL_LINES, pathFirst_, pathCount_);
    glBindVertexArray(0);
}

void WhistlerLayer::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (shader_) glDeleteProgram(shader_);
    vao_ = vbo_ = shader_ = 0;
    markerFirst_ = markerCount_ = 0;
    pathFirst_ = pathCount_ = 0;
    dirty_ = true;
}
