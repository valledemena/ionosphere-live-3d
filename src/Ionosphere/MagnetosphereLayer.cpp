#include "MagnetosphereLayer.h"
#include "Data/ShueMath.h"
#include "Data/IrtamGridEval.h"   // DW.21: IGRF-14 completo (NMAX=13)
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// Shader unico: posicion + color RGBA por vertice. El llamante usa blend
// aditivo (SRC_ALPHA, ONE), asi que alpha modula el brillo de cada linea.
static const char* magVertSrc = R"(
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

static const char* magFragSrc = R"(
#version 400 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

// DW.21: lineas gruesas antialiasadas con geometry shader (core 4.0).
static const char* fieldVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
out vec4 vColor;
void main() { vColor = aColor; gl_Position = vec4(aPos, 1.0); }
)";

static const char* fieldGeomSrc = R"(
#version 400 core
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
uniform mat4 u_view, u_projection;
uniform float u_halfWidthNdc;
in vec4 vColor[];
out vec4 gColor;
out float vSide;
void main() {
    vec4 p0 = u_projection * u_view * gl_in[0].gl_Position;
    vec4 p1 = u_projection * u_view * gl_in[1].gl_Position;
    vec2 a = p0.xy / p0.w, b = p1.xy / p1.w;
    vec2 d = b - a;
    float len = length(d);
    float aspect = u_projection[1][1] / u_projection[0][0];
    vec2 n = (len > 1e-7) ? vec2(-d.y / aspect, d.x) * (u_halfWidthNdc / len)
                          : vec2(u_halfWidthNdc / aspect, 0.0);
    gColor = vColor[0];
    vSide = -1.0; gl_Position = p0 + vec4(n * p0.w * -1.0, 0.0, 0.0); EmitVertex();
    vSide =  1.0; gl_Position = p0 + vec4(n * p0.w,        0.0, 0.0); EmitVertex();
    vSide = -1.0; gl_Position = p1 + vec4(n * p1.w * -1.0, 0.0, 0.0); EmitVertex();
    vSide =  1.0; gl_Position = p1 + vec4(n * p1.w,        0.0, 0.0); EmitVertex();
    EndPrimitive();
}
)";

static const char* fieldFragSrc = R"(
#version 400 core
in vec4 gColor;
in float vSide;
out vec4 fragColor;
void main() {
    float coverage = 0.78 + 0.22 * (1.0 - abs(vSide));
    fragColor = vec4(gColor.rgb * coverage, gColor.a);
}
)";

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

// 1 unidad = 1 R_E. Sin compresion radial: la magnetopausa se dibuja a escala
// real (r0 ~ 6-12 R_E) y la camara debe alejarse para verla entera.
constexpr float kEarthRadiusUnits = 1.0f;

// ---- IGRF-14 grado 1, epoca 2025.0 congelada (sin SV), nT ----
constexpr float kG10 = -29350.0f;
constexpr float kG11 = -1410.30f;
constexpr float kH11 = 4545.50f;

// Los coeficientes y las formulas de Shue-1998 viven en Data/ShueMath.h porque
// tambien los usa el rio de viento solar (SolarWindRiverSim) para desviar sus
// particulas alrededor de la misma superficie. Aqui solo se aliasan.
float shueR0(float bzNT, float dpNPa) { return shuemath::r0(bzNT, dpNPa); }
float shueAlpha(float bzNT, float dpNPa) { return shuemath::alpha(bzNT, dpNPa); }

// Estiramiento nocturno v1 (declarado, NO Tsyganenko): 1 sin Bz sur.
float tailStretchFactor(float bzNT) {
    return 1.0f + (0.15f * std::max(0.0f, -bzNT)) / 5.0f;
}

// Redondeo para la clave de rebuild (r0 2 dp, alpha/cola 3 dp).
float roundKey(float v, float scale) {
    return std::round(v * scale) / scale;
}

// DW.21: con lineas gruesas activadas se consumen los coeficientes IGRF-14
// completos (NMAX=13) ya embebidos: la inclinacion del eje se deriva del
// campo real en el ecuador geografico. Es una APROXIMACION v2 del eje
// dipolar, no una traza RK2 completa; el marco sigue siendo Tierra, nunca
// GSM (la magnetopausa Shue permanece en GSM).
// Con flag OFF se usa el arreglo historico de grado 1 (byte-identico).
static bool g_useFullIgrf = false;

