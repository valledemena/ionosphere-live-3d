#include "Atmosphere.h"
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void Atmosphere::init() {
    const int stacks = 32, slices = 64;
    const int vertsX = slices + 1;
    std::vector<float> verts;
    std::vector<unsigned int> indices;

    for (int i = 0; i <= stacks; i++) {
        float phi = (float)M_PI * i / stacks;
        for (int j = 0; j <= slices; j++) {
            float theta = 2.0f * (float)M_PI * j / slices;
            float x = sinf(phi) * cosf(theta);
            float y = cosf(phi);
            float z = sinf(phi) * sinf(theta);
            verts.push_back(x);
            verts.push_back(y);
            verts.push_back(z);
        }
    }

    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < slices; j++) {
            int a = i * vertsX + j;
            int b = a + vertsX;
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(a + 1);
            indices.push_back(a + 1);
            indices.push_back(b);
            indices.push_back(b + 1);
        }
    }

    indexCount = (int)indices.size();

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // M6 (plan 9.4): LUT de transmittance hacia el sol, 256x64 GL_RG32F,
    // horneada UNA vez aqui (CPU; hay contexto GL — mismo init que crea el
    // VAO/VBO). Ejes (cruzados con el lookup de shaders/atmosphere.frag):
    //   u = (cos(cenit solar en el punto) + 1)/2   -> 256 columnas
    //   v = altura h / (ATM_R - PLANET_R)          -> 64 filas
    // Texel = (odR, odM) SIN el 1.1 de Mie (la extincion se aplica en el
    // shader tras el lookup — la LUT guarda od puros, como la marcha High).
    // CONSTANTES CRUZADAS con shaders/atmosphere.frag (plan 9.1, unidades
    // R_tierra — no convertir):
    const float PLANET_R = 1.0f;
    const float ATM_R    = 1.0255f;   // = escala 1.0255f del model en App.cpp
    const float H_R      = 8.5e-3f;
    const float H_M      = 1.2e-2f;
    const int   LW = 256, LH = 64, SUN_STEPS = 32;   // bake: CPU, precision barata
    std::vector<float> lut((size_t)LW * LH * 2);
    for (int iy = 0; iy < LH; iy++) {
        float h = (ATM_R - PLANET_R) * ((float)iy + 0.5f) / (float)LH;
        float r = PLANET_R + h;
        for (int ix = 0; ix < LW; ix++) {
            float ct = -1.0f + 2.0f * ((float)ix + 0.5f) / (float)LW;
            float st = sqrtf(fmaxf(1.0f - ct * ct, 0.0f));
            // Geometria canonica: punto sobre el eje Z a radio r, sol en el
            // plano XZ con cenit ct (el od al sol solo depende de ct —
            // simetria azimutal).
            float px = 0.0f, pz = r, sx = st, sz = ct;
            float odR = 0.0f, odM = 0.0f;
            // Sombra dura del planeta (plan 9.1): od ~1e9 -> extincion 0.
            float b = px * sx + pz * sz;
            float cP = px * px + pz * pz - PLANET_R * PLANET_R;
            float hP = b * b - cP;
            if (hP >= 0.0f && (-b - sqrtf(hP)) >= 0.0f) {
                odR = 1.0e9f;
                odM = 1.0e9f;
            } else {
                // salida de la atmosfera hacia el sol (raiz lejana; r <= ATM_R)
                float cA = px * px + pz * pz - ATM_R * ATM_R;
                float hA = b * b - cA;
                float tExit = (hA >= 0.0f) ? (-b + sqrtf(hA)) : 0.0f;
                float ds = tExit / (float)SUN_STEPS;
                for (int j = 0; j < SUN_STEPS; j++) {
                    float t = ((float)j + 0.5f) * ds;
                    float qx = px + sx * t;
                    float qz = pz + sz * t;
                    float hq = fmaxf(sqrtf(qx * qx + qz * qz) - PLANET_R, 0.0f);
                    odR += expf(-hq / H_R) * ds;
                    odM += expf(-hq / H_M) * ds;
                }
            }
            size_t k = ((size_t)iy * LW + (size_t)ix) * 2;
            lut[k] = odR;
            lut[k + 1] = odM;
        }
    }
    glGenTextures(1, &lutTex);
    glBindTexture(GL_TEXTURE_2D, lutTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, LW, LH, 0, GL_RG, GL_FLOAT, lut.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Atmosphere::render() {
    // M6 (P2, plan 9.2): dibujar SOLO el hemisferio TRASERO de la cascara —
    // la parte tras el planeta la mata el depth test (limbo correcto) y la
    // cara visible del globo la pinta earth.frag con aerial perspective
    // (plan 9.3). PATRON save/enable/restore de VolumeRenderer::render (el
    // que sello MUSE para P2 — ninguno inventado). MODO: GL_BACK, no GL_FRONT:
    // el generador de esfera del motor (mismo bucle aqui y en VolumeRenderer)
    // deja la cara EXTERIOR como back-facing — triangulo del ecuador visto
    // desde fuera = CW (verificado numericamente). Por eso VolumeRenderer
    // cull FRONT dibuja su hemisferio CERCANO (el volumen se ve sobre el
    // globo, comportamiento de produccion); aqui, para dibujar el TRASERO,
    // hay que cull BACK.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, lutTex);   // M6: LUT en la unidad 0 (u_lutTex)
    GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
    GLint cullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);    // hemisferio trasero (ver comentario de arriba)
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    if (!wasCull) glDisable(GL_CULL_FACE);
    glCullFace(cullMode);
}
