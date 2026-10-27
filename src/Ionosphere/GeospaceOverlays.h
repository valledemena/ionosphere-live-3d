#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

// GeospaceOverlays: tres capas ligeras portadas de la demo web
// (ionosphere-web), todas derivadas de datos que el motor ya descarga:
//
//   1. Anillo Dst  - circulo ecuatorial cuyo radio se cierra con la
//      tormenta (formula web: r = 5.5 + Dst/100, acotado 3.5..6 R_E) y
//      cuyo color sigue las bandas quiet/unsettled/storm/intense.
//      El radio real 3.5..6 R_E queda fuera de la camara por defecto
//      (dist 3), asi que se COMPRIME a 1.25..1.80 R_E conservando el
//      orden (mas negativo = mas cerca). El tooltip lo declara.
//   2. Electrojets - dos bucles a latitud +/-67 grados (oval auroral
//      aproximado) a ~110 km, coloreados por AE (azul -> amarillo ->
//      blanco) con marcas radiales tipo filamento.
//   3. Grey line   - cinta de +/-7.5 grados alrededor del terminador
//      (zenit solar 82.5..97.5 grados), calculada directamente en 3D a
//      partir de la direccion del Sol (gran circulo perpendicular a el).
//      Equivale a la geometria de greyline.ts sin pasar por lat/lon.
//
// Convencion de coordenadas: la del motor (RefShells/GridLayer):
//   phi = pi/2 - radians(lat), theta = pi - radians(lon)
//   x = r*sin(phi)*cos(theta), y = r*cos(phi), z = r*sin(phi)*sin(theta)
//
// Sin datos propios ni red: App.cpp empuja Dst, AE y la direccion solar.
class GeospaceOverlays {
public:
    GeospaceOverlays();
    ~GeospaceOverlays();

    void init();
    void destroy();

    void setDstRingVisible(bool v) { dstVisible_ = v; dirty_ = true; }
    void setElectrojetVisible(bool v) { aeVisible_ = v; dirty_ = true; }
    void setGreylineVisible(bool v) { greyVisible_ = v; dirty_ = true; }

    bool dstRingVisible() const { return dstVisible_; }
    bool electrojetVisible() const { return aeVisible_; }
    bool greylineVisible() const { return greyVisible_; }
    bool hasVisible() const { return dstVisible_ || aeVisible_ || greyVisible_; }

    // Datos vivos (throttle interno: solo marca dirty si cambian de verdad).
    void setDst(float dstNT);
    void setAe(float aeNT);
    void setSunDirection(const glm::vec3& sunWorld);

    float getDst() const { return dst_; }
    float getAe() const { return ae_; }
    // Radio dibujado del anillo Dst (R_E comprimidas) y radio real.
    float getDstDrawnRadius() const;
    static float dstTrueRadius(float dstNT);

    // Reconstruye la geometria si algo cambio. Idempotente.
    void update();
    void render(const glm::mat4& view, const glm::mat4& projection);

private:
    void rebuild();
    glm::vec3 latLonToPos(float latDeg, float lonDeg, float r) const;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;

    int dstFirst_ = 0, dstCount_ = 0;
    int aeFirst_ = 0, aeCount_ = 0;
    int greyFirst_ = 0, greyCount_ = 0;

    bool dstVisible_ = false;
    bool aeVisible_ = false;
    bool greyVisible_ = false;

    float dst_ = 0.0f;              // nT
    float ae_ = 0.0f;               // nT
    glm::vec3 sun_ = glm::vec3(1.0f, 0.0f, 0.0f);
    bool dirty_ = true;
};
