#include "Terminator.h"
#include <vector>
#include <cmath>
#include <glm/glm.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void Terminator::init() {
    // Anillo unitario en el plano XY; update() lo orienta al sol.
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
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void Terminator::update(const glm::vec3& sunDir) {
    if (vao == 0) return;
    glm::vec3 s = glm::normalize(sunDir);
    // Base (u, v) del plano perpendicular al sol.
    glm::vec3 ref = fabsf(s.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 u = glm::normalize(glm::cross(s, ref));
    glm::vec3 v = glm::cross(s, u);
    const int segments = 128;
    std::vector<float> verts;
    verts.reserve((segments + 1) * 3);
    for (int i = 0; i <= segments; i++) {
        float t = 2.0f * (float)M_PI * i / segments;
        glm::vec3 p = (u * cosf(t) + v * sinf(t)) * 1.01f;
        verts.push_back(p.x);
        verts.push_back(p.y);
        verts.push_back(p.z);
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Terminator::render() {
    glBindVertexArray(vao);
    glDrawArrays(GL_LINE_STRIP, 0, vertexCount);
    glBindVertexArray(0);
}
