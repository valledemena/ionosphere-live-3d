#include "VolumeRenderer.h"
#include "Utils/Colormap.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>

// NOTA DE CONVENCION (critica, ver docs/volumetric-density-spec.md):
// el motor usa theta = pi - lonRad (NO atan2(z,x) a secas).
// lon = pi - atan2(n.z, n.x) plegado a [-pi, pi]; t = 0.5 - lat/pi (norte).
static const char* volVertSrc = R"(
#version 400 core
layout(location=0) in vec3 aPos;
uniform mat4 u_model, u_view, u_projection;
out vec3 vWorldPos;
void main() {
    vWorldPos = vec3(u_model * vec4(aPos, 1.0));
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
}
)";

static const char* volFragSrc = R"(
#version 400 core
in vec3 vWorldPos;
uniform sampler3D u_density;    // log10(Ne) normalizado 0..1 (filtrado)
uniform sampler3D u_layer;      // rama ganadora 0..3 (SIN filtrar: texelFetch)
uniform sampler1D u_colormap;
uniform vec3 u_camPos;
uniform vec3 u_sunDir;            // S-2: direccion solar (sombra dura, ruling 130 sec.6.2)
uniform float u_innerR, u_outerR;
uniform float u_opacityScale;
uniform int u_steps;
uniform int u_colorMode;        // 0 = densidad, 1 = capas fijas
uniform ivec3 u_volRes;         // dims para texelFetch
uniform int u_isoOn;            // realce de isosuperficies logNe 10.5/11/11.5
uniform float u_isoWidth;
out vec4 fragColor;

bool raySphere(vec3 ro, vec3 rd, float r, out float t0, out float t1) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r * r;
    float disc = b * b - c;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    t0 = -b - s; t1 = -b + s;
    return true;
}

void main() {
    vec3 ro = u_camPos;
    vec3 rd = normalize(vWorldPos - u_camPos);

    float tOutA, tOutB;
    if (!raySphere(ro, rd, u_outerR, tOutA, tOutB)) discard;
    float tEnter = max(tOutA, 0.0);
    float tExit = tOutB;

    float tInA, tInB;
    if (raySphere(ro, rd, u_innerR, tInA, tInB) && tInA > tEnter) {
        tExit = min(tExit, tInA);   // no ver a traves del planeta
    }
    if (tExit <= tEnter) discard;

    // Oclusion terrestre analitica (radio 1.0): permite dibujar este pase
    // DESPUES del composite sin depth buffer compartido (modo categorico).
    if (u_colorMode == 1) {
        float tEA, tEB;
        if (raySphere(ro, rd, 1.0, tEA, tEB) && tEA > tEnter) {
            tExit = min(tExit, tEA);
        }
        if (tExit <= tEnter) discard;
    }

    float dt = (tExit - tEnter) / float(u_steps);
    // S-3: jitter del punto de entrada (ruling 130 sec.6.3). IGN espacial
    // determinista (sin componente temporal: el motor no tiene TAA).
    // Decorrela el peine de muestreo entre pixeles vecinos (anti-banding);
    // espejo CPU en Utils/VolumeJitter.h (misma formula).
    float jit = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float tEnterJ = tEnter + dt * jit;
    vec3 accumColor = vec3(0.0);
    float accumAlpha = 0.0;

    for (int i = 0; i < 256; i++) {
        if (i >= u_steps) break;
        float t = tEnterJ + dt * (float(i) + 0.5);
        if (t >= tExit) break;

        vec3 p = ro + rd * t;
        // S-2: sombra dura del planeta (ruling 130 sec.6.2). Multiplicativa:
        // el dimming cosChi de CPU se conserva y se combina. Dura, sin
        // penumbra; espejo CPU en Utils/VolumeShadow.h (misma matematica).
        float sh0, sh1;
        float sunShadow = 1.0;
        if (raySphere(p, u_sunDir, u_innerR, sh0, sh1) && sh0 > 1e-4) sunShadow = 0.0;
        float r = length(p);
        float altNorm = clamp((r - u_innerR) / (u_outerR - u_innerR), 0.0, 1.0);

        vec3 n = p / max(r, 1e-6);
        float lat = asin(clamp(n.y, -1.0, 1.0));
        // t=0 es el SUR (fila 0 del upload = latIdx 0 = lat -90):
        // t = lat/pi + 0.5. (NO 0.5 - lat/pi: eso lee el hemisferio especular.)
        float lon = 3.14159265 - atan(n.z, n.x);
        if (lon > 3.14159265) lon -= 2.0 * 3.14159265;
        if (lon < -3.14159265) lon += 2.0 * 3.14159265;
        vec2 uv = vec2(lon / (2.0 * 3.14159265) + 0.5, lat / 3.14159265 + 0.5);

        float d = texture(u_density, vec3(uv, altNorm)).r;
        vec3 col;
        if (u_colorMode == 1) {
            // Categórico: ID exacto por texelFetch (el LINEAR corrompería
            // fronteras: 1.7 no es ninguna capa). D=naranja, E=verde,
            // F1=amarillo, F2=cian.
            ivec3 ip = ivec3(clamp(vec3(floor(uv.x * float(u_volRes.x)),
                                           floor(uv.y * float(u_volRes.y)),
                                           floor(altNorm * float(u_volRes.z))),
                                   vec3(0.0), vec3(u_volRes) - 1));
            int w = int(texelFetch(u_layer, ip, 0).r * 255.0 + 0.5);
            if (w == 0)      col = vec3(1.00, 0.55, 0.10);
            else if (w == 1) col = vec3(0.20, 0.85, 0.30);
            else if (w == 2) col = vec3(1.00, 0.90, 0.20);
            else             col = vec3(0.20, 0.80, 1.00);
        } else {
            col = texture(u_colormap, d).rgb;
        }
        col *= sunShadow;
        float a = clamp(d * d * u_opacityScale * dt * 40.0, 0.0, 1.0) * sunShadow;
        if (u_isoOn != 0) {
            // Iso-bandas logNe 10.5/11/11.5 (normalizado 0.556/0.667/0.778).
            float m = min(min(abs(d - 0.556), abs(d - 0.667)), abs(d - 0.778));
            float band = 1.0 - smoothstep(0.0, u_isoWidth, m);
            col = mix(col, vec3(1.0), band * 0.55);
            a = max(a, min(band * 0.35, u_opacityScale));
        }

        accumColor += (1.0 - accumAlpha) * a * col;
        accumAlpha += (1.0 - accumAlpha) * a;
        if (accumAlpha > 0.98) break;
    }
    if (accumAlpha <= 0.001) discard;
    fragColor = vec4(accumColor, accumAlpha);
}
)";

