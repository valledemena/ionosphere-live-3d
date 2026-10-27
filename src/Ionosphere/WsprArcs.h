#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include "Data/WsprAdapter.h"

// WsprArcs: un arco de gran circulo por spot WSPR (TX -> RX) a altura
// ionosferica. Port de la capa de arcos de ionosphere-web (wspr.ts).
//
// FUENTE Y ATRIBUCION: spots de la red de voluntarios WSPRnet (balizas y
// receptores de radioaficionados), tabla publica olddb. Los datos NO son
// nuestros ni se corrigen: se dibuja exactamente lo que reporta la red.
// App.cpp empuja el ultimo snapshot del WsprAdapter.
//
// PERSISTENCIA Y TECHO (declarados): cada spot caduca a los 10 minutos
// (SPOT_TTL_MIN, lo aplica el adapter); el alpha del arco decae con la edad
// (0.80 * clamp(1 - edad/600, 0.12, 1)). Se dibujan como maximo 40 arcos
// (SPOT_CAP). Sin spots no se dibuja NADA (ni rejilla, ni arcos sinteticos).
//
// GEOMETRIA: la altura y el abombamiento son una ELECCION DE PANTALLA, no
// fisica: base 1.03 R_E (~190 km) y bulge 0.06 R_E * sin(pi t) * min(1,
// ang/2), de modo que los trayectos largos se ven mas altos. El arco es un
// slerp de 32 segmentos entre los dos puntos (GL_LINES, 2 vertices/segmento).
//
// COLOR POR BANDA (fijo, port del mapa de wspr.ts; gris 0.6,0.6,0.6 si la
// banda no esta en la tabla):
//   160m violeta, 80m azul, 60m celeste, 40m verde-teal, 30m verde,
//   20m amarillo, 17m naranja, 15m naranja-rojo, 12m rojo, 10m rosa,
//   6m magenta.
//
// Convencion de coordenadas: la del motor (RefShells/GridLayer):
//   phi = pi/2 - radians(lat), theta = pi - radians(lon)
//   x = r*sin(phi)*cos(theta), y = r*cos(phi), z = r*sin(phi)*sin(theta)
class WsprArcs {
public:
    WsprArcs();
    ~WsprArcs();

    void init();
    void destroy();

    void setVisible(bool v);
    bool visible() const { return visible_; }

    // Solo marca dirty si cambia lo visible (n spots o max timeUtc). Ademas
    // se refresca al cambiar el tramo de fade de 1 min, para que el decaimiento
    // declarado de 10 min se vea sin reconstruir cada frame.
    void setSnapshot(const std::vector<WsprSpot>& spots, double nowUtc);
    void update();
    void render(const glm::mat4& view, const glm::mat4& projection);

    int arcCount() const { return arcCount_; }
    static void bandColor(const std::string& band, glm::vec3& rgb);

private:
    void rebuild();
    static glm::vec3 latLonToUnit(float latDeg, float lonDeg);

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;

    bool visible_ = false;
    bool dirty_ = true;
    bool haveSnapshot_ = false;

    int vertexCount_ = 0;
    int arcCount_ = 0;
    size_t lastSpotCount_ = 0;
    double lastSpotMax_ = 0.0;
    int lastFadeStep_ = -1;
    double nowUtc_ = 0.0;

    std::vector<WsprSpot> spots_;
};
