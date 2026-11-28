#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Ionosphere/IonosondeLayer.h"   // StationPoint

// DW.29 V10a: capas ionosfericas bajas analyticas.
//
// D-layer: carcasa esferica a 80 km (region D, 60-90 km) con opacidad
// por iluminacion solar y factor de flare GOES. Es un PROXY VISUAL de la
// absorcion: el calculo Sauer-Wilkinson del LUF/SID queda intacto.
//
// Es 3D: un parche por estacion GIRO con foEs > 3 MHz, a hEs (o 110 km
// por defecto), coloreado por foEs. Visualiza la medicion; no predice Es.
//
// Dos flags independientes, OFF por defecto. Un draw aditivo cada una,
// sin FBOs nuevos, depth test on y depth mask off.
class LowerIonoLayers {
public:
    LowerIonoLayers() = default;
    ~LowerIonoLayers() { destroy(); }

    void init();
    void updateEs(const std::vector<StationPoint>& samples);
    void setDLayerParams(const glm::vec3& sunDir, float flare);
    void renderD(const glm::mat4& view, const glm::mat4& projection);
    void renderEs(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    int esPatchCount() const { return patchCount_; }
    float esMaxFoEs() const { return maxFoEs_; }

private:
    GLuint dVao_ = 0, dVbo_ = 0, dShader_ = 0;
    GLuint esVao_ = 0, esVbo_ = 0, esShader_ = 0;
    int dVerts_ = 0;
    int patchCount_ = 0;
    float maxFoEs_ = 0.0f;
    glm::vec3 sunDir_ = glm::vec3(1.0f, 0.0f, 0.0f);
    float flare_ = 0.0f;
};
