#include "GeospaceOverlays.h"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

// Shader unico: posicion + color RGBA por vertice. El bloque de overlays de
// App.cpp usa blend aditivo (SRC_ALPHA, ONE), asi que alpha modula el brillo.
static const char* geoVertSrc = R"(
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

static const char* geoFragSrc = R"(
#version 400 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Bandas Dst de la demo web (dst.ts, declaradas):
// >= -30 quiet, -50..-30 unsettled, -100..-50 storm, < -100 intense.
glm::vec3 dstColor(float dstNT) {
    if (dstNT < -100.0f) return glm::vec3(1.00f, 0.25f, 0.20f);
    if (dstNT < -50.0f)  return glm::vec3(1.00f, 0.55f, 0.15f);
    if (dstNT < -30.0f)  return glm::vec3(0.90f, 0.75f, 0.30f);
    return glm::vec3(0.35f, 0.55f, 1.00f);
}

// AE -> color (electrojets.ts): <=100 azul, 100-500 a amarillo,
// 500-1500 a blanco, >=1500 blanco.
glm::vec3 aeColor(float aeNT) {
    const glm::vec3 lo(0.20f, 0.40f, 1.00f);
    const glm::vec3 mid(1.00f, 0.85f, 0.20f);
    const glm::vec3 hi(1.00f, 1.00f, 1.00f);
    if (aeNT <= 100.0f) return lo;
    if (aeNT >= 1500.0f) return hi;
    if (aeNT <= 500.0f) return glm::mix(lo, mid, (aeNT - 100.0f) / 400.0f);
    return glm::mix(mid, hi, (aeNT - 500.0f) / 1000.0f);
}

}  // namespace

GeospaceOverlays::GeospaceOverlays() {}
GeospaceOverlays::~GeospaceOverlays() { destroy(); }

glm::vec3 GeospaceOverlays::latLonToPos(float latDeg, float lonDeg, float r) const {
    // Misma convencion que RefShells/GridLayer (theta = pi - lon).
    float phi = kPi * 0.5f - glm::radians(latDeg);
    float theta = kPi - glm::radians(lonDeg);
    float sn = sinf(phi), cs = cosf(phi);
    return glm::vec3(r * sn * cosf(theta), r * cs, r * sn * sinf(theta));
}

float GeospaceOverlays::dstTrueRadius(float dstNT) {
    return glm::clamp(5.5f + dstNT / 100.0f, 3.5f, 6.0f);
}

float GeospaceOverlays::getDstDrawnRadius() const {
    // Compresion lineal 3.5..6 R_E -> 1.25..1.80 R_E. Se conserva el orden
    // (mas tormenta = anillo mas cerrado) y queda dentro de la camara.
    return 1.25f + (dstTrueRadius(dst_) - 3.5f) * (0.55f / 2.5f);
}

void GeospaceOverlays::setDst(float dstNT) {
    if (!std::isfinite(dstNT)) return;
    if (fabsf(dstNT - dst_) > 0.5f) {
        dst_ = dstNT;
        dirty_ = true;
    }
}

void GeospaceOverlays::setAe(float aeNT) {
    if (!std::isfinite(aeNT)) return;
    if (fabsf(aeNT - ae_) > 0.5f) {
        ae_ = aeNT;
        dirty_ = true;
    }
}

void GeospaceOverlays::setSunDirection(const glm::vec3& sunWorld) {
    float len = glm::length(sunWorld);
    if (len < 1e-6f) return;
    glm::vec3 s = sunWorld / len;
    if (glm::dot(s, sun_) < 0.99995f) {   // ~0.57 grados
        sun_ = s;
        dirty_ = true;
    }
}

void GeospaceOverlays::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &geoVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &geoFragSrc, nullptr);
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
    printf("[OK] GeospaceOverlays initialized\n");
}

void GeospaceOverlays::update() {
    if (dirty_ && vao_ != 0) rebuild();
}

