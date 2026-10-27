#pragma once
// =============================================================================
// Lote 5 / modulo D: ventana "Parker spiral" (panel 2D cenital, sin texturas).
//
// Patron SunPanel/GeospacePanel: App reune los inputs (viento solar REAL ya
// descargado por SolarWindAdapter, Bx vivo, historia de Bx y el epoch) y la
// geometria persistente; el panel solo dibuja con ImGui::GetWindowDrawList().
// No pide red, no sube texturas y no toca el backend de ImGui (solo el
// draw list de su propia ventana).
//
// ESQUEMA DECLARADO (espejo de ParkerPanel.tsx del demo web): vista 2D
// cenital del plano ecliptico, Sol a la izquierda, Tierra FIJA en (1, 0) AU y
// el brazo espiral calculado con el v_sw vivo (foto instantanea, no
// simulacion). Rejilla discreta de 0.5/1.0/1.5 AU. El encuadre AU se ajusta al
// contenido real del brazo (escala 1:1, tope 140 px/AU) para que el esquema
// llene el panel; la geometria del brazo no depende de la ventana.
// NO hay capa 3D: 1 AU no cabe en la vista en radios terrestres (R_E).
//
// HONESTIDAD: sin viento (hasWind=false o speed<=0) se escribe "Waiting for
// live solar wind..." y NO se dibuja espiral. Sin Bx (hasBx=false) el brazo es
// gris y no se declara polaridad como dato. El cruce de sector solo se cita si
// findCrossings devuelve un cruce REAL dentro de 6 h; si no, "no recent sector
// crossing" (negativo declarado: sin cruce no hay aviso).
// =============================================================================
#include <vector>

#include "Data/ParkerMath.h"

struct ParkerGeom {
    float x = -1.0f, y = -1.0f;  // x<0 = default (derecha de la pantalla)
    float w = 520.0f, h = 320.0f;
};

struct ParkerPanelInput {
    float speedKms = 0.0f;  // viento solar real (0 = sin dato)
    float bxNt = 0.0f;
    bool hasBx = false;  // si no, NO se pinta polaridad como dato
    bool hasWind = false;
    const std::vector<parker::BxSample>* bxHistory = nullptr;  // puede ser null
    double utc = 0.0;      // ahora (epoch UTC en s; ventana de 6 h)
    bool* pVisible = nullptr;  // inout (X de la ventana)
};

class ParkerPanel {
public:
    void draw(const ParkerPanelInput& in, ParkerGeom& geom);

private:
    bool geomInit_ = false;
};
