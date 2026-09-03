#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Camera {
    glm::vec3 position = glm::vec3(0, 0, 3);
    glm::vec3 target = glm::vec3(0);
    glm::vec3 up = glm::vec3(0, 1, 0);
    float fov = 45.0f;
    float aspect = 16.0f / 9.0f;
    float nearP = 0.1f;
    float farP = 100.0f;
    float distance = 3.0f;
    float rotateX = 0.0f;
    float rotateY = 0.0f;

    glm::mat4 getViewMatrix() const {
        float cx = distance * sinf(rotateY) * cosf(rotateX);
        float cy = distance * sinf(rotateX);
        float cz = distance * cosf(rotateY) * cosf(rotateX);
        return glm::lookAt(glm::vec3(cx, cy, cz), target, up);
    }
};
