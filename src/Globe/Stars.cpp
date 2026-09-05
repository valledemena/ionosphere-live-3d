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
    const int N = 1600;
    count = N;

    // Galactic plane normal (tilted for a natural band across the sky).
    const float px = 0.42f, py = 0.88f, pz = -0.22f;
    const float pl = sqrtf(px * px + py * py + pz * pz);
    const float nx = px / pl, ny = py / pl, nz = pz / pl;

    auto rnd01 = []() { return (float)(rand() % 10000) / 10000.0f; };

    int placed = 0;
    while (placed < N) {
        // Random point on unit sphere
        float theta = 2.0f * M_PI * rnd01();
        float phi = acosf(2.0f * rnd01() - 1.0f);
        float x = sinf(phi) * cosf(theta);
        float y = cosf(phi);
        float z = sinf(phi) * sinf(theta);
        // Milky Way band: accept dense near the galactic plane.
        float b = x * nx + y * ny + z * nz;   // sin(galactic latitude)
        float accept = 0.12f + 0.88f * expf(-powf(b / 0.28f, 2.0f));
        if (rnd01() > accept) continue;
        // Magnitude skewed dim + slight band brightening.
        float mag = powf(rnd01(), 1.5f) * 6.5f - 0.4f * expf(-powf(b / 0.2f, 2.0f));
        if (mag < 0.0f) mag = 0.0f;
        // Temperature tint: warm <-> white <-> cool blue.
        float t = rnd01();
        float r, g, bl;
        if (t < 0.5f) { float k = t * 2.0f; r = 1.0f; g = 0.75f + 0.25f * k; bl = 0.55f + 0.45f * k; }
        else { float k = (t - 0.5f) * 2.0f; r = 1.0f - 0.35f * k; g = 1.0f - 0.20f * k; bl = 1.0f; }
        data.push_back(x);
        data.push_back(y);
        data.push_back(z);
        data.push_back(mag);
        data.push_back(r);
        data.push_back(g);
        data.push_back(bl);
        placed++;
    }

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

void Stars::render() {
    glBindVertexArray(vao);
    glDrawArrays(GL_POINTS, 0, count);
    glBindVertexArray(0);
}
