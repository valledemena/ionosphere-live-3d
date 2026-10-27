#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// SolarWindLayer: capa 3D de flechas sobre el globo que representa el viento
// solar. Cada flecha es un segmento (GL_LINES) orientado segun el flujo del
// viento (desde el subsolar hacia el antisol), con longitud proporcional a la
// velocidad y color por magnitud (rampa web: velocidad + Bz sur + densidad) o
// por Bz (rojo=norte, azul=sur).
//
// Convencion de coordenadas (coincide con la esfera del GridLayer/IonosondeLayer):
//   phi = (90 - lat) en rad, theta = lon en rad
//   x = r*sin(phi)*cos(theta), y = r*cos(phi), z = r*sin(phi)*sin(theta)
class SolarWindLayer {
public:
    SolarWindLayer();
    ~SolarWindLayer();

    void init();
    // Reconstruye las flechas a partir del estado del viento solar.
    //   speed, bz   : magnitudes actuales (para escala/color).
    //   density     : densidad de protones (cm^-3); modula el brillo.
    //   sunWorld    : direccion del sol en espacio de mundo (normalizada).
    //   mode        : 0 = rampa velocidad/Bz/densidad (demo web);
    //                 1 = color por Bz (divergente rojo/azul).
    void updateArrowData(float speed, float bz, float density,
                         const glm::vec3& sunWorld, int mode);
    void render(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    void setVisible(bool v) { visible_ = v; }
    bool isVisible() const { return visible_; }

    void setHeightOffset(float alt) { heightOffset_ = glm::clamp(alt, 0.0f, 0.15f); }
    float getHeightOffset() const { return heightOffset_; }

    // Escala de las flechas (multiplicador de longitud). 1.0 = normal.
    void setArrowScale(float s) { arrowScale_ = glm::clamp(s, 0.0f, 5.0f); rebuild_ = true; }
    float getArrowScale() const { return arrowScale_; }

    int getArrowCount() const { return arrowCount_; }

private:
    void rebuild();
    glm::vec3 latLonToPos(float latDeg, float lonDeg, float r) const;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;
    int arrowCount_ = 0;

    bool visible_ = false;
    float heightOffset_ = 0.015f;   // ~95 km sobre el radio unitario
    float arrowScale_ = 1.0f;
    float speed_ = 380.0f;
    float bz_ = 0.0f;
    float density_ = 5.0f;
    glm::vec3 sunWorld_ = glm::vec3(1.0f, 0.0f, 0.0f);
    int mode_ = 0;
    bool rebuild_ = true;
};
