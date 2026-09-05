#include "IonosondeLayer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdio>
#include <algorithm>

static const char* pointVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
uniform mat4 u_view, u_projection;
uniform float u_pointSize;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = u_projection * u_view * vec4(aPos, 1.0);
    gl_PointSize = u_pointSize;
}
)";

static const char* pointFragSrc = R"(
#version 400 core
in vec3 vColor;
out vec4 fragColor;
void main() {
    // Punto circular con borde suavizado
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float d = length(c);
    float alpha = 1.0 - smoothstep(0.5, 0.62, d);
    if (alpha <= 0.003) discard;
    fragColor = vec4(vColor, alpha);
}
)";

IonosondeLayer::IonosondeLayer() {}
IonosondeLayer::~IonosondeLayer() { destroy(); }

static void buildPalette(const char* name, size_t n, float* out) {
    struct Stop { float pos, r, g, b; };
    const Stop* stops = nullptr;
    int nStops = 0;

    static const Stop vir[] = {
        {0.00f, 0.267f, 0.004f, 0.329f}, {0.13f, 0.282f, 0.140f, 0.458f},
        {0.25f, 0.253f, 0.265f, 0.530f}, {0.38f, 0.191f, 0.407f, 0.557f},
        {0.50f, 0.127f, 0.566f, 0.551f}, {0.63f, 0.206f, 0.718f, 0.473f},
        {0.75f, 0.478f, 0.824f, 0.318f}, {0.88f, 0.741f, 0.873f, 0.150f},
        {1.00f, 0.993f, 0.906f, 0.144f},
    };
    static const Stop pls[] = {
        {0.00f, 0.052f, 0.020f, 0.519f}, {0.13f, 0.251f, 0.009f, 0.678f},
        {0.25f, 0.452f, 0.008f, 0.742f}, {0.38f, 0.617f, 0.108f, 0.628f},
        {0.50f, 0.740f, 0.213f, 0.440f}, {0.63f, 0.831f, 0.315f, 0.309f},
        {0.75f, 0.871f, 0.462f, 0.171f}, {0.88f, 0.875f, 0.654f, 0.044f},
        {1.00f, 0.940f, 0.975f, 0.131f},
    };
    static const Stop inf[] = {
        {0.00f, 0.000f, 0.000f, 0.040f}, {0.13f, 0.108f, 0.031f, 0.310f},
        {0.25f, 0.358f, 0.045f, 0.451f}, {0.38f, 0.595f, 0.083f, 0.405f},
        {0.50f, 0.786f, 0.153f, 0.203f}, {0.63f, 0.932f, 0.318f, 0.131f},
        {0.75f, 0.984f, 0.500f, 0.147f}, {0.88f, 0.988f, 0.711f, 0.300f},
        {1.00f, 0.988f, 0.998f, 0.645f},
    };
    static const Stop trb[] = {
        {0.00f, 0.190f, 0.071f, 0.232f}, {0.13f, 0.214f, 0.357f, 0.770f},
        {0.25f, 0.165f, 0.604f, 0.843f}, {0.38f, 0.196f, 0.784f, 0.582f},
        {0.50f, 0.450f, 0.866f, 0.314f}, {0.63f, 0.762f, 0.784f, 0.042f},
        {0.75f, 0.954f, 0.555f, 0.036f}, {0.88f, 0.966f, 0.256f, 0.052f},
        {1.00f, 0.648f, 0.033f, 0.112f},
    };

    std::string nm = name;
    if (nm == "inferno") { stops = inf; nStops = sizeof(inf)/sizeof(inf[0]); }
    else if (nm == "plasma") { stops = pls; nStops = sizeof(pls)/sizeof(pls[0]); }
    else if (nm == "turbo") { stops = trb; nStops = sizeof(trb)/sizeof(trb[0]); }
    else { stops = vir; nStops = sizeof(vir)/sizeof(vir[0]); }

    for (size_t i = 0; i < n; i++) {
        float t = (float)i / (float)(n - 1);
        int s = 0;
        for (int j = 0; j < nStops - 1; j++)
            if (t >= stops[j].pos && t <= stops[j + 1].pos) { s = j; break; }
        float f = (t - stops[s].pos) / (stops[s + 1].pos - stops[s].pos + 1e-6f);
        out[i * 3 + 0] = stops[s].r + f * (stops[s + 1].r - stops[s].r);
        out[i * 3 + 1] = stops[s].g + f * (stops[s + 1].g - stops[s].g);
        out[i * 3 + 2] = stops[s].b + f * (stops[s + 1].b - stops[s].b);
    }
}

void IonosondeLayer::init() {
    colormap_.resize(256 * 3);
    buildColormap();

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &pointVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &pointFragSrc, nullptr);
    glCompileShader(fs);

    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    printf("[OK] IonosondeLayer initialized\n");
}

float IonosondeLayer::magnitudeOf(const StationPoint& s) const {
    switch (mode_) {
        case 1: return s.muf;
        case 2: return s.hmF2;
        case 3: return s.nmF2;
        default: return s.foF2;
    }
}

const std::string& IonosondeLayer::getModeName() const {
    static const std::string names[] = { "foF2 [MHz]", "MUF(D) [MHz]", "hmF2 [km]", "NmF2 [m^-3]" };
    return names[mode_];
}

// Construye el colormap de los puntos: el de la metric seleccionada en modo auto
// (foF2=plasma, MUF=inferno, hmF2/NmF2=turbo) o el override manual si esta activo.
void IonosondeLayer::buildColormap() {
    const char* name = "viridis";
    if (pointColormapOverride_) {
        name = pointColormapOverrideName_.c_str();
    } else {
        switch (mode_) {
            case 1: name = "inferno"; break;
            case 2: name = "turbo";   break;
            case 3: name = "turbo";   break;
            default: name = "plasma"; break;   // foF2
        }
    }
    buildPalette(name, 256, colormap_.data());
}

