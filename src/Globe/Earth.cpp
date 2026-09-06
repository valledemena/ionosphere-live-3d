#include "Earth.h"
#include <vector>
#include <cstdio>
#include <cmath>
#include <stb_image.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static GLuint loadTexture(const char* path) {
    int w, h, ch;
    bool loaded = false;
    unsigned char* data = stbi_load(path, &w, &h, &ch, 3);
    if (data) {
        loaded = true;
    } else {
        fprintf(stderr, "Failed to load texture: %s, using fallback\n", path);
        static unsigned char fallback[] = {30, 60, 120};
        data = fallback;
        w = h = 1; ch = 3;
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Anisotropia: nitidez en angulos rasantes (limbo). Si la extension no
    // existe, maxAniso queda en 1.0 y se omite (sin error visible).
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
    GLfloat maxAniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    glGetError();  // limpiar posible GL_INVALID_ENUM si no hay extension
    GLfloat aniso = maxAniso > 16.0f ? 16.0f : maxAniso;
    if (aniso > 1.0f) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    if (loaded) stbi_image_free(data);
    return tex;
}

static bool imageFitsGL(const char* path) {
    // stbi_info lee solo la cabecera (barato, sin decodificar pixeles).
    int w = 0, h = 0, ch = 0;
    if (!stbi_info(path, &w, &h, &ch)) return false;
    GLint maxTex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    printf("[Earth] %s: %dx%d (GL max %d)\n", path, w, h, (int)maxTex);
    return w > 0 && h > 0 && w <= maxTex && h <= maxTex;
}

void Earth::init() {
    const int stacks = 64, slices = 128;
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
            verts.push_back(x);
            verts.push_back(y);
            verts.push_back(z);
            float u = 1.0f - (float)j / slices;
            verts.push_back(u);
            verts.push_back((float)i / stacks);
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

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    texture = loadTexture("textures/earth-day.jpg");
    // Noche HiRes (Black Marble 3km, 13500px) si cabe en la GPU;
    // si no, la estandar (3600px). Sin red ni descargas en runtime.
    if (imageFitsGL("textures/earth-night-hires.jpg"))
        nightTexture = loadTexture("textures/earth-night-hires.jpg");
    else
        nightTexture = loadTexture("textures/earth-night.jpg");
}

void Earth::render() {
    glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, nightTexture);
    glActiveTexture(GL_TEXTURE0);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Earth::destroy() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    if (texture) glDeleteTextures(1, &texture);
    if (nightTexture) glDeleteTextures(1, &nightTexture);
}
