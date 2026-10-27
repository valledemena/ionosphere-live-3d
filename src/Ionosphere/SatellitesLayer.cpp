#include "SatellitesLayer.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// Shader unico para puntos y anillos. El flag u_ring elige si hay que rotar
// el vertice por el GMST (anillos guardados en el marco inercial). El color
// y el tamano los decide la CPU; el blend/depth los fija App.cpp.
static const char* satVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
layout(location=2) in vec2 aCorner;
uniform mat4 u_view, u_projection;
uniform float u_pointSize;
uniform float u_gmst;
uniform int u_ring;
out vec4 vColor;
out vec2 vCorner;
void main() {
    vec3 p = aPos;
    if (u_ring == 1) {
        float c = cos(u_gmst);
        float s = sin(u_gmst);
        vec3 e = vec3(c * p.x + s * p.y, -s * p.x + c * p.y, p.z);
        // ECEF -> convencion del motor: x = -xe, y = ze, z = ye.
        p = vec3(-e.x, e.z, e.y);
    }
    vec4 clip = u_projection * u_view * vec4(p, 1.0);
    // Billboard en espacio de recorte: tamano constante en pantalla.
    clip.xy += aCorner * u_pointSize * clip.w;
    gl_Position = clip;
    vColor = aColor;
    vCorner = aCorner;
}
)";

static const char* satFragSrc = R"(
#version 400 core
in vec4 vColor;
in vec2 vCorner;
out vec4 fragColor;
void main() {
    // aCorner es (0,0) en los anillos (GL_LINES): d2 = 0, sin recorte.
    float d2 = dot(vCorner, vCorner);
    if (d2 > 1.0) discard;
    float edge = smoothstep(1.0, 0.55, d2);
    fragColor = vec4(vColor.rgb, vColor.a * edge);
}
)";