VolumeRenderer::VolumeRenderer() {}
VolumeRenderer::~VolumeRenderer() { destroy(); }

void VolumeRenderer::init() {
    glGenTextures(1, &tex3D_);
    glBindTexture(GL_TEXTURE_3D, tex3D_);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &colormapTex_);
    setColormap("viridis");

    glGenTextures(1, &layerTex_);
    glBindTexture(GL_TEXTURE_3D, layerTex_);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &volVertSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &volFragSrc, nullptr);
    glCompileShader(fs);
    shader_ = glCreateProgram();
    glAttachShader(shader_, vs);
    glAttachShader(shader_, fs);
    glLinkProgram(shader_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    // Esfera exterior unidad; el radio real va en u_model (outerR).
    const int slices = 64, stacks = 32;
    std::vector<float> verts;
    std::vector<unsigned int> idx;
    for (int st = 0; st <= stacks; st++) {
        float phi = 3.14159265f * st / stacks;
        for (int sl = 0; sl <= slices; sl++) {
            float th = 2.0f * 3.14159265f * sl / slices;
            verts.push_back(sinf(phi) * cosf(th));
            verts.push_back(cosf(phi));
            verts.push_back(sinf(phi) * sinf(th));
        }
    }
    for (int st = 0; st < stacks; st++) {
        for (int sl = 0; sl < slices; sl++) {
            unsigned a = st * (slices + 1) + sl, b = a + slices + 1;
            idx.push_back(a); idx.push_back(b); idx.push_back(a + 1);
            idx.push_back(b); idx.push_back(b + 1); idx.push_back(a + 1);
        }
    }
    indexCount_ = (int)idx.size();

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    printf("[OK] VolumeRenderer initialized\n");
}