glm::vec3 dipoleMoment() {
    if (g_useFullIgrf) {
        double bn = 0.0, be = 0.0, bd = 0.0, ba = 0.0;
        if (irtameval::igrfFull(2025.0, 0.0, 0.0, 0.0, bn, be, bd, ba) &&
            std::isfinite(bn) && std::isfinite(be) && std::isfinite(bd) &&
            std::fabs(bn) > 1.0) {
            const float tilt = std::atan2((float)std::fabs(bd), (float)std::fabs(bn));
            const float sgn = (be >= 0.0) ? 1.0f : -1.0f;
            return glm::normalize(glm::vec3(sgn * std::sin(tilt),
                                            std::cos(tilt), 0.0f));
        }
    }
    return glm::normalize(glm::vec3(kG11, kG10, -kH11));
}

// Base ortonormal magnetica (u, v, m): m = eje dipolar, z = m en el marco
// magnetico. Mismo criterio de referencia que la web (|m.z| < 0.9).
void magneticBasis(glm::vec3& u, glm::vec3& v, glm::vec3& m) {
    m = dipoleMoment();
    const glm::vec3 ref = (std::fabs(m.z) < 0.9f) ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                  : glm::vec3(1.0f, 0.0f, 0.0f);
    u = glm::normalize(glm::cross(m, ref));
    v = glm::cross(m, u);
}

}  // namespace

MagnetosphereLayer::MagnetosphereLayer() {}
MagnetosphereLayer::~MagnetosphereLayer() { destroy(); }

// Dp[nPa] = 1.6726e-6 * n[cm^-3] * v[km/s]^2 (mp*n*v^2).
float MagnetosphereLayer::dynamicPressure(float densityCm3, float speedKms) {
    return shuemath::dynamicPressure(densityCm3, speedKms);
}

float MagnetosphereLayer::subsolarR0() const {
    return shueR0(bzNT_, dynamicPressure(densityCm3_, speedKms_));
}

float MagnetosphereLayer::alpha() const {
    return shueAlpha(bzNT_, dynamicPressure(densityCm3_, speedKms_));
}

float MagnetosphereLayer::tailStretch() const {
    return tailStretchFactor(bzNT_);
}

bool MagnetosphereLayer::visible() const { return visible_; }

void MagnetosphereLayer::setThickLines(bool v) {
    if (thickLines_ == v) return;
    thickLines_ = v;
    g_useFullIgrf = v;  // DW.21: solo con flag ON se consumen los coeficientes completos
    dirty_ = true;
}

bool MagnetosphereLayer::thickLines() const { return thickLines_; }

void MagnetosphereLayer::setVisible(bool v) {
    if (v == visible_) return;
    visible_ = v;
    // Al encender hay que garantizar malla en GPU (pudo destruirse o no
    // haberse construido nunca).
    if (v) dirty_ = true;
}

void MagnetosphereLayer::refreshKey() {
    // Clave redondeada: solo marca dirty si cambia de verdad (equivale al
    // magnetoKey de la web, que usa toFixed(2)/toFixed(3)).
    const float dp = dynamicPressure(densityCm3_, speedKms_);
    const float r0 = roundKey(shueR0(bzNT_, dp), 100.0f);
    const float al = roundKey(shueAlpha(bzNT_, dp), 1000.0f);
    const float tl = roundKey(tailStretchFactor(bzNT_), 1000.0f);
    if (!keyValid_ || r0 != keyR0_ || al != keyAlpha_ || tl != keyTail_) {
        keyR0_ = r0;
        keyAlpha_ = al;
        keyTail_ = tl;
        keyValid_ = true;
        dirty_ = true;
    }
}

void MagnetosphereLayer::setSolarWind(float speedKms, float densityCm3, float bzNT) {
    // Cada valor no finito se ignora por separado (se conserva el ultimo
    // valido), igual que hacen los setters de GeospaceOverlays.
    bool changed = false;
    if (std::isfinite(speedKms) && speedKms != speedKms_) {
        speedKms_ = speedKms;
        changed = true;
    }
    if (std::isfinite(densityCm3) && densityCm3 != densityCm3_) {
        densityCm3_ = densityCm3;
        changed = true;
    }
    if (std::isfinite(bzNT) && bzNT != bzNT_) {
        bzNT_ = bzNT;
        changed = true;
    }
    if (changed) refreshKey();
}

void MagnetosphereLayer::setSunDirection(const glm::vec3& sunWorld) {
    const float len = glm::length(sunWorld);
    if (!std::isfinite(len) || len < 1e-6f) return;
    const glm::vec3 s = sunWorld / len;
    if (glm::dot(s, sun_) < 0.99995f) {   // ~0.57 grados: mismo umbral que GeospaceOverlays
        sun_ = s;
        dirty_ = true;
    }
}

