#include "SolarWindRiver.h"
#include "Data/SolarWindRamp.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdio>

// Puntos con color por vertice. El tamano se calcula en el shader con la
// distancia de vista, replicando sizeAttenuation de three.js (world units):
//   gl_PointSize = size_world * (H / (2*tan(fov/2))) / (-viewZ)
static const char* riverVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
uniform mat4 u_view, u_projection;
uniform float u_size, u_pixelScale;
out vec3 vColor;
void main() {
    vec4 vp = u_view * vec4(aPos, 1.0);
    gl_Position = u_projection * vp;
    float d = max(0.05, -vp.z);
    gl_PointSize = clamp(u_size * u_pixelScale / d, 1.0, 48.0);
    vColor = aColor;
}
)";

// Sprite redondo con borde suave: el blend aditivo del llamante multiplica por
// alpha, asi que u_alpha modula el brillo (nucleo 0.9, halo 0.16).
static const char* riverFragSrc = R"(
#version 400 core
in vec3 vColor;
uniform float u_alpha;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, u_alpha);
}
)";

// DW.23 V3a: quads instanciados, mismo VBO pos(3)+color(3). No toca sim_.
static const char* riverInstVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
uniform mat4 u_view, u_projection;
uniform float u_size;
out vec3 vColor;
out vec2 vCorner;
void main() {
    vec2 c = vec2(float((gl_VertexID & 2) >> 1), float(gl_VertexID & 1)) * 2.0 - 1.0;
    vec4 vp = u_view * vec4(aPos, 1.0);
    vp.xy += c * u_size;
    gl_Position = u_projection * vp;
    vCorner = c;
    vColor = aColor;
}
)";

static const char* riverInstFragSrc = R"(
#version 400 core
in vec3 vColor;
in vec2 vCorner;
uniform float u_alpha;
out vec4 fragColor;
void main() {
    float r = length(vCorner);
    float soft = 1.0 - smoothstep(0.55, 1.0, r);
    fragColor = vec4(vColor, u_alpha * soft);
}
)";

// DW.24 V3b: simulacion en GPU. Las posiciones viven en marco SOLAR en dos
// buffers ping-pong; el shader replica stepLength (x60), obstaclePush (Shue)
// y el reciclado. El recycle usa un hash GLSL, NO mulberry32: no se afirma
// identidad bit a bit con la simulacion CPU (declarado en el pre-registro).
static const char* riverTfSimVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
uniform float u_dist, u_r0, u_alpha, u_clearance, u_frame;
out vec3 vPos;
float h1(uint x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return float(x) / 4294967296.0;
}
void main() {
    vec3 p = aPos;
    p.x -= u_dist;
    float r = length(p);
    if (r > 1e-6) {
        float c = clamp(p.x / r, -1.0, 1.0);
        // u_r0/u_alpha vienen de shuemath::r0() y shuemath::alpha() en CPU:
        // NO hay copia de coeficientes Shue en este shader (nota 1 del veredicto).
        float rmp = u_r0 * pow(2.0 / (1.0 + c), u_alpha) * u_clearance;
        if (r < rmp) p.yz *= rmp / r;
    }
    if (p.x < -28.0) {
        uint s = uint(gl_VertexID) * 3u + uint(u_frame) * 7919u;
        float sx = h1(s), sa = h1(s + 1u), sr = h1(s + 2u);
        p.x = 18.0 - sx * 43.0;
        float a = sa * 6.28318530718;
        float rr = sqrt(sr) * 12.0;
        p.y = cos(a) * rr;
        p.z = sin(a) * rr;
    }
    vPos = p;
}
)";

static const char* riverTfSimFragSrc = R"(
#version 400 core
out vec4 fragColor;
void main() { fragColor = vec4(0.0); }
)";

// Render con rotacion a mundo en el vertex shader: el ping-pong se mantiene
// en marco solar para no mezclar marcos entre frames.
static const char* riverTfRenderVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
uniform mat4 u_view, u_projection;
uniform mat3 u_rot;
uniform float u_size, u_pixelScale;
out vec3 vColor;
void main() {
    vec4 vp = u_view * vec4(u_rot * aPos, 1.0);
    gl_Position = u_projection * vp;
    float d = max(0.05, -vp.z);
    gl_PointSize = clamp(u_size * u_pixelScale / d, 1.0, 48.0);
    vColor = aColor;
}
)";

