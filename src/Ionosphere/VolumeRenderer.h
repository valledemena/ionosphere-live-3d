#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "Ionosphere/DensityVolume.h"

// VolumeRenderer: raymarching single-pass de Ne(lat, lon, h) sobre una
// textura 3D (ver docs/volumetric-density-spec.md).
//
// Convencion esferica del motor (NO la ingenua): lon = pi - atan2(z, x),
// t = lat/pi + 0.5 (t=0 es el SUR: la fila 0 del upload es latIdx 0).
// Ver IonosondeLayer.cpp:249-250. Invertirla lee el hemisferio especular.
class VolumeRenderer {
public:
    VolumeRenderer();
    ~VolumeRenderer();

    void init();
    void updateVolume(const DensityVolume& vol);
    void render(const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& camPosWorld);
    void destroy();

    void setVisible(bool v) { visible_ = v; }
    bool isVisible() const { return visible_; }

    void setColormap(const std::string& name);
    const std::string& getColormapName() const { return colormapName_; }
    // (Sin setDensityRange a proposito: la normalizacion log10 [8,12.5] se hace
    // en CPU al construir el volumen; un setter aqui mentiria.)
    void setOpacityScale(float k) { opacityScale_ = glm::clamp(k, 0.0f, 3.0f); }
    float getOpacityScale() const { return opacityScale_; }
    void setStepCount(int n) { steps_ = glm::clamp(n, 16, 256); }

    bool hasVolume() const { return hasVolume_; }

private:
    GLuint tex3D_ = 0;
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLuint colormapTex_ = 0;
    GLuint shader_ = 0;
    int indexCount_ = 0;
    int lonRes_ = 0, latRes_ = 0, altRes_ = 0;
    float altMinKm_ = 60.0f, altMaxKm_ = 500.0f;
    float opacityScale_ = 1.0f;
    int steps_ = 64;
    bool visible_ = false;
    bool hasVolume_ = false;
    std::string colormapName_ = "viridis";
};
