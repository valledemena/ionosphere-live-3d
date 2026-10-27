#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "Data/CmeAdapter.h"

// CmeLayer: frente de choque del CME en transito Sol -> Tierra, alimentado
// por el ETA del modelo ENLIL (CmeAdapter, port de ionosphere-web cme.ts).
//
// FUENTE Y FORMA PINADA: GET https://ccmc.gsfc.nasa.gov/DONKI-API/get/
// WSAEnlilSimulations devuelve un array de simulaciones con simulationID,
// modelCompletionTime, cmeInputs[] (time21_5, speed, halfAngle, latitude,
// longitude, isMostAccurate, CMEID), estimatedShockArrivalTime y link. El
// evento es la ultima simulacion CON ETA y su entrada isMostAccurate.
//
// HONESTIDAD:
//  - ENLIL publica un ETA de llegada, NO un Dp ni un indice de actividad:
//    aqui no se simula plasma, solo se dibuja cuando llega el frente.
//  - El transito real es 1 UA ~ 215 R_E, imposible de encajar en la camara
//    (zoom maximo ~20 R_E). Por eso el radio se COMPRIME con un mapeo
//    LINEAL SOBRE EL PROGRESO, DECLARADO y no fisico:
//        p      = progreso 0..1 entre inicio (time21_5) y ETA
//        rFront = 1.8 + 17.7 * p      -> 1.8 .. 19.5 R_E
//    La UI debe declarar esa compresion.
//  - El frente es un CASQUETE ESFERICO de radio angular halfAngle (el
//    halfAngle publicado por ENLIL, 45 grados si falta) centrado en la
//    direccion del Sol (+axis); se dibujan su circulo limitrofe y 4 arcos
//    meridianos.
//  - Sin evento (hasEvent=false) NO se dibuja nada.
//
// Convencion: coordenadas del motor, el Sol esta en +axis (App.cpp empuja
// la direccion solar con setSunDirection, igual que GeospaceOverlays).
// render() no toca estado de blend ni de depth: lo fija el bloque de
// overlays de App.cpp.
class CmeLayer {
public:
    CmeLayer();
    ~CmeLayer();

    void init();
    void destroy();

    void setVisible(bool v);              // default false
    bool visible() const { return visible_; }

    // Marca dirty solo si cambia lo que se dibuja (ETA, halfAngle, evento).
    void setEvent(const CmeEvent& ev, bool hasEvent);
    // Ignora el vector nulo (direccion aun desconocida).
    void setSunDirection(const glm::vec3& sunWorld);
    void setProgress(float p01);

    // Reconstruye la geometria si algo cambio. Idempotente.
    void update();
    void render(const glm::mat4& view, const glm::mat4& projection);

    // Radio dibujado del frente (R_E comprimidas, 1.8..19.5).
    float frontRadius() const;
    bool hasEvent() const { return hasEvent_; }

private:
    void rebuild();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;

    int axisFirst_ = 0, axisCount_ = 0;
    int frontFirst_ = 0, frontCount_ = 0;

    bool visible_ = false;
    bool hasEvent_ = false;
    bool dirty_ = true;

    CmeEvent ev_;
    glm::vec3 sun_ = glm::vec3(1.0f, 0.0f, 0.0f);
    float progress_ = 0.0f;
};