static const char* riverTfInstVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
uniform mat4 u_view, u_projection;
uniform mat3 u_rot;
uniform float u_size;
out vec3 vColor;
out vec2 vCorner;
void main() {
    vec2 c = vec2(float((gl_VertexID & 2) >> 1), float(gl_VertexID & 1)) * 2.0 - 1.0;
    vec4 vp = u_view * vec4(u_rot * aPos, 1.0);
    vp.xy += c * u_size;
    gl_Position = u_projection * vp;
    vCorner = c;
    vColor = aColor;
}
)";

namespace {

// Tamanos del web (size 0.17 / 0.38 R_E) y sus opacidades.
constexpr float kCoreSize = 0.17f;
constexpr float kCoreAlpha = 0.90f;
constexpr float kGlowSize = 0.38f;
constexpr float kGlowAlpha = 0.16f;

float roundKey(float v, float scale) { return std::round(v * scale) / scale; }

}  // namespace

SolarWindRiver::SolarWindRiver() {}
SolarWindRiver::~SolarWindRiver() { destroy(); }

void SolarWindRiver::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &riverVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &riverFragSrc, nullptr);
    glCompileShader(fs);

    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    // Layout: pos(3) + color(3) = 6 floats.
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts_), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // DW.23: VAO instanciado sobre el MISMO vbo_ (divisor 1).
    GLuint ivs = glCreateShader(GL_VERTEX_SHADER);
    GLuint ifs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(ivs, 1, &riverInstVertSrc, nullptr);
    glShaderSource(ifs, 1, &riverInstFragSrc, nullptr);
    glCompileShader(ivs); glCompileShader(ifs);
    instShader_ = glCreateProgram();
    glAttachShader(instShader_, ivs);
    glAttachShader(instShader_, ifs);
    glLinkProgram(instShader_);
    GLint linked = GL_FALSE;
    glGetProgramiv(instShader_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        fprintf(stderr, "[SolarWindRiver] instanced shader link failed; GL_POINTS intacto\n");
        glDeleteProgram(instShader_);
        instShader_ = 0;
    }
    glDeleteShader(ivs); glDeleteShader(ifs);
    if (instShader_) {
        glGenVertexArrays(1, &instVao_);
        glBindVertexArray(instVao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribDivisor(0, 1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribDivisor(1, 1);
        glBindVertexArray(0);
    }

    // DW.24 V3b: recursos TF (no afectan al path OFF).
    initTf();

    // Necesario para que el vertex shader controle el tamano de punto.
    glEnable(GL_PROGRAM_POINT_SIZE);

    colorsDirty_ = true;
    colorKeyValid_ = false;
    refreshRotation();
    fprintf(stderr, "[OK] SolarWindRiver initialized (%d particles)\n", swriver::kCount);
}

void SolarWindRiver::setInstanced(bool v) { instanced_ = v; }

void SolarWindRiver::initTf() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &riverTfSimVertSrc, nullptr);
    glShaderSource(fs, 1, &riverTfSimFragSrc, nullptr);
    glCompileShader(vs); glCompileShader(fs);
    tfProg_ = glCreateProgram();
    glAttachShader(tfProg_, vs);
    glAttachShader(tfProg_, fs);
    const char* varyings[] = {"vPos"};
    glTransformFeedbackVaryings(tfProg_, 1, varyings, GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(tfProg_);
    GLint ok = GL_FALSE;
    glGetProgramiv(tfProg_, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        fprintf(stderr, "[SolarWindRiver] TF sim link failed; path CPU intacto\n");
        glDeleteProgram(tfProg_);
        tfProg_ = 0;
    }
    glDeleteShader(vs); glDeleteShader(fs);

    auto makeProg = [](const char* vsrc, const char* fsrc) -> GLuint {
        GLuint v = glCreateShader(GL_VERTEX_SHADER);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(v, 1, &vsrc, nullptr);
        glShaderSource(f, 1, &fsrc, nullptr);
        glCompileShader(v); glCompileShader(f);
        GLuint p = glCreateProgram();
        glAttachShader(p, v); glAttachShader(p, f);
        glLinkProgram(p);
        GLint k = GL_FALSE;
        glGetProgramiv(p, GL_LINK_STATUS, &k);
        if (k != GL_TRUE) { glDeleteProgram(p); p = 0; }
        glDeleteShader(v); glDeleteShader(f);
        return p;
    };
    tfRenderPoints_ = makeProg(riverTfRenderVertSrc, riverFragSrc);
    tfRenderInst_ = makeProg(riverTfInstVertSrc, riverInstFragSrc);

    glGenBuffers(2, tfPosBuf_);
    glGenBuffers(1, &tfColorBuf_);
    glBindBuffer(GL_ARRAY_BUFFER, tfColorBuf_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * swriver::kCount * 3,
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenVertexArrays(2, tfSimVao_);
    glGenVertexArrays(2, tfRenderVao_);
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, tfPosBuf_[i]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * swriver::kCount * 3,
                     i == 0 ? sim_.positions() : nullptr, GL_DYNAMIC_COPY);
        glBindVertexArray(tfSimVao_[i]);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(tfRenderVao_[i]);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, tfColorBuf_);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(1);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    refreshTfColors();
}

