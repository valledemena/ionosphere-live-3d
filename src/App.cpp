#include "App.h"

#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

#include "Render/Shader.h"
#include "Render/Camera.h"
#include "Render/Framebuffer.h"
#include "Globe/Earth.h"
#include "Globe/Stars.h"
#include "Globe/Atmosphere.h"
#include "Globe/Terminator.h"
#include "Ionosphere/TecLayer.h"
#include "Data/DataManager.h"
#include "Data/EsaAdapter.h"
#include "Utils/SolarPosition.h"
#include "Utils/CoordinateSystem.h"

struct App::Impl {
    Camera camera;
    Shader earthShader;
    Shader starShader;
    Shader atmosphereShader;
    Shader bloomShader;
    Shader compositeShader;
    Shader terminatorShader;
    Earth earth;
    Stars stars;
    Atmosphere atmosphere;
    Terminator terminator;
    TecLayer tecLayer;
    DataManager dataManager;
    EsaAdapter esaAdapter;
    Framebuffer sceneFBO;
    Framebuffer bloomFBO[2];

    Shader tecShader;

    GLuint quadVAO = 0;
    GLuint quadVBO = 0;

    float time = 0.0f;
    bool bloomEnabled = true;

    glm::vec3 sunDirection = glm::vec3(1.0f, 0.3f, 0.5f);

    void updateSunDirection() {
        double utc = glfwGetTime();
        sunDirection = SolarPosition::getDirection(utc);
    }
};

static void framebufferSizeCallback(GLFWwindow* w, int width, int height) {
    glViewport(0, 0, width, height);
}

static void mouseCallback(GLFWwindow* w, double xpos, double ypos) {
    // handled in processInput
}

App::App() : impl(new Impl) {}
App::~App() { delete impl; }