void IonosondeLayer::getActiveRange(float& minV, float& maxV) const {
    minV = 1e9f; maxV = -1e9f;
    int nValid = 0;
    for (const auto& s : samples_) {
        if (!s.valid) continue;
        float v = magnitudeOf(s);
        if (v <= 0.0f) continue;
        minV = fminf(minV, v);
        maxV = fmaxf(maxV, v);
        nValid++;
    }
    if (nValid == 0) { minV = 0.0f; maxV = 1.0f; }
}

void IonosondeLayer::updateSamples(const std::vector<StationPoint>& samples) {
    samples_ = samples;
    rebuildColors_ = true;
    rebuild();
}

void IonosondeLayer::rebuild() {
    if (vao_ == 0) return;

    buildColormap();   // respeta modo/override antes de asignar colores

    std::vector<float> verts;
    float minV, maxV;
    getActiveRange(minV, maxV);
    float range = maxV - minV + 1e-6f;

    float r = 1.0f + heightOffset_;
    for (const auto& s : samples_) {
        if (!s.valid) continue;
        // Conversion lat/lon -> posicion 3D sobre la esfera, alineada con el globo.
        // phi = (90 - lat) en rad: 0 en el polo norte (y=+r), pi en el sur.
        // theta = pi - lon en rad (desfase de 180° respecto a lon=0 para coincidir
        // con la longitud de la proyeccion de la malla de la Tierra/TEC).
        float phi = 3.14159265f * 0.5f - glm::radians(s.latDeg);
        float theta = 3.14159265f - glm::radians(s.lonDeg);
        float sn = sinf(phi), cs = cosf(phi);
        float x = r * sn * cosf(theta);
        float y = r * cs;
        float z = r * sn * sinf(theta);

        float val = magnitudeOf(s);
        float t = (val - minV) / range;
        t = glm::clamp(t, 0.0f, 1.0f);
        int ci = (int)(t * 255.0f);
        float cr = colormap_[ci * 3 + 0];
        float cg = colormap_[ci * 3 + 1];
        float cb = colormap_[ci * 3 + 2];

        verts.push_back(x); verts.push_back(y); verts.push_back(z);
        verts.push_back(cr); verts.push_back(cg); verts.push_back(cb);
    }
    pointCount_ = (int)verts.size() / 6;

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    rebuildColors_ = false;
}

void IonosondeLayer::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_ || pointCount_ == 0) return;
    if (rebuildColors_) { rebuild(); }

    glEnable(GL_PROGRAM_POINT_SIZE);   // permite gl_PointSize desde el vertex shader
    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, &projection[0][0]);
    glUniform1f(glGetUniformLocation(shader_, "u_pointSize"), pointSize_);

    glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, pointCount_);
    glBindVertexArray(0);
    glDisable(GL_PROGRAM_POINT_SIZE);
}

int IonosondeLayer::hitTest(float ndcX, float ndcY, const glm::mat4& view,
                            const glm::mat4& projection, float radius) const {
    // Rayo desde la camara a traves del pixel (NDC) en espacio de mundo.
    glm::mat4 invVP = glm::inverse(projection * view);
    glm::vec4 nearP = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farP  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    glm::vec3 n = glm::vec3(nearP) / nearP.w;
    glm::vec3 f = glm::vec3(farP)  / farP.w;
    glm::vec3 dir = glm::normalize(f - n);

    // Interseccion rayo-esfera unitaria (el globo ~ radio 1).
    float b = glm::dot(n, dir);
    float cc = glm::dot(n, n) - 1.0f;
    float disc = b * b - cc;
    if (disc < 0.0f) return -1;
    float tHit = -b - sqrtf(disc);
    if (tHit < 0.0f) return -1;
    glm::vec3 hit = n + dir * tHit;   // punto sobre la esfera unitaria

    float hitLat = 90.0f - glm::degrees(acosf(glm::clamp(hit.y, -1.0f, 1.0f)));
    // Inversa de la conversion de render (theta = pi - lon) => lon = pi - atan2(z,x).
    float hitLon = glm::degrees(3.14159265f - atan2f(hit.z, hit.x));

    // Buscar la estacion valida angularmente mas cercana.
    float r = 1.0f + heightOffset_;
    int best = -1;
    float bestAng = radius;   // umbral en radianes
    for (int i = 0; i < (int)samples_.size(); i++) {
        const auto& s = samples_[i];
        if (!s.valid) continue;
        float dlat = glm::radians(s.latDeg - hitLat);
        float dlon = fmodf(s.lonDeg - hitLon, 360.0f);
        if (dlon > 180.0f) dlon -= 360.0f;
        if (dlon < -180.0f) dlon += 360.0f;
        dlon = glm::radians(dlon);
        float a = sinf(dlat * 0.5f) * sinf(dlat * 0.5f) +
                  cosf(glm::radians(s.latDeg)) * cosf(glm::radians(hitLat)) *
                  sinf(dlon * 0.5f) * sinf(dlon * 0.5f);
        float ang = 2.0f * asinf(sqrtf(a));
        if (ang < bestAng) { bestAng = ang; best = i; }
    }
    (void)r;
    return best;
}

const StationPoint* IonosondeLayer::getSample(int index) const {
    if (index < 0 || index >= (int)samples_.size()) return nullptr;
    return &samples_[index];
}

void IonosondeLayer::destroy() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (shader_) glDeleteProgram(shader_);
    vao_ = vbo_ = shader_ = 0;
}