void SolarWindRiver::refreshTfColors() {
    for (int i = 0; i < swriver::kCount; ++i) {
        tfColors_[i * 3 + 0] = verts_[i * 6 + 3];
        tfColors_[i * 3 + 1] = verts_[i * 6 + 4];
        tfColors_[i * 3 + 2] = verts_[i * 6 + 5];
    }
    glBindBuffer(GL_ARRAY_BUFFER, tfColorBuf_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tfColors_), tfColors_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SolarWindRiver::setTransformFeedback(bool v) {
    if (v == tf_) return;
    tf_ = v && tfProg_ != 0 && tfRenderPoints_ != 0 && tfRenderInst_ != 0;
    if (tf_) {
        // Sincroniza el buffer 0 con el estado CPU actual (marco solar).
        glBindBuffer(GL_ARRAY_BUFFER, tfPosBuf_[0]);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float) * swriver::kCount * 3,
                        sim_.positions());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        tfCur_ = 0;
        refreshTfColors();
    }
}

void SolarWindRiver::stepTf(float dt) {
    if (!tfProg_ || dt <= 0.0f || speedKms_ <= 0.0f) return;
    const float dist = swriver::stepLength(dt, speedKms_);   // MISMO x60 declarado
    if (dist <= 0.0f) return;
    const float dp = shuemath::dynamicPressure(densityCm3_, speedKms_);
    // Unica fuente de los coeficientes: ShueMath.h (sin copia en GLSL).
    const float sr0 = shuemath::r0(bzNT_, dp);
    const float sal = shuemath::alpha(bzNT_, dp);
    const int cur = tfCur_, next = 1 - cur;
    glUseProgram(tfProg_);
    glUniform1f(glGetUniformLocation(tfProg_, "u_dist"), dist);
    glUniform1f(glGetUniformLocation(tfProg_, "u_r0"), sr0);
    glUniform1f(glGetUniformLocation(tfProg_, "u_alpha"), sal);
    glUniform1f(glGetUniformLocation(tfProg_, "u_clearance"), swriver::kClearance);
    glUniform1f(glGetUniformLocation(tfProg_, "u_frame"), (float)(++tfFrame_));
    glBindVertexArray(tfSimVao_[cur]);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, tfPosBuf_[next]);
    glEnable(GL_RASTERIZER_DISCARD);
    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS, 0, swriver::kCount);
    glEndTransformFeedback();
    glDisable(GL_RASTERIZER_DISCARD);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    glBindVertexArray(0);
    tfCur_ = next;
}


