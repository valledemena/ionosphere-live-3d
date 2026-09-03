#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

class App {
public:
    App();
    ~App();
    bool init(int width, int height, const char* title);
    void run();
    void shutdown();

private:
    void processInput();
    void update(float dt);
    void render();

    GLFWwindow* window = nullptr;
    int winW = 1280, winH = 720;
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    double mouseX = 0.0, mouseY = 0.0;
    bool firstMouse = true;
    float lastMouseX = 640.0f, lastMouseY = 360.0f;

    // Forward declarations via pimpl-like
    struct Impl;
    Impl* impl = nullptr;
};
