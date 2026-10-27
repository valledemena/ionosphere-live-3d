#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include "Data/StormAdapter.h"

// WhistlerLayer: sprites de tormenta + trayectorias de whistler canalizado,
// portado de la demo web (ionosphere-web, lightning.ts::whistlerPath).
//
// FUENTE HONESTA (declarado, sin recuentos inventados):
//   - NWS alerts es la UNICA fuente honesta de esta capa: los poligonos de
//     "Severe Thunderstorm Warning" dan celdas geolocalizadas sobre CONUS.
//   - NO hay recuentos de flashes: los granulos GLM-L2-LCFA (NetCDF-4) son
//     NetCDF y quedan FUERA DE ALCANCE en este motor (sin HDF5 local no hay
//     localizacion legible), asi que la capa no afirma actividad electrica
//     medida ni "rayos por minuto". Solo dibuja celdas de aviso.
//   - Cobertura SOLO CONUS: no es un mapa global de tormentas (GLM cubre
//     Americas, pero la fuente aqui no).
//   - El centroide de cada celda es la MEDIA SIMPLE del anillo del poligono
//     (declarado simple, igual que polyCentroid de la web), no el centroide
//     de area ni el centro del aviso: es una posicion de sprite, no geografia
//     exacta del aviso.
//
// WHISTLER (Helliwell 1965, "Whistlers and Related Ionospheric Phenomena"):
// los whistlers se canalizan por ductos a lo largo de las lineas del campo
// geomagnetico. Por eso la trayectoria dibujada es la LINEA DE CAMPO DIPOLAR
// REAL que pasa por la celda de tormenta (IGRF-14 grado 1, epoca 2025.0
// CONGELADA, con los mismos g10/g11/h11 que MagnetosphereLayer), con
// r = L*cos^2(lambda) en el meridiano magnetico del propio punto. NO es una
// animacion, ni un arco decorativo, ni una trayectoria de propagacion
// simulada: es la geometria del ducto, sin pulso viajero.
//
// Geometria (1 unidad = 1 R_E = 6371 km), dos grupos GL_LINES y un shader:
//   (a) Marcador sprite por celda: estrella de 3 ejes (6 segmentos
//       centro->punta en +-X, +-Y, +-Z del mundo) de semitamano 0.022 R_E,
//       sobre el centroide del aviso a r = 1.005. Color (1.00, 0.55, 0.95),
//       alpha 0.95.
//   (b) Trayectoria por celda: 48 segmentos (49 muestras) de la linea de
//       campo dipolar real. Color (0.72, 0.45, 1.00), alpha 0.50.
// Maximo 24 celdas dibujadas (recorte declarado, coherente con StormAdapter).
//
// Convencion de coordenadas del motor (RefShells/GeospaceOverlays):
//   phi = pi/2 - radians(lat), theta = pi - radians(lon)
//   x = r*sin(phi)*cos(theta), y = r*cos(phi), z = r*sin(phi)*sin(theta)
// y marco web Y-up (wx, wy, wz) -> motor = (-wx, wy, -wz).
//
// render() NO toca estado de blend/depth: lo pone el llamante (blend aditivo
// SRC_ALPHA, ONE; depth test on; depth mask off), asi que aColor.a es brillo.
// Sin datos propios ni red: App.cpp empuja las celdas desde StormAdapter.
class WhistlerLayer {
public:
    WhistlerLayer();
    ~WhistlerLayer();

    void init();
    void destroy();

    void setVisible(bool v);              // por defecto false
    bool visible() const;

    // Marca dirty solo si cambia el numero de celdas o sus ids (patron de
    // clave de rebuild de GeospaceOverlays/MagnetosphereLayer).
    void setCells(const std::vector<StormCell>& cells);

    void update();                        // reconstruye si esta sucio
    void render(const glm::mat4& view, const glm::mat4& projection);

    int cellCount() const;                // celdas dibujadas (<= 24)

private:
    void rebuild();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;

    int markerFirst_ = 0, markerCount_ = 0;
    int pathFirst_ = 0, pathCount_ = 0;

    bool visible_ = false;
    bool dirty_ = true;

    std::vector<StormCell> cells_;
};