bool App::init(int width, int height, const char* title) {
    winW = width;
    winH = height;

    if (!glfwInit()) {
        fprintf(stderr, "GLFW init failed\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    window = glfwCreateWindow(winW, winH, title, nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Window creation failed\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "GLAD init failed\n");
        return false;
    }

    printf("[OK] OpenGL %s\n", glGetString(GL_VERSION));
    printf("[OK] Renderer: %s\n", glGetString(GL_RENDERER));

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 400");

    // Camera
    impl->camera.position = glm::vec3(0.0f, 0.0f, 3.0f);
    impl->camera.target = glm::vec3(0.0f);
    impl->camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    impl->camera.fov = 45.0f;
    impl->camera.aspect = (float)winW / winH;
    impl->camera.nearP = 0.1f;
    impl->camera.farP = 100.0f;
    impl->camera.distance = 3.0f;
    impl->camera.rotateX = 0.0f;
    impl->camera.rotateY = 0.0f;

    // Shaders
    if (!impl->earthShader.load("shaders/earth.vert", "shaders/earth.frag")) {
        fprintf(stderr, "Earth shader failed, using fallback\n");
        impl->earthShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec3 aPos;\n"
            "layout(location=1) in vec3 aNormal;\n"
            "layout(location=2) in vec2 aTexCoord;\n"
            "uniform mat4 u_model, u_view, u_projection;\n"
            "out vec3 vNormal; out vec2 vTexCoord; out vec3 vFragPos;\n"
            "void main(){\n"
            "  vNormal = mat3(u_model) * aNormal;\n"
            "  vTexCoord = aTexCoord;\n"
            "  vFragPos = vec3(u_model * vec4(aPos, 1.0));\n"
            "  gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);\n"
            "}\n",
            "#version 400 core\n"
            "in vec3 vNormal; in vec2 vTexCoord; in vec3 vFragPos;\n"
            "uniform sampler2D u_texture;\n"
            "uniform vec3 u_sunDir;\n"
            "out vec4 fragColor;\n"
            "void main(){\n"
            "  vec3 n = normalize(vNormal);\n"
            "  float diff = max(dot(n, normalize(u_sunDir)), 0.15);\n"
            "  vec3 col = texture(u_texture, vTexCoord).rgb * diff;\n"
            "  fragColor = vec4(col, 1.0);\n"
            "}\n"
        );
    }

    if (!impl->starShader.load("shaders/star.vert", "shaders/star.frag")) {
        impl->starShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec3 aPos;\n"
            "layout(location=1) in float aMag;\n"
            "uniform mat4 u_view, u_projection;\n"
            "uniform float u_time;\n"
            "out float vAlpha;\n"
            "void main(){\n"
            "  gl_Position = u_projection * u_view * vec4(aPos * 20.0, 1.0);\n"
            "  vAlpha = 0.3 + 0.7 * (1.0 - aMag / 6.0);\n"
            "  vAlpha *= 0.8 + 0.2 * sin(u_time * 0.5 + aPos.x * 10.0);\n"
            "  gl_PointSize = max(1.0, (3.0 - aMag) * 1.5);\n"
            "}\n",
            "#version 400 core\n"
            "in float vAlpha;\n"
            "out vec4 fragColor;\n"
            "void main(){\n"
            "  vec2 c = gl_PointCoord * 2.0 - 1.0;\n"
            "  float d = dot(c, c);\n"
            "  float a = smoothstep(1.0, 0.0, d) * vAlpha;\n"
            "  fragColor = vec4(1.0, 1.0, 0.95, a);\n"
            "}\n"
        );
    }

    if (!impl->atmosphereShader.load("shaders/atmosphere.vert", "shaders/atmosphere.frag")) {
        impl->atmosphereShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec3 aPos;\n"
            "uniform mat4 u_model, u_view, u_projection;\n"
            "out vec3 vPos;\n"
            "void main(){\n"
            "  vPos = aPos;\n"
            "  gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);\n"
            "}\n",
            "#version 400 core\n"
            "in vec3 vPos;\n"
            "uniform vec3 u_sunDir;\n"
            "uniform vec3 u_cameraPos;\n"
            "out vec4 fragColor;\n"
            "void main(){\n"
            "  vec3 n = normalize(vPos);\n"
            "  vec3 viewDir = normalize(u_cameraPos - vPos);\n"
            "  float fresnel = 1.0 - max(dot(viewDir, n), 0.0);\n"
            "  fresnel = pow(fresnel, 3.0);\n"
            "  float sunAlign = max(dot(n, normalize(u_sunDir)), 0.0);\n"
            "  vec3 dayColor = vec3(0.3, 0.6, 1.0);\n"
            "  vec3 sunsetColor = vec3(1.0, 0.4, 0.1);\n"
            "  vec3 col = mix(dayColor, sunsetColor, pow(1.0 - sunAlign, 5.0));\n"
            "  col *= 0.5 + 0.5 * sunAlign;\n"
            "  fragColor = vec4(col, fresnel * 0.6);\n"
            "}\n"
        );
    }

    if (!impl->bloomShader.load("shaders/bloom.vert", "shaders/bloom.frag")) {
        impl->bloomShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec2 aPos;\n"
            "out vec2 vUV;\n"
            "void main(){ vUV = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n",
            "#version 400 core\n"
            "in vec2 vUV;\n"
            "uniform sampler2D u_texture;\n"
            "uniform vec2 u_direction;\n"
            "uniform vec2 u_resolution;\n"
            "out vec4 fragColor;\n"
            "void main(){\n"
            "  float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
            "  vec3 result = texture(u_texture, vUV).rgb * weight[0];\n"
            "  vec2 texel = u_direction / u_resolution;\n"
            "  for(int i=1;i<5;i++){\n"
            "    result += texture(u_texture, vUV + texel * float(i)).rgb * weight[i];\n"
            "    result += texture(u_texture, vUV - texel * float(i)).rgb * weight[i];\n"
            "  }\n"
            "  fragColor = vec4(result, 1.0);\n"
            "}\n"
        );
    }

    if (!impl->compositeShader.load("shaders/composite.vert", "shaders/composite.frag")) {
        impl->compositeShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec2 aPos;\n"
            "out vec2 vUV;\n"
            "void main(){ vUV = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n",
            "#version 400 core\n"
            "in vec2 vUV;\n"
            "uniform sampler2D u_scene;\n"
            "uniform sampler2D u_bloom;\n"
            "uniform bool u_bloomEnabled;\n"
            "out vec4 fragColor;\n"
            "void main(){\n"
            "  vec3 scene = texture(u_scene, vUV).rgb;\n"
            "  vec3 bloom = texture(u_bloom, vUV).rgb;\n"
            "  vec3 col = scene;\n"
            "  if(u_bloomEnabled) col += bloom * 0.4;\n"
            "  fragColor = vec4(col, 1.0);\n"
            "}\n"
        );
    }

    // Globe
    impl->earth.init();
    impl->stars.init();
    impl->atmosphere.init();
    impl->terminator.init();

    // Ionosphere
    impl->tecLayer.init();
    impl->dataManager.init();
    impl->esaAdapter.init();

    // Generate initial TEC data
    impl->esaAdapter.fetchTEC(glfwGetTime());
    TECGrid grid;
    grid.width = impl->esaAdapter.getData().width;
    grid.height = impl->esaAdapter.getData().height;
    grid.values = impl->esaAdapter.getData().values;
    grid.timestamp = impl->esaAdapter.getData().timestamp;
    impl->tecLayer.updateFromGrid(grid);

    // Framebuffers
    impl->sceneFBO.init(winW, winH);
    impl->bloomFBO[0].init(winW / 2, winH / 2);
    impl->bloomFBO[1].init(winW / 2, winH / 2);

    // Fullscreen quad for post-processing (bloom, composite)
    float quadVerts[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
    };
    glGenVertexArrays(1, &impl->quadVAO);
    glGenBuffers(1, &impl->quadVBO);
    glBindVertexArray(impl->quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, impl->quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Set scroll callback once
    glfwSetWindowUserPointer(window, this);
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double sy) {
        auto* self = static_cast<App*>(glfwGetWindowUserPointer(w));
        if (self) self->impl->camera.distance -= (float)sy * 0.2f;
    });

    impl->updateSunDirection();

    printf("[OK] All initialized\n");
    return true;
}