namespace {

constexpr int kFloatsPerVert = 9;       // pos(3) + rgba(4) + corner(2)
constexpr int kVertsPerSat = 6;         // dos triangulos por billboard
constexpr int kRingSegments = 64;       // segmentos por anillo (2 verts cada uno)
constexpr int kMaxRings = 150;          // subconjunto determinista de anillos
constexpr double kPointStepSec = 0.05;  // rebuild de puntos <= 20 Hz
constexpr double kRingRefreshSec = 600.0;  // refresco de anillos (10 min)
constexpr float kPointRadiusPx = 2.0f;  // radio del marcador en pixeles
constexpr float kPointAlpha = 0.45f;
constexpr float kRingAlpha = 0.18f;

// Fronteras de regimen (km sobre el elipsoide WGS84, altitud media).
constexpr double kLeoMaxKm = 2000.0;
constexpr double kMeoMaxKm = 30000.0;
constexpr double kGeoAltKm = 35786.0;
constexpr double kGeoBandKm = 2000.0;
constexpr double kGeoMaxEcc = 0.05;

// Seis esquinas del quad unitario (dos triangulos, sentido antihorario).
const float kCorners[kVertsPerSat][2] = {
    {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
    {-1.0f, -1.0f}, {1.0f, 1.0f},  {-1.0f, 1.0f},
};

}  // namespace

SatellitesLayer::SatellitesLayer() {}
SatellitesLayer::~SatellitesLayer() { destroy(); }

void SatellitesLayer::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &satVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &satFragSrc, nullptr);
    glCompileShader(fs);

    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    uView_ = glGetUniformLocation(shader_, "u_view");
    uProj_ = glGetUniformLocation(shader_, "u_projection");
    uPointSize_ = glGetUniformLocation(shader_, "u_pointSize");
    uGmst_ = glGetUniformLocation(shader_, "u_gmst");
    uRing_ = glGetUniformLocation(shader_, "u_ring");

    glGenVertexArrays(1, &pointVao_);
    glGenBuffers(1, &pointVbo_);
    glGenVertexArrays(1, &ringVao_);
    glGenBuffers(1, &ringVbo_);

    // Layout comun: pos(3) + color(4) + corner(2).
    const GLsizei stride = (GLsizei)(kFloatsPerVert * sizeof(float));
    const GLuint vaos[2] = {pointVao_, ringVao_};
    const GLuint vbos[2] = {pointVbo_, ringVbo_};
    for (int i = 0; i < 2; ++i) {
        glBindVertexArray(vaos[i]);
        glBindBuffer(GL_ARRAY_BUFFER, vbos[i]);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                              (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                              (void*)(7 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    pointsDirty_ = true;
    ringsDirty_ = true;
    printf("[OK] SatellitesLayer initialized\n");
}

void SatellitesLayer::destroy() {
    if (pointVbo_) glDeleteBuffers(1, &pointVbo_);
    if (pointVao_) glDeleteVertexArrays(1, &pointVao_);
    if (ringVbo_) glDeleteBuffers(1, &ringVbo_);
    if (ringVao_) glDeleteVertexArrays(1, &ringVao_);
    if (shader_) glDeleteProgram(shader_);
    pointVbo_ = pointVao_ = ringVbo_ = ringVao_ = shader_ = 0;
    uView_ = uProj_ = uPointSize_ = uGmst_ = uRing_ = -1;
    pointVertexCount_ = 0;
    ringVertexCount_ = 0;
    satCount_ = 0;
    ringCount_ = 0;
    pointsDirty_ = true;
    ringsDirty_ = true;
    snapshotValid_ = false;
    sats_.clear();
}

void SatellitesLayer::regimeColor(double meanAltKm, double ecc, float& r, float& g, float& b) {
    // Orden: LEO, GEO (antes que MEO: 35786 no cae en la banda MEO), MEO, HEO.
    if (meanAltKm < kLeoMaxKm) {
        r = 0.35f; g = 0.85f; b = 1.00f;   // LEO cian
    } else if (std::fabs(meanAltKm - kGeoAltKm) <= kGeoBandKm && ecc < kGeoMaxEcc) {
        r = 1.00f; g = 0.35f; b = 0.95f;   // GEO magenta
    } else if (meanAltKm < kMeoMaxKm) {
        r = 1.00f; g = 0.88f; b = 0.30f;   // MEO amarillo
    } else {
        r = 1.00f; g = 0.55f; b = 0.15f;   // HEO naranja
    }
}

void SatellitesLayer::setSnapshot(const TleSnapshot& snap, double nowUtc) {
    // Si App.cpp repite el MISMO catalogo cada frame, no se copian ~16k filas:
    // basta con anotar el instante (el movimiento lo hace update()).
    const bool sameCatalog =
        sats_.size() == snap.sats.size() && !sats_.empty() &&
        sats_.front().noradId == snap.sats.front().noradId &&
        sats_.back().noradId == snap.sats.back().noradId &&
        sats_.front().el.epochUtc == snap.sats.front().el.epochUtc &&
        sats_.back().el.epochUtc == snap.sats.back().el.epochUtc;
    if (sameCatalog && snapshotValid_) {
        nowUtc_ = nowUtc;
        return;
    }

    sats_ = snap.sats;   // copia: la capa no comparte estado con el adapter
    snapshotValid_ = snap.valid && !sats_.empty();
    nowUtc_ = nowUtc;
    if (!snapshotValid_) {
        // Snapshot invalido: no se dibuja nada (ni datos viejos ni inventados).
        sats_.clear();
        satCount_ = 0;
        ringCount_ = 0;
        pointVertexCount_ = 0;
        ringVertexCount_ = 0;
    }
    pointsDirty_ = true;
    ringsDirty_ = true;
}

void SatellitesLayer::update(double utc) {
    nowUtc_ = utc;
    gmst_ = (float)orbitprop::gmstRad(utc);
    if (pointVao_ == 0 || ringVao_ == 0) return;   // sin init no hay GL
    if (!snapshotValid_) {
        satCount_ = 0;
        ringCount_ = 0;
        pointVertexCount_ = 0;
        ringVertexCount_ = 0;
        pointsDirty_ = false;
        ringsDirty_ = false;
        return;
    }
    const bool timeMoved = !(std::fabs(utc - lastPointUtc_) < kPointStepSec);
    if (pointsDirty_ || timeMoved) rebuildPoints();
    // Anillos: al cambiar de snapshot o cada 10 min (la precesion J2 mueve el
    // plano; el GMST de cada frame solo rota la Tierra).
    if (ringsDirty_ || !(std::fabs(utc - lastRingUtc_) < kRingRefreshSec)) rebuildRings();
}

void SatellitesLayer::rebuildPoints() {
    pointsDirty_ = false;
    lastPointUtc_ = nowUtc_;
    pointVertexCount_ = 0;
    satCount_ = 0;
    if (pointVao_ == 0) return;

    verts_.clear();
    verts_.reserve(sats_.size() * kVertsPerSat * kFloatsPerVert);

    for (const TleSat& sat : sats_) {
        double x = 0.0, y = 0.0, z = 0.0, altKm = 0.0;
        if (!orbitprop::toScene(sat.el, nowUtc_, x, y, z, altKm)) continue;
        float cr = 0.0f, cg = 0.0f, cb = 0.0f;
        regimeColor(orbitprop::meanAltitudeKm(sat.el), sat.el.ecc, cr, cg, cb);
        for (int k = 0; k < kVertsPerSat; ++k) {
            verts_.push_back((float)x);
            verts_.push_back((float)y);
            verts_.push_back((float)z);
            verts_.push_back(cr);
            verts_.push_back(cg);
            verts_.push_back(cb);
            verts_.push_back(kPointAlpha);
            verts_.push_back(kCorners[k][0]);
            verts_.push_back(kCorners[k][1]);
        }
        ++satCount_;
    }

    pointVertexCount_ = (int)(verts_.size() / kFloatsPerVert);
    upload(pointVbo_, verts_);
}

void SatellitesLayer::rebuildRings() {
    ringsDirty_ = false;
    lastRingUtc_ = nowUtc_;
    ringVertexCount_ = 0;
    ringCount_ = 0;
    if (ringVao_ == 0) return;

    verts_.clear();
    const size_t n = sats_.size();
    if (n == 0) {
        upload(ringVbo_, verts_);
        return;
    }

    // Subconjunto determinista: stride fijo sobre el catalogo (sin azar y sin
    // depender del frame). El primero de cada tramo es el representante.
    const size_t stride = std::max<size_t>(1, n / (size_t)kMaxRings);
    size_t built = 0;
    for (size_t i = 0; i < n && built < (size_t)kMaxRings; i += stride) {
        const TleSat& sat = sats_[i];
        const double periodMin = orbitprop::orbitalPeriodMin(sat.el);
        if (!(periodMin > 0.0)) continue;
        const double periodSec = periodMin * 60.0;
        const double stepSec = periodSec / (double)kRingSegments;

        float cr = 0.0f, cg = 0.0f, cb = 0.0f;
        regimeColor(orbitprop::meanAltitudeKm(sat.el), sat.el.ecc, cr, cg, cb);

        const size_t ringStart = verts_.size();
        double prev[3] = {0.0, 0.0, 0.0};
        bool havePrev = false;
        bool ok = true;
        for (int k = 0; k <= kRingSegments; ++k) {
            double r[3] = {0.0, 0.0, 0.0};
            if (!orbitprop::propagateTeme(sat.el, nowUtc_ + stepSec * (double)k, r)) {
                ok = false;
                break;
            }
            // Radio geocentrico en unidades del motor (1 R_E = 6371 km).
            const double s = 1.0 / orbitprop::kEngineRadiusKm;
            const double px = r[0] * s, py = r[1] * s, pz = r[2] * s;
            if (havePrev) {
                verts_.push_back((float)prev[0]);
                verts_.push_back((float)prev[1]);
                verts_.push_back((float)prev[2]);
                verts_.push_back(cr);
                verts_.push_back(cg);
                verts_.push_back(cb);
                verts_.push_back(kRingAlpha);
                verts_.push_back(0.0f);
                verts_.push_back(0.0f);

                verts_.push_back((float)px);
                verts_.push_back((float)py);
                verts_.push_back((float)pz);
                verts_.push_back(cr);
                verts_.push_back(cg);
                verts_.push_back(cb);
                verts_.push_back(kRingAlpha);
                verts_.push_back(0.0f);
                verts_.push_back(0.0f);
            }
            prev[0] = px;
            prev[1] = py;
            prev[2] = pz;
            havePrev = true;
        }
        if (ok) ++built;
        else verts_.resize(ringStart);   // anillo incompleto: fuera
    }

    ringCount_ = (int)built;
    ringVertexCount_ = (int)(verts_.size() / kFloatsPerVert);
    upload(ringVbo_, verts_);
}

void SatellitesLayer::upload(GLuint vbo, const std::vector<float>& verts) {
    if (vbo == 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)),
                 verts.empty() ? nullptr : verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SatellitesLayer::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_ || shader_ == 0) return;
    if (!snapshotValid_ || pointVertexCount_ == 0) return;   // sin datos, nada

    // Tamano del marcador en NDC a partir del viewport actual (radio fijo en
    // pixeles). No cambia estado: solo consulta el viewport.
    GLint vp[4] = {0, 0, 1, 1};
    glGetIntegerv(GL_VIEWPORT, vp);
    const int height = (vp[3] > 0) ? vp[3] : 1080;
    const float pointSizeNdc = 2.0f * kPointRadiusPx / (float)height;

    glUseProgram(shader_);
    glUniformMatrix4fv(uView_, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(uProj_, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(uPointSize_, pointSizeNdc);

    // Anillos primero (bajo los puntos): guardados en inercial, rotados aqui
    // por el GMST del frame.
    if (ringVertexCount_ > 0 && ringVao_ != 0) {
        glUniform1i(uRing_, 1);
        glUniform1f(uGmst_, gmst_);
        glBindVertexArray(ringVao_);
        glDrawArrays(GL_LINES, 0, ringVertexCount_);
    }

    glUniform1i(uRing_, 0);
    glBindVertexArray(pointVao_);
    glDrawArrays(GL_TRIANGLES, 0, pointVertexCount_);
    glBindVertexArray(0);
}
