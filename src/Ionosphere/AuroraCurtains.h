#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Ionosphere/GridLayer.h"   // GridData

// DW.28 V7: cortinas de aurora 100-250 km sobre el ovalo Ovation.
//
// Una cortina por celda del grid NOAA Ovation por encima del umbral (8),
// con tres bandas emissivas parametrisadas: 557,7 nm (verde, 100-150 km),
// 630,0 nm (rojo, 200-250 km) y N2+ (violeta, borde inferior). La
// inclinacion sigue la direccion del campo dipolar derivada de
// irtameval::igrfFull (IGRF-14 NMAX=13, 195 coeficientes + SV, epoch
// 2025.0), en MARCO TIERRA. La magnetopausa Shue permanece en GSM: no se
// mezclan marcos. Es una parametrisacion visual declarada, no un modelo
// de emision radiativa ni de transporte de particulas.
//
// Un unico draw aditivo, sin FBOs nuevos. Flag OFF por defecto: la capa
// ovalada GridLayer existente queda intacta.
class AuroraCurtains {
public:
    AuroraCurtains() = default;
    ~AuroraCurtains() { destroy(); }

    void init();
    void updateFromGrid(const GridData& grid);
    void render(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    void setVisible(bool v) { visible_ = v; }
    bool visible() const { return visible_; }
    void setIntensity(float v) { intensity_ = glm::clamp(v, 0.0f, 3.0f); }
    float intensity() const { return intensity_; }
    int curtainCount() const { return curtainCount_; }
    int activeCells() const { return activeCells_; }
    float gridMax() const { return gridMax_; }
    double gridTimestamp() const { return gridTimestamp_; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;
    bool visible_ = false;
    float intensity_ = 1.0f;
    int curtainCount_ = 0;   // = activeCells_ (1 quad por celda)
    int activeCells_ = 0;
    float gridMax_ = 0.0f;
    double gridTimestamp_ = 0.0f;
    glm::vec3 fieldAxis_ = glm::vec3(0.0f, 1.0f, 0.0f);
};
