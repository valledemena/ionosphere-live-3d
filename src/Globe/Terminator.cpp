#include "Terminator.h"
#include <vector>
#include <cmath>
#include <glm/glm.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void Terminator::init() {
    // Generate a circle that will be rotated to match the terminator plane
    const int segments = 128;
    std::vector<float> verts;
    for (int i = 0; i <= segments; i++) {
        float t = 2.0f * (float)M_PI * i / segments;
        verts.push_back(cosf(t) * 1.01f);
        verts.push_back(sinf(t) * 1.01f);
        verts.push_back(0.0f);
    }
    vertexCount = segments + 1;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void Terminator::render() {
    glBindVertexArray(vao);
    glDrawArrays(GL_LINE_STRIP, 0, vertexCount);
    glBindVertexArray(0);
}
