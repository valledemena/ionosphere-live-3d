#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// SliceLayer: "cortina" vertical de Ne(h) a lo largo de un trayecto (QTH->DX).
// Malla SEG×LVL sobre el gran circulo entre A y B (60-500 km, espaciado ^1.5
// como el volumen), con textura RGBA horneada en CPU (colormap compartido).
// Los valores 0..1 vienen del DensityVolume ya construido (misma escala).
class SliceLayer {
public:
    SliceLayer();
    ~SliceLayer();

    void init();
    // Reconstruye geometria (si A/B cambian) y textura (si hay datos nuevos).
    //   ax,ay,az / bx,by,bz: extremos unitarios en el marco del motor.
    //   values: SEG*LVL valores 0..1 (s rapido = a lo largo del trayecto).
    //   cmap: nombre de paleta (viridis/inferno/...).
    void updateSlice(const glm::vec3& a, const glm::vec3& b,
                     const std::vector<float>& values,
                     const std::string& cmap,
                     float altMinKm = 60.0f, float altMaxKm = 500.0f);
    void render(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    void setVisible(bool v) { visible_ = v; }
    bool isVisible() const { return visible_; }
    void setOpacity(float o) { opacity_ = glm::clamp(o, 0.0f, 1.5f); }
    bool hasData() const { return hasData_; }

    static const int SEG = 64;   // tramos a lo largo del trayecto
    static const int LVL = 20;   // niveles 60..500 km

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, texture_ = 0, shader_ = 0;
    int indexCount_ = 0;
    bool visible_ = false;
    bool hasData_ = false;
    float opacity_ = 0.9f;
};
