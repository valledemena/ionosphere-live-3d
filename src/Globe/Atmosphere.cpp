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
}

void Atmosphere::render() {
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