void MagnetosphereLayer::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &magVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &magFragSrc, nullptr);
    glCompileShader(fs);

    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    // DW.21: segundo programa con geometry shader; si falla, se usa el normal.
    GLuint fvs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fgs = glCreateShader(GL_GEOMETRY_SHADER);
    GLuint ffs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fvs, 1, &fieldVertSrc, nullptr);
    glShaderSource(fgs, 1, &fieldGeomSrc, nullptr);
    glShaderSource(ffs, 1, &fieldFragSrc, nullptr);
    glCompileShader(fvs); glCompileShader(fgs); glCompileShader(ffs);
    thickShader_ = glCreateProgram();
    glAttachShader(thickShader_, fvs);
    glAttachShader(thickShader_, fgs);
    glAttachShader(thickShader_, ffs);
    glLinkProgram(thickShader_);
    GLint linked = GL_FALSE;
    glGetProgramiv(thickShader_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        fprintf(stderr, "[MagnetosphereLayer] thick-line shader link failed; fallback a GL_LINES\n");
        glDeleteProgram(thickShader_);
        thickShader_ = 0;
    }
    glDeleteShader(fvs); glDeleteShader(fgs); glDeleteShader(ffs);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    keyValid_ = false;
    dirty_ = true;
    printf("[OK] MagnetosphereLayer initialized\n");
}

void MagnetosphereLayer::update() {
    if (dirty_ && vao_ != 0) rebuild();
}

void MagnetosphereLayer::rebuild() {
    if (vao_ == 0) return;

    const float dp = dynamicPressure(densityCm3_, speedKms_);
    const float r0 = shueR0(bzNT_, dp);
    const float al = shueAlpha(bzNT_, dp);
    const float tail = tailStretchFactor(bzNT_);

    // La clave queda sincronizada con lo realmente construido.
    keyValid_ = true;
    keyR0_ = roundKey(r0, 100.0f);
    keyAlpha_ = roundKey(al, 1000.0f);
    keyTail_ = roundKey(tail, 1000.0f);

    // Layout: pos(3) + rgba(4) = 7 floats. Total con todo visible:
    // anillos 9*48*2 = 864, meridianos 8*30*2 = 480, dipolo 32*64*2 = 4096.
    std::vector<float> verts;
    verts.reserve(5440 * 7);

    auto push = [&](const glm::vec3& p, const glm::vec4& c) {
        verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
        verts.push_back(c.r); verts.push_back(c.g); verts.push_back(c.b); verts.push_back(c.a);
    };

    ringFirst_ = ringCount_ = 0;
    merFirst_ = merCount_ = 0;
    dipFirst_ = dipCount_ = 0;

    // ---- 1. Magnetopausa Shue-1998 (anillos theta constante) ----
    // Terna solar -> mundo: ex hacia el Sol, ey/ez completan la base. El
    // norte (0,1,0) se sustituye por (0,0,1) si es degenerado (Sol en el polo).
    glm::vec3 ex = sun_;
    const float sunLen = glm::length(ex);
    ex = (sunLen > 1e-6f) ? ex / sunLen : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 north(0.0f, 1.0f, 0.0f);
    if (std::fabs(glm::dot(north, ex)) > 0.999f) north = glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 ey = glm::normalize(north - ex * glm::dot(north, ex));
    const glm::vec3 ez = glm::cross(ex, ey);

    // Punto de la magnetopausa: (theta, phi) en grados, marco solar.
    auto solarPoint = [&](float thetaDeg, float phiDeg) {
        const float th = glm::clamp(thetaDeg * kDeg2Rad, 0.0f, kPi - 1e-3f);
        const float ph = phiDeg * kDeg2Rad;
        const float r = r0 * std::pow(2.0f / (1.0f + std::cos(th)), al);
        float x = r * std::cos(th);
        if (x < 0.0f) x *= tail;   // cola v1: estiramiento nocturno declarado
        const float rho = std::sqrt(std::max(r * r - x * x, 0.0f));
        const glm::vec3 p(x, rho * std::cos(ph), rho * std::sin(ph));
        return ex * p.x + ey * p.y + ez * p.z;
    };

    const glm::vec4 ringCol(0.55f, 0.45f, 1.00f, 0.58f);
    ringFirst_ = static_cast<int>(verts.size()) / 7;
    // Recorte declarado: la web llega a theta=135, pero ahi r supera 25 R_E y
    // la malla solo cruza la pantalla como arcos sueltos. Cortamos en 105
    // grados (borde de salida de la cavidad) para que la forma se lea.
    for (int t = 0; t < 7; ++t) {
        const float thetaDeg = 15.0f + 15.0f * static_cast<float>(t);   // 15..105
        glm::vec3 prev = solarPoint(thetaDeg, 0.0f);
        for (int i = 1; i <= 48; ++i) {
            const glm::vec3 cur = solarPoint(thetaDeg, 360.0f * static_cast<float>(i) / 48.0f);
            push(prev, ringCol);
            push(cur, ringCol);
            prev = cur;
        }
    }
    ringCount_ = static_cast<int>(verts.size()) / 7 - ringFirst_;

    // ---- 1b. Meridianos (phi = m*45 grados, theta 5..120) ----
    const glm::vec4 merCol(0.45f, 0.38f, 0.85f, 0.38f);
    merFirst_ = static_cast<int>(verts.size()) / 7;
    for (int m = 0; m < 8; ++m) {
        const float phiDeg = 45.0f * static_cast<float>(m);
        glm::vec3 prev = solarPoint(5.0f, phiDeg);
        for (int i = 1; i <= 30; ++i) {
            const float thetaDeg = 5.0f + 115.0f * static_cast<float>(i) / 30.0f;
            const glm::vec3 cur = solarPoint(thetaDeg, phiDeg);
            push(prev, merCol);
            push(cur, merCol);
            prev = cur;
        }
    }
    merCount_ = static_cast<int>(verts.size()) / 7 - merFirst_;

    // ---- 2. Lineas de campo dipolares (IGRF-14 grado 1, 2025.0) ----
    glm::vec3 u, v, m;
    magneticBasis(u, v, m);
    const float shells[4] = { 1.5f, 2.5f, 4.0f, 6.0f };
    const int NSAMP = 64;
    const glm::vec4 dipCol(0.35f, 0.80f, 0.95f, 0.45f);   // plano a proposito
    dipFirst_ = static_cast<int>(verts.size()) / 7;
    for (int s = 0; s < 4; ++s) {
        const float L = shells[s];
        const float lamMax = std::acos(std::sqrt(1.0f / std::max(L, 1.0f)));
        for (int mi = 0; mi < 8; ++mi) {
            const float ph = 45.0f * static_cast<float>(mi) * kDeg2Rad;
            glm::vec3 prev(0.0f);
            for (int i = 0; i <= NSAMP; ++i) {
                const float lam = -lamMax + (2.0f * lamMax * static_cast<float>(i)) /
                                               static_cast<float>(NSAMP);
                const float r = L * std::cos(lam) * std::cos(lam);
                // q en marco magnetico (z = eje dipolar); |q| = r en R_E, asi
                // que se conserva la escala real (la web normalizaba a esfera
                // unidad y perdia la cascara).
                const glm::vec3 q(r * std::cos(lam) * std::cos(ph),
                                  r * std::cos(lam) * std::sin(ph),
                                  r * std::sin(lam));
                const glm::vec3 geo = q.x * u + q.y * v + q.z * m;
                // Marco web -> motor (-x, y, -z), escala real 1 unidad = 1 R_E.
                const glm::vec3 p = glm::vec3(-geo.x, geo.y, -geo.z) * kEarthRadiusUnits;
                if (i > 0) {
                    push(prev, dipCol);
                    push(p, dipCol);
                }
                prev = p;
            }
        }
    }
    dipCount_ = static_cast<int>(verts.size()) / 7 - dipFirst_;

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    dirty_ = false;
}