void GeospaceOverlays::rebuild() {
    if (vao_ == 0) return;

    // Layout: pos(3) + rgba(4) = 7 floats.
    std::vector<float> verts;
    verts.reserve(6000 * 7);

    auto push = [&](const glm::vec3& p, const glm::vec4& c) {
        verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
        verts.push_back(c.r); verts.push_back(c.g); verts.push_back(c.b); verts.push_back(c.a);
    };

    dstFirst_ = dstCount_ = 0;
    aeFirst_ = aeCount_ = 0;
    greyFirst_ = greyCount_ = 0;

    // ---- 1. Anillo Dst (circulo ecuatorial) ----
    if (dstVisible_) {
        dstFirst_ = (int)verts.size() / 7;
        const int SEG = 192;
        const glm::vec3 col = dstColor(dst_);
        const float r = getDstDrawnRadius();
        // Tres circulos muy proximos en Y dan grosor sin glLineWidth.
        const float yOff[3] = { -0.018f, 0.0f, 0.018f };
        const float alpha[3] = { 0.35f, 0.95f, 0.35f };
        for (int k = 0; k < 3; k++) {
            for (int i = 0; i < SEG; i++) {
                float a0 = 2.0f * kPi * (float)i / (float)SEG;
                float a1 = 2.0f * kPi * (float)(i + 1) / (float)SEG;
                glm::vec3 p0(r * cosf(a0), yOff[k], r * sinf(a0));
                glm::vec3 p1(r * cosf(a1), yOff[k], r * sinf(a1));
                glm::vec4 c(col, alpha[k]);
                push(p0, c);
                push(p1, c);
            }
        }
        dstCount_ = (int)verts.size() / 7 - dstFirst_;
    }

    // ---- 2. Electrojets (bucles a +/-67 grados, ~110 km) ----
    if (aeVisible_) {
        aeFirst_ = (int)verts.size() / 7;
        const int SEG = 128;
        const glm::vec3 col = aeColor(ae_);
        const float r = 1.0f + 110.0f / 6371.0f;   // ~110 km
        const float lat[2] = { 67.0f, -67.0f };
        for (int h = 0; h < 2; h++) {
            for (int i = 0; i < SEG; i++) {
                float lon0 = -180.0f + 360.0f * (float)i / (float)SEG;
                float lon1 = -180.0f + 360.0f * (float)(i + 1) / (float)SEG;
                glm::vec3 p0 = latLonToPos(lat[h], lon0, r);
                glm::vec3 p1 = latLonToPos(lat[h], lon1, r);
                glm::vec4 c(col, 0.95f);
                push(p0, c);
                push(p1, c);
                // Marcas radiales tipo filamento cada 8 segmentos.
                if ((i % 8) == 0) {
                    glm::vec3 tick = latLonToPos(lat[h], lon0, r + 0.022f);
                    push(p0, glm::vec4(col, 0.45f));
                    push(tick, glm::vec4(col, 0.0f));
                }
            }
        }
        aeCount_ = (int)verts.size() / 7 - aeFirst_;
    }

    // ---- 3. Grey line (cinta +/-7.5 grados alrededor del terminador) ----
    if (greyVisible_) {
        greyFirst_ = (int)verts.size() / 7;
        const int SEG = 144;
        const int BANDS = 4;
        const float hw = glm::radians(7.5f);
        const float r = 1.004f;
        const glm::vec3 base(0.55f, 0.78f, 1.0f);

        glm::vec3 s = sun_;
        glm::vec3 helper = (fabsf(s.y) < 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 u = glm::normalize(glm::cross(helper, s));
        glm::vec3 v = glm::cross(s, u);

        auto pointAt = [&](float theta, float off) {
            glm::vec3 c = cosf(theta) * u + sinf(theta) * v;
            return glm::normalize(cosf(off) * c + sinf(off) * s) * r;
        };
        auto alphaAt = [&](int b) {
            // Perfil suave: casi nulo en los bordes, maximo en el centro.
            float t = (float)b / (float)BANDS;
            return 0.10f + 0.22f * sinf(kPi * t);
        };

        for (int i = 0; i < SEG; i++) {
            float t0 = 2.0f * kPi * (float)i / (float)SEG;
            float t1 = 2.0f * kPi * (float)(i + 1) / (float)SEG;
            for (int b = 0; b < BANDS; b++) {
                float o0 = -hw + 2.0f * hw * (float)b / (float)BANDS;
                float o1 = -hw + 2.0f * hw * (float)(b + 1) / (float)BANDS;
                glm::vec4 c0(base, alphaAt(b));
                glm::vec4 c1(base, alphaAt(b + 1));
                glm::vec3 a = pointAt(t0, o0);
                glm::vec3 bb = pointAt(t0, o1);
                glm::vec3 cc = pointAt(t1, o0);
                glm::vec3 dd = pointAt(t1, o1);
                push(a, c0); push(bb, c1); push(cc, c0);
                push(cc, c0); push(bb, c1); push(dd, c1);
            }
        }
        greyCount_ = (int)verts.size() / 7 - greyFirst_;
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    dirty_ = false;
}

void GeospaceOverlays::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!hasVisible()) return;
    if (dirty_) rebuild();
    if (dstCount_ == 0 && aeCount_ == 0 && greyCount_ == 0) return;

    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));

    glBindVertexArray(vao_);
    if (greyCount_ > 0) glDrawArrays(GL_TRIANGLES, greyFirst_, greyCount_);
    if (dstCount_ > 0) glDrawArrays(GL_LINES, dstFirst_, dstCount_);
    if (aeCount_ > 0) glDrawArrays(GL_LINES, aeFirst_, aeCount_);
    glBindVertexArray(0);
}

void GeospaceOverlays::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (shader_) glDeleteProgram(shader_);
    vbo_ = vao_ = shader_ = 0;
    dstFirst_ = dstCount_ = 0;
    aeFirst_ = aeCount_ = 0;
    greyFirst_ = greyCount_ = 0;
}
