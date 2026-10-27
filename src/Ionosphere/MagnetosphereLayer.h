#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

// MagnetosphereLayer: cavidad magnetosferica en escala REAL (1 unidad = 1 R_E,
// R_E = 6371 km), portada de la demo web (ionosphere-web, magnetosphere.ts).
// Dos grupos de geometria, ambos con GL_LINES y un unico shader:
//
//   1. Magnetopausa Shue-1998 en WIREFRAME (no superficie: es una cavidad de
//      lineas, sin relleno ni sombreado). Forma funcional y coeficientes
//      pineados de [S98] (Shue et al. 1998, JGR 103, 17691), con Bz en nT,
//      Dp en nPa, r en R_E y theta = angulo cenital solar en radianes:
//
//        r0(Bz,Dp) = [10.22 + 1.29*tanh(0.184*(Bz+8.14))] / Dp^(1/6.6)
//        alpha(Bz,Dp) = (0.58 - 0.007*Bz) * (1 + 0.024*ln(Dp))
//        r(theta) = r0 * [2/(1+cos(theta))]^alpha
//
//      con Dp la presion dinamica del viento solar
//      Dp = 1.6726e-6 * n[cm^-3] * v[km/s]^2 (mp*n*v^2) acotada a 0.05 nPa.
//      Malla: 7 anillos (theta = 15, 30, ... 105 grados, 48 segmentos
//      cerrados) + 8 meridianos (phi = m*45 grados, theta = 5..120 grados en
//      30 pasos), construida en marco solar (+x al Sol) y rotada con la
//      direccion solar viva. La web llega a theta=135/150; aqui se recorta
//      declaradamente (ver rebuild) porque mas alla de ~105 grados la
//      superficie pasa de 25 R_E y solo queda como arcos sueltos en pantalla.
//
//      COLA: el hemisferio nocturno (x<0) se estira con
//      s = 1 + 0.15*max(0,-Bz)/5. Es una PARAMETRIZACION v1 DECLARADA, NO el
//      modelo de Tsyganenko (no hay corriente de cola ni campo real detras).
//
//   2. Lineas de campo dipolares del termino IGRF-14 GRADO 1, epoca 2025.0
//      CONGELADA (sin variacion secular): g10 = -29350.0, g11 = -1410.30,
//      h11 = 4545.50 nT. El eje dipolar resultante queda en ~(80.79 N,
//      72.77 W) con una inclinacion de 9.21 grados respecto del eje de giro.
//      Cascaras L = 1.5, 2.5, 4.0, 6.0 x 8 meridianos magneticos (32 lineas)
//      con r = L*cos^2(lambda) en R_E. Un grado IGRF > 1 queda fuera.
//
// Escala y camara: no se comprime nada radialmente; 1 unidad = 1 R_E. La
// magnetopausa subsolar cae en ~6-12 R_E y la cola estirada pasa de 10 R_E,
// asi que las zonas mas alla de ~10 R_E solo se ven con la camara alejada
// (zoom out hasta 20 R_E). Las cascaras L<=6 se ven con el zoom por defecto.
//
// Fuera de alcance: muestrear B(r) (eso lo resuelve el servicio
// magnetosphereField de la web, aqui no hay campo consultable), el termino
// cuadrupolar, la corriente de anillo y cualquier acoplamiento MHD. Esta capa
// solo dibuja geometria; App.cpp empuja viento solar y direccion solar.
//
// Convencion de coordenadas del motor (RefShells/GeospaceOverlays):
//   phi = pi/2 - radians(lat), theta = pi - radians(lon)
//   x = r*sin(phi)*cos(theta), y = r*cos(phi), z = r*sin(phi)*sin(theta)
// y marco web Y-up (wx, wy, wz) -> motor = (-wx, wy, -wz).
//
// render() NO toca estado de blend/depth: lo pone el llamante (blend aditivo
// SRC_ALPHA, ONE; depth test on; depth mask off), asi que aColor.a es brillo.
class MagnetosphereLayer {
public:
    MagnetosphereLayer();
    ~MagnetosphereLayer();

    void init();
    void destroy();

    void setVisible(bool v);          // por defecto false
    bool visible() const;

    void setSolarWind(float speedKms, float densityCm3, float bzNT);  // ignora no finitos
    void setSunDirection(const glm::vec3& sunWorld);                  // ignora el vector nulo

    float subsolarR0() const;         // r0 Shue actual en R_E
    float alpha() const;
    float tailStretch() const;
    static float dynamicPressure(float densityCm3, float speedKms);    // 1.6726e-6 * n * v^2

    void update();                                    // reconstruye si esta sucio
    void render(const glm::mat4& view, const glm::mat4& projection);

private:
    void rebuild();
    void refreshKey();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;

    int ringFirst_ = 0, ringCount_ = 0;
    int merFirst_ = 0, merCount_ = 0;
    int dipFirst_ = 0, dipCount_ = 0;

    bool visible_ = false;
    bool dirty_ = true;

    float speedKms_ = 400.0f;               // km/s
    float densityCm3_ = 5.0f;               // cm^-3
    float bzNT_ = 0.0f;                     // nT (IMF Bz)
    glm::vec3 sun_ = glm::vec3(1.0f, 0.0f, 0.0f);

    // Clave de rebuild redondeada (r0 2 dp, alpha 3 dp, cola 3 dp): evita
    // regenerar la malla por ruido numerico de los datos.
    bool keyValid_ = false;
    float keyR0_ = 0.0f;
    float keyAlpha_ = 0.0f;
    float keyTail_ = 0.0f;
};