void SolarWindRiver::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (instVao_) glDeleteVertexArrays(1, &instVao_);
    if (shader_) glDeleteProgram(shader_);
    if (instShader_) glDeleteProgram(instShader_);
    if (tfProg_) glDeleteProgram(tfProg_);
    if (tfRenderPoints_) glDeleteProgram(tfRenderPoints_);
    if (tfRenderInst_) glDeleteProgram(tfRenderInst_);
    glDeleteBuffers(2, tfPosBuf_);
    glDeleteBuffers(1, &tfColorBuf_);
    glDeleteVertexArrays(2, tfSimVao_);
    glDeleteVertexArrays(2, tfRenderVao_);
    tfProg_ = tfRenderPoints_ = tfRenderInst_ = 0;
    tfSimVao_[0] = tfSimVao_[1] = tfPosBuf_[0] = tfPosBuf_[1] = tfColorBuf_ = 0;
    tfRenderVao_[0] = tfRenderVao_[1] = 0;
    tf_ = false; tfCur_ = 0;
    vbo_ = vao_ = instVao_ = shader_ = instShader_ = 0;
    visible_ = false;
    colorsDirty_ = true;
    colorKeyValid_ = false;
}

void SolarWindRiver::setVisible(bool v) {
    if (v == visible_) return;
    visible_ = v;
    // Al encender hay que garantizar colores y posiciones en GPU.
    if (v) colorsDirty_ = true;
}

void SolarWindRiver::setSolarWind(float speedKms, float densityCm3, float bzNT) {
    if (std::isfinite(speedKms) && speedKms > 0.0f) speedKms_ = speedKms;
    if (std::isfinite(densityCm3) && densityCm3 >= 0.0f) densityCm3_ = densityCm3;
    if (std::isfinite(bzNT)) bzNT_ = bzNT;

    // Clave redondeada (0.1 km/s, 0.01 nT, 0.01 cm^-3): el color solo se
    // recalcula cuando el viento cambia de verdad.
    const float ks = roundKey(speedKms_, 10.0f);
    const float kb = roundKey(bzNT_, 100.0f);
    const float kd = roundKey(densityCm3_, 100.0f);
    if (!colorKeyValid_ || ks != keySpeed_ || kb != keyBz_ || kd != keyDensity_) {
        keySpeed_ = ks;
        keyBz_ = kb;
        keyDensity_ = kd;
        colorKeyValid_ = true;
        colorsDirty_ = true;
    }
}

void SolarWindRiver::setSunDirection(const glm::vec3& sunWorld) {
    const float len = glm::length(sunWorld);
    if (!std::isfinite(len) || len < 1e-6f) return;
    const glm::vec3 s = sunWorld / len;
    if (glm::dot(s, sun_) < 0.99995f) {   // ~0.57 grados, umbral de las otras capas
        sun_ = s;
        refreshRotation();
    }
}

// Rotacion que lleva +x (eje solar del sim) a la direccion del Sol viva.
void SolarWindRiver::refreshRotation() {
    const glm::vec3 from(1.0f, 0.0f, 0.0f);
    const float c = glm::clamp(glm::dot(from, sun_), -1.0f, 1.0f);
    if (c > 0.999999f) {
        rot_ = glm::mat3(1.0f);
        return;
    }
    if (c < -0.999999f) {
        // Antiparalelo: media vuelta sobre z (el eje es degenerado).
        rot_ = glm::mat3(glm::rotate(glm::mat4(1.0f), swriver::kPi, glm::vec3(0.0f, 0.0f, 1.0f)));
        return;
    }
    const glm::vec3 axis = glm::normalize(glm::cross(from, sun_));
    rot_ = glm::mat3(glm::rotate(glm::mat4(1.0f), std::acos(c), axis));
}

void SolarWindRiver::refreshColors() {
    for (int i = 0; i < swriver::kCount; ++i) {
        // Desfase aureo por particula, igual que la web: (i*0.618034) mod 1.
        const float seed = std::fmod((float)i * 0.61803398875f, 1.0f);
        const glm::vec3 c = windramp::webColor(speedKms_, bzNT_, densityCm3_, seed);
        verts_[i * 6 + 3] = c.r;
        verts_[i * 6 + 4] = c.g;
        verts_[i * 6 + 5] = c.b;
    }
    colorsDirty_ = false;
}

void SolarWindRiver::update(float dt) {
    if (!visible_ || vao_ == 0) return;
    const float d = glm::clamp(dt, 0.0f, 0.1f);
    if (colorsDirty_) {
        refreshColors();
        if (tfProg_) refreshTfColors();
    }
    if (tf_ && tfProg_) {
        stepTf(d);   // DW.24: simulacion en GPU, sin sim_.step ni upload()
    } else {
        sim_.step(d, speedKms_, densityCm3_, bzNT_);
        upload();    // path OFF: byte-identico al original
    }
}

