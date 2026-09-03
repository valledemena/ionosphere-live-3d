#include "Stars.h"
#include <vector>
#include <cstdlib>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void Stars::init() {
    srand(42);
    std::vector<float> data;
    const int N = 500;
    count = N;

    for (int i = 0; i < N; i++) {
        // Random point on unit sphere
        float theta = 2.0f * M_PI * (rand() % 10000) / 10000.0f;
        float phi = acosf(2.0f * (rand() % 10000) / 10000.0f - 1.0f);
        float x = sinf(phi) * cosf(theta);
        float y = cosf(phi);
        float z = sinf(phi) * sinf(theta);
        float mag = (float)(rand() % 600) / 100.0f; // 0.0 to 6.0
        data.push_back(x);
        data.push_back(y);
        data.push_back(z);
        data.push_back(mag);
    }

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void Stars::render() {
    glBindVertexArray(vao);
    glDrawArrays(GL_POINTS, 0, count);
    glBindVertexArray(0);
}
