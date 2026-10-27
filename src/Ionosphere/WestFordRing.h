#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

// WestFordRing: reconstruccion declarada del cinturon de dipolos de cobre
// West Ford (mayo 1963), portado de la demo web (ring1963.ts).
//
// Es una ILUSTRACION de un unico plano orbital, no una orbita fingida:
//   - Sin datos vivos, sin propagacion y sin rotacion simulada: la nube es
//     estatica y solo se dibuja el plano del cinturon.
//   - Nodo (RAAN) fijo declarado en 0 grados: valor ILUSTRATIVO, no hay
//     dato historico de RAAN. Inclinacion 87.4 grados (SAGE 2025) con
//     jitter +-0.6 grados en nodo e inclinacion como espesor declarado.
//   - 4.8e8 dipolos reales de 1.78 cm (documentacion); se dibujan 4096
//     muestras deterministas (LCG mulberry32 con semilla 19630509), asi la
//     nube es reproducible byte a byte entre builds.
//
// Altitud -> radio (1 unidad = 1 R_E, R_E = 6371 km):
//   alt 3600 km -> r = 1 + 3600/6371 = 1.5651 R_E
//   alt 3680 km -> r = 1 + 3680/6371 = 1.5776 R_E
//   El muestreo usa r = 1 + altKm/6371 con altKm uniforme en ese rango.
//
// Convencion de coordenadas: la del motor (RefShells/GridLayer):
//   phi = pi/2 - radians(lat), theta = pi - radians(lon)
//   x = r*sin(phi)*cos(theta), y = r*cos(phi), z = r*sin(phi)*sin(theta)
// La demo web trabaja en un marco Y-up (wx, wy, wz); el mapeo aplicado es
// engine = (-wx, wy, -wz) (giro de 180 grados sobre Y), de modo que la banda
// cae en el mismo plano geografico que en la web.
//
// Dibujo: GL_POINTS con un unico programa (pos + rgba intercalados, 7 floats
// por vertice) y color uniforme (0.62, 0.86, 1.00) con alpha 0.55. El bloque
// de App.cpp usa blend aditivo (SRC_ALPHA, ONE), asi que alpha = brillo.
// render() NO toca el estado de blend ni de profundidad: lo fija el llamante.
class WestFordRing {
public:
    WestFordRing();
    ~WestFordRing();

    void init();
    void destroy();

    void setVisible(bool v);          // por defecto false
    bool visible() const;

    void setPointSize(float px);      // acotado 1..6

    int particleCount() const;
    static const char* label();

    void update();                                   // hook vacio (simetria)
    void render(const glm::mat4& view, const glm::mat4& projection);

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;

    bool visible_ = false;
    float pointSize_ = 2.0f;
    int count_ = 0;
};
