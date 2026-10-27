#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

// VanAllenBelts: cinturones de radiacion de Van Allen como dos tori
// alambrados (GL_LINES) alrededor de la Tierra. Port de belts.ts de la
// demo web (ionosphere-web): solo geometria + brillo, sin red ni datos
// propios; App.cpp empuja Kp, rbeGlow y el tiempo.
//
//   - Interno (protones): L = 1.5 R_E fijo, estable, plata-acero
//     (0.72, 0.78, 0.92). Radio de tubo 0.28 R_E.
//   - Externo (electrones): L = 4.2 + 0.08 * Kp con Kp acotado a 0..9,
//     ambar (1.00, 0.70, 0.28). Radio de tubo 0.85 R_E.
//   - Alpha externo = 0.70 * (0.45 + 0.55 * rbeGlow): un evento RBE
//     reciente (glow 0..1) enciende el cinturon de electrones.
//
// L es el valor fisico (radio del eje del toro). Los radios de tubo NO lo
// son: son una eleccion de dibujo para que los cinturones se lean como
// bandas y no como lineas de 1 px.
//
// Escala: 1 unidad = 1 R_E = 6371 km. El cinturon externo vive entre 4.2
// y 4.9 R_E, muy por fuera del encuadre por defecto: hay que alejar la
// camara (la distancia maxima es 20 R_E) para verlo entero junto al
// interno, que a 1.5 R_E casi roza la superficie.
//
// Deriva fisica: protones hacia el OESTE, electrones hacia el ESTE. Se
// dibuja como nudos de brillo que viajan por el angulo mayor (alpha
// modulado por cos(3*(u - fase))); la malla no gira.
//
// Simplificaciones declaradas:
//   - Eje del toro = +Y geografico (ecuador geometrico): se ignoran la
//     inclinacion y el desplazamiento del dipolo real (~11 grados).
//   - No hay feed RBE en vivo: App.cpp calcula rbeGlow (decaida a 14 dias
//     en la demo web) y lo empuja aqui.
//   - Sin AE9/AP9: forma parametrica por Kp (v1 de la demo web).
//
// Convencion de coordenadas del motor (RefShells/GridLayer):
//   phi = pi/2 - radians(lat), theta = pi - radians(lon)
//   x = r*sin(phi)*cos(theta), y = r*cos(phi), z = r*sin(phi)*sin(theta)
// El angulo mayor u se interpreta como longitud (crece hacia el ESTE) y el
// tubo se abre en el plano (radial, +Y), de modo que theta = pi - u.
class VanAllenBelts {
public:
    VanAllenBelts();
    ~VanAllenBelts();

    void init();
    void destroy();

    void setVisible(bool v);          // default false
    bool visible() const;

    void setKp(float kp);             // clamps 0..9, non-finite ignored
    void setRbeGlow(float glow01);    // clamps 0..1
    void setTimeSeconds(float t);     // drives the drift animation

    float kp() const;
    float outerL() const;             // current outer belt L in R_E
    static float outerLForKp(float kp);

    void update();                                   // rebuild if dirty
    void render(const glm::mat4& view, const glm::mat4& projection);

private:
    void rebuild();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;

    int innerFirst_ = 0, innerCount_ = 0;
    int outerFirst_ = 0, outerCount_ = 0;

    bool visible_ = false;
    float kp_ = 0.0f;               // Kp geomagnetico (0..9)
    float rbeGlow_ = 0.0f;          // brillo por RBE reciente (0..1)
    float time_ = 0.0f;             // segundos, solo fase de deriva
    bool dirty_ = true;
};