void VolumeRenderer::setColormap(const std::string& name) {
    colormapName_ = name;
    float pal[256 * 3];
    colormap::buildPalette(name, 256, pal);
    // Subir como RGB float (el shader muestrea .rgb).
    std::vector<unsigned char> bytes(256 * 3);
    for (int i = 0; i < 256 * 3; i++)
        bytes[i] = (unsigned char)(glm::clamp(pal[i], 0.0f, 1.0f) * 255.0f);
    if (colormapTex_ != 0) {
        glBindTexture(GL_TEXTURE_1D, colormapTex_);
        glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, bytes.data());
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    }
}

void VolumeRenderer::updateVolume(const DensityVolume& vol) {
    if (!vol.valid || vol.data.empty()) return;
    lonRes_ = vol.lonRes; latRes_ = vol.latRes; altRes_ = vol.altRes;
    altMinKm_ = vol.altMinKm; altMaxKm_ = vol.altMaxKm;
    std::vector<unsigned char> bytes(vol.data.size());
    for (size_t i = 0; i < vol.data.size(); i++)
        bytes[i] = (unsigned char)(glm::clamp(vol.data[i], 0.0f, 1.0f) * 255.0f);
    glBindTexture(GL_TEXTURE_3D, tex3D_);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, lonRes_, latRes_, altRes_, 0,
                 GL_RED, GL_UNSIGNED_BYTE, bytes.data());
    if (vol.layer.size() == vol.data.size()) {
        glBindTexture(GL_TEXTURE_3D, layerTex_);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, lonRes_, latRes_, altRes_, 0,
                     GL_RED, GL_UNSIGNED_BYTE, vol.layer.data());
    }
    hasVolume_ = true;
}

void VolumeRenderer::render(const glm::mat4& view, const glm::mat4& projection,
                            const glm::vec3& camPosWorld) {
    if (!visible_ || !hasVolume_) return;

    float outerR = 1.0f + (altMaxKm_ / 6371.0f) * explode_;
    float innerR = 1.0f + (altMinKm_ / 6371.0f) * explode_;
    glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(outerR));

    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_model"), 1, GL_FALSE, &model[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_view"), 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "u_projection"), 1, GL_FALSE, &projection[0][0]);
    glUniform3f(glGetUniformLocation(shader_, "u_camPos"),
                camPosWorld.x, camPosWorld.y, camPosWorld.z);
    glUniform3f(glGetUniformLocation(shader_, "u_sunDir"),
                sunDir_.x, sunDir_.y, sunDir_.z);
    glUniform1f(glGetUniformLocation(shader_, "u_innerR"), innerR);
    glUniform1f(glGetUniformLocation(shader_, "u_outerR"), outerR);
    glUniform1f(glGetUniformLocation(shader_, "u_opacityScale"), opacityScale_);
    glUniform1i(glGetUniformLocation(shader_, "u_steps"), steps_);
    glUniform1i(glGetUniformLocation(shader_, "u_colorMode"), mode_);
    glUniform1i(glGetUniformLocation(shader_, "u_isoOn"), isoOn_ ? 1 : 0);
    glUniform1f(glGetUniformLocation(shader_, "u_isoWidth"), 0.02f);
    glUniform3i(glGetUniformLocation(shader_, "u_volRes"), lonRes_, latRes_, altRes_);
    glUniform1i(glGetUniformLocation(shader_, "u_density"), 0);
    glUniform1i(glGetUniformLocation(shader_, "u_colormap"), 1);
    glUniform1i(glGetUniformLocation(shader_, "u_layer"), 2);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, tex3D_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, colormapTex_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, layerTex_);
    glActiveTexture(GL_TEXTURE0);

    GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
    GLint cullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);   // entrar por la cara interior
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    if (!wasCull) glDisable(GL_CULL_FACE);
    glCullFace(cullMode);
}

void VolumeRenderer::destroy() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (tex3D_) glDeleteTextures(1, &tex3D_);
    if (layerTex_) glDeleteTextures(1, &layerTex_);
    if (colormapTex_) glDeleteTextures(1, &colormapTex_);
    if (shader_) glDeleteProgram(shader_);
    vao_ = vbo_ = ebo_ = tex3D_ = layerTex_ = colormapTex_ = shader_ = 0;
    indexCount_ = 0;
    hasVolume_ = false;
}