void SolarWindRiver::upload() {
    const float* p = sim_.positions();
    for (int i = 0; i < swriver::kCount; ++i) {
        const glm::vec3 w = rot_ * glm::vec3(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
        verts_[i * 6 + 0] = w.x;
        verts_[i * 6 + 1] = w.y;
        verts_[i * 6 + 2] = w.z;
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts_), verts_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SolarWindRiver::render(const glm::mat4& view, const glm::mat4& projection, float pixelScale) {
    if (!visible_ || vao_ == 0 || shader_ == 0) return;

    if (tf_ && tfRenderPoints_ && tfRenderInst_ && tfRenderVao_[tfCur_]) {
        // DW.24: misma primitiva/tamaños/alfa que el path CPU, pero leyendo
        // el buffer TF actual y rotando a mundo en el vertex shader.
        const bool inst = instanced_;
        GLuint prog = inst ? tfRenderInst_ : tfRenderPoints_;
        glUseProgram(prog);
        glUniformMatrix4fv(glGetUniformLocation(prog, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(prog, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix3fv(glGetUniformLocation(prog, "u_rot"), 1, GL_FALSE, glm::value_ptr(rot_));
        glBindVertexArray(tfRenderVao_[tfCur_]);
        const GLint sizeLoc = glGetUniformLocation(prog, "u_size");
        const GLint alphaLoc = glGetUniformLocation(prog, "u_alpha");
        if (inst) {
            glUniform1f(sizeLoc, kCoreSize); glUniform1f(alphaLoc, kCoreAlpha);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, swriver::kCount);
            glUniform1f(sizeLoc, kGlowSize); glUniform1f(alphaLoc, kGlowAlpha);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, swriver::kCount);
        } else {
            glUniform1f(glGetUniformLocation(prog, "u_pixelScale"),
                        pixelScale > 0.0f ? pixelScale : 1000.0f);
            glUniform1f(sizeLoc, kCoreSize); glUniform1f(alphaLoc, kCoreAlpha);
            glDrawArrays(GL_POINTS, 0, swriver::kCount);
            glUniform1f(sizeLoc, kGlowSize); glUniform1f(alphaLoc, kGlowAlpha);
            glDrawArrays(GL_POINTS, 0, swriver::kCount);
        }
        glBindVertexArray(0);
        return;
    }

    if (instanced_ && instShader_ != 0 && instVao_ != 0) {
        // DW.23 V3a: mismos tamanos, mismos alfas, mismos colores del VBO.
        glUseProgram(instShader_);
        glUniformMatrix4fv(glGetUniformLocation(instShader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(instShader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glBindVertexArray(instVao_);
        glUniform1f(glGetUniformLocation(instShader_, "u_size"), kCoreSize);
        glUniform1f(glGetUniformLocation(instShader_, "u_alpha"), kCoreAlpha);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, swriver::kCount);
        glUniform1f(glGetUniformLocation(instShader_, "u_size"), kGlowSize);
        glUniform1f(glGetUniformLocation(instShader_, "u_alpha"), kGlowAlpha);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, swriver::kCount);
        glBindVertexArray(0);
        return;
    }

    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(shader_, "u_pixelScale"),
                pixelScale > 0.0f ? pixelScale : 1000.0f);

    const GLint sizeLoc = glGetUniformLocation(shader_, "u_size");
    const GLint alphaLoc = glGetUniformLocation(shader_, "u_alpha");

    glBindVertexArray(vao_);
    // Nucleo (core): puntos pequeños, opacidad alta.
    glUniform1f(sizeLoc, kCoreSize);    // 0.17 R_E (tamaño del web)
    glUniform1f(alphaLoc, kCoreAlpha);  // 0.90
    glDrawArrays(GL_POINTS, 0, swriver::kCount);
    // Halo (glow): puntos más grandes, baja opacidad, mismo color base.
    glUniform1f(sizeLoc, kGlowSize);    // 0.38 R_E
    glUniform1f(alphaLoc, kGlowAlpha);  // 0.16
    glDrawArrays(GL_POINTS, 0, swriver::kCount);
    glBindVertexArray(0);
}