void App::run() {
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        impl->time = currentFrame;

        glfwPollEvents();
        processInput();
        update(deltaTime);
        render();
        glfwSwapBuffers(window);
    }
}

void App::processInput() {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // Mouse orbit
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        float dx = (float)(xpos - lastMouseX);
        float dy = (float)(ypos - lastMouseY);
        impl->camera.rotateY += dx * 0.005f;
        impl->camera.rotateX += dy * 0.005f;
        impl->camera.rotateX = glm::clamp(impl->camera.rotateX, -1.5f, 1.5f);
    }

    lastMouseX = (float)xpos;
    lastMouseY = (float)ypos;

    // Zoom
    double scrollY = 0.0;
    glfwSetScrollCallback(window, [](GLFWwindow*, double, double sy) {});
    // Simple zoom with keys
    if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS)
        impl->camera.distance -= 0.02f;
    if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS)
        impl->camera.distance += 0.02f;

    impl->camera.distance = glm::clamp(impl->camera.distance, 1.5f, 20.0f);
}

void App::update(float dt) {
    impl->updateSunDirection();
}

void App::render() {
    // --- Pass 1: Render scene to FBO ---
    impl->sceneFBO.bind();
    glClearColor(0.01f, 0.01f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view = impl->camera.getViewMatrix();
    glm::mat4 proj = glm::perspective(
        glm::radians(impl->camera.fov),
        impl->camera.aspect,
        impl->camera.nearP,
        impl->camera.farP
    );

    // Stars (first, behind everything)
    glDepthMask(GL_FALSE);
    impl->starShader.use();
    impl->starShader.setMat4("u_view", view);
    impl->starShader.setMat4("u_projection", proj);
    impl->starShader.setFloat("u_time", impl->time);
    impl->stars.render();
    glDepthMask(GL_TRUE);

    // Earth
    glm::mat4 model = glm::mat4(1.0f);
    impl->earthShader.use();
    impl->earthShader.setMat4("u_model", model);
    impl->earthShader.setMat4("u_view", view);
    impl->earthShader.setMat4("u_projection", proj);
    impl->earthShader.setVec3("u_sunDir", impl->sunDirection);
    impl->earthShader.setInt("u_texture", 0);
    impl->earth.render();

    // TEC Layer (additive blend on top of Earth)
    if (impl->tecLayer.isVisible()) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        impl->tecLayer.render(view, proj);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // Atmosphere (additive)
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glm::mat4 atmoModel = glm::scale(glm::mat4(1.0f), glm::vec3(1.02f));
    impl->atmosphereShader.use();
    impl->atmosphereShader.setMat4("u_model", atmoModel);
    impl->atmosphereShader.setMat4("u_view", view);
    impl->atmosphereShader.setMat4("u_projection", proj);
    impl->atmosphereShader.setVec3("u_sunDir", impl->sunDirection);
    impl->atmosphereShader.setVec3("u_cameraPos", impl->camera.position);
    impl->atmosphere.render();
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Terminator line
    impl->terminatorShader.use();
    model = glm::mat4(1.0f);
    impl->terminatorShader.setMat4("u_model", model);
    impl->terminatorShader.setMat4("u_view", view);
    impl->terminatorShader.setMat4("u_projection", proj);
    impl->terminatorShader.setVec3("u_sunDir", impl->sunDirection);
    impl->terminator.render();

    impl->sceneFBO.unbind();

    // --- Pass 2: Bloom ---
    if (impl->bloomEnabled) {
        glDisable(GL_DEPTH_TEST);
        impl->bloomShader.use();

        // Horizontal pass
        impl->bloomFBO[0].bind();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        impl->bloomShader.setVec2("u_direction", glm::vec2(1.0f, 0.0f));
        impl->bloomShader.setVec2("u_resolution", glm::vec2((float)winW / 2, (float)winH / 2));
        impl->bloomShader.setInt("u_texture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, impl->sceneFBO.colorTexture);
        glBindVertexArray(impl->quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        impl->bloomFBO[0].unbind();

        // Vertical pass
        impl->bloomFBO[1].bind();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        impl->bloomShader.setVec2("u_direction", glm::vec2(0.0f, 1.0f));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, impl->bloomFBO[0].colorTexture);
        glBindVertexArray(impl->quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        impl->bloomFBO[1].unbind();

        glEnable(GL_DEPTH_TEST);
    }

    // --- Pass 3: Composite to screen ---
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    impl->compositeShader.use();

    // Unit 0 = scene
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, impl->sceneFBO.colorTexture);
    impl->compositeShader.setInt("u_scene", 0);

    // Unit 1 = bloom (DO NOT rebind GL_TEXTURE0!)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, impl->bloomFBO[1].colorTexture);
    impl->compositeShader.setInt("u_bloom", 1);

    impl->compositeShader.setBool("u_bloomEnabled", impl->bloomEnabled);

    // Draw fullscreen quad with persistent VAO
    glBindVertexArray(impl->quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);

    // --- ImGui ---
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // FPS
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 60), ImGuiCond_FirstUseEver);
    ImGui::Begin("Ionosphere Live 3D");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("OpenGL: %s", glGetString(GL_VERSION));
    ImGui::Checkbox("Bloom", &impl->bloomEnabled);
    ImGui::End();

    // Layer panel
    ImGui::SetNextWindowPos(ImVec2((float)winW - 220, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(210, 250), ImGuiCond_FirstUseEver);
    ImGui::Begin("Layers");
    static bool showEarth = true, showAtmo = true, showStars = true, showTEC = false;
    ImGui::Checkbox("Earth", &showEarth);
    ImGui::Checkbox("Atmosphere", &showAtmo);
    ImGui::Checkbox("Stars", &showStars);
    ImGui::Separator();
    ImGui::Text("Ionosphere");
    if (ImGui::Checkbox("TEC Layer", &showTEC)) {
        impl->tecLayer.setVisible(showTEC);
    }
    static float altSlider = 300.0f;
    if (ImGui::SliderFloat("Altitude (km)", &altSlider, 100.0f, 800.0f)) {
        impl->tecLayer.setAltitude(altSlider / 6371.0f);
    }
    ImGui::Separator();
    ImGui::Text("Sun: %.2f, %.2f, %.2f", impl->sunDirection.x, impl->sunDirection.y, impl->sunDirection.z);
    ImGui::Separator();
    if (impl->dataManager.isFetching()) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Fetching TEC...");
    } else {
        ImGui::Text("TEC: %dx%d", impl->esaAdapter.getData().width, impl->esaAdapter.getData().height);
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void App::shutdown() {
    impl->tecLayer.destroy();

    if (impl->quadVAO) glDeleteVertexArrays(1, &impl->quadVAO);
    if (impl->quadVBO) glDeleteBuffers(1, &impl->quadVBO);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (window) glfwDestroyWindow(window);
    glfwTerminate();

    delete impl;
    impl = nullptr;
}
