#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include "Data/TleAdapter.h"

// SatellitesLayer: catalogo TLE vivo dibujado sobre el globo. Port de la capa
// de satelites de la demo web (sky.ts), sin SGP4.
//
// FUENTE: TleAdapter (CelesTrak -> SatNOGS -> cache). App.cpp empuja el
// ultimo snapshot con setSnapshot(); sin snapshot valido NO se dibuja NADA
// (ni puntos ni anillos). La capa no descarga: solo dibuja.
//
// MODELO (declarado, ver OrbitProp.h): elementos medios del TLE + deriva
// secular J2, Kepler por Newton.
//   "visual-grade: TLE mean elements + J2 secular, no SGP4/SDP4;
//    posicion visual, no para tracking".
// Los puntos se propagan a ECEF con el GMST del instante y se pasan a la
// convencion del motor. Los anillos se muestrean en el marco INERCIAL con los
// elementos propagados al instante actual (refresco como mucho cada 10 min) y
// el vertex shader los rota por el GMST de cada frame: el anillo es un
// INSTANTANEO de la orbita, no una traza historica. El radio del anillo usa
// |r| geocentrico (1 R_E = 6371 km); los puntos usan altitud geodetica, asi
// que ambos pueden diferir hasta ~0.3% (invisible).
//
// RENDER: 6 vertices por satelite (billboard de tamano fijo en pantalla, sin
// GL_PROGRAM_POINT_SIZE: no se toca el estado de puntos). Hasta 20000
// satelites; el buffer se reconstruye como mucho 20 veces por segundo
// (kPointStepSec) para no castigar la CPU. Anillos orbitales SOLO para un
// subconjunto determinista del catalogo (stride fijo, max 150).
// render() NO toca blend ni depth: los fija App.cpp (blend aditivo
// SRC_ALPHA/ONE, depth test on, depth mask off).
//
// COLOR POR REGIMEN (altitud media = a - Re):
//   LEO < 2000 km             cian      (0.35, 0.85, 1.00)
//   MEO 2000-30000 km         amarillo  (1.00, 0.88, 0.30)
//   GEO 35786 +/- 2000, e<.05 magenta   (1.00, 0.35, 0.95)
//   resto (HEO)               naranja   (1.00, 0.55, 0.15)
// La clasificacion usa la altitud MEDIA (estable), no la instantanea.
//
// Convencion de coordenadas del motor (RefShells/GridLayer/WsprArcs):
//   phi = pi/2 - radians(lat), theta = pi - radians(lon)
//   x = r*sin(phi)*cos(theta), y = r*cos(phi), z = r*sin(phi)*sin(theta)
class SatellitesLayer {
public:
    SatellitesLayer();
    ~SatellitesLayer();

    void init();
    void destroy();

    void setVisible(bool v) { visible_ = v; }
    bool visible() const { return visible_; }

    // Copia el catalogo y marca dirty. Un snapshot invalido no dibuja nada.
    // Llamarlo solo al cambiar de snapshot (TleAdapter::getGeneration); si el
    // catalogo es identico no se copia (solo se anota el instante).
    void setSnapshot(const TleSnapshot& snap, double nowUtc);
    // Recalcula posiciones (como mucho 20 Hz) y el GMST de los anillos.
    void update(double utc);
    void render(const glm::mat4& view, const glm::mat4& projection);

    // Satellites realmente dibujados en el ultimo update().
    int satCount() const { return satCount_; }
    int ringCount() const { return ringCount_; }

private:
    void rebuildPoints();
    void rebuildRings();
    void upload(GLuint vbo, const std::vector<float>& verts);
    static void regimeColor(double meanAltKm, double ecc, float& r, float& g, float& b);

    GLuint pointVao_ = 0, pointVbo_ = 0;
    GLuint ringVao_ = 0, ringVbo_ = 0;
    GLuint shader_ = 0;
    GLint uView_ = -1, uProj_ = -1;
    GLint uPointSize_ = -1, uGmst_ = -1, uRing_ = -1;

    bool visible_ = false;
    bool snapshotValid_ = false;
    bool pointsDirty_ = false;
    bool ringsDirty_ = false;

    double nowUtc_ = 0.0;        // instante del ultimo update
    double lastPointUtc_ = 0.0;  // instante del ultimo rebuild de puntos
    double lastRingUtc_ = 0.0;   // instante del ultimo rebuild de anillos
    float gmst_ = 0.0f;          // GMST del frame, para rotar los anillos

    int satCount_ = 0;
    int ringCount_ = 0;
    int pointVertexCount_ = 0;
    int ringVertexCount_ = 0;

    std::vector<TleSat> sats_;   // copia del catalogo vigente
    std::vector<float> verts_;   // scratch de reconstruccion (sin realloc)
};