void MagnetosphereLayer::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_) return;
    if (dirty_) rebuild();
    if (ringCount_ == 0 && merCount_ == 0 && dipCount_ == 0) return;

    const bool useThick = thickLines_ && thickShader_ != 0;
    const GLuint prog = useThick ? thickShader_ : shader_;
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "u_view"), 1, GL_FALSE,
                       glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(prog, "u_projection"), 1, GL_FALSE,
                       glm::value_ptr(projection));
    if (useThick)
        glUniform1f(glGetUniformLocation(prog, "u_halfWidthNdc"), 0.0025f);

    glBindVertexArray(vao_);
    if (ringCount_ > 0) glDrawArrays(GL_LINES, ringFirst_, ringCount_);
    if (merCount_ > 0) glDrawArrays(GL_LINES, merFirst_, merCount_);
    if (dipCount_ > 0) glDrawArrays(GL_LINES, dipFirst_, dipCount_);
    glBindVertexArray(0);
}

void MagnetosphereLayer::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (shader_) glDeleteProgram(shader_);
    if (thickShader_) glDeleteProgram(thickShader_);
    vao_ = vbo_ = shader_ = thickShader_ = 0;
    ringFirst_ = ringCount_ = 0;
    merFirst_ = merCount_ = 0;
    dipFirst_ = dipCount_ = 0;
    keyValid_ = false;
    dirty_ = true;
}
