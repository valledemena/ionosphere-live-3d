#pragma once
// =============================================================================
// Lote 5 / modulo B: ventana "Voyager" (Voyager 1 y 2 desde JPL Horizons).
//
// Panel autocontenido, patron SunPanel/SolarImagesPanel/GeospacePanel: App
// pasa el adaptador, el epoch actual, el kick del worker y la visibilidad;
// el panel solo LEE (getSnapshot devuelve copia) y dibuja texto. No sube
// texturas, no toca GL ni el backend de ImGui.
//
// NO DIBUJA NADA EN EL GLOBO: a 172 AU las escalas son absurdas (la Tierra
// quedaria < 1 px y cualquier "linea Voyager" mentiria sobre la distancia).
// La sonda vive en esta ventana, no en la escena 3D.
//
// HONESTIDAD: si el adaptador no tiene rango (sin red, HTTP no-200, tabla sin
// filas en la banda 100-200 AU) se escribe "No Voyager range available" y el
// error. Nunca una distancia sintetica. Si el dato viene de
// cache/voyager.txt se rotula "cached" con su edad; si es una descarga viva,
// "live". El pie declara fuente, hora de comprobacion, edad y el lema
// "No range, no claim - no synthetic distance".
//
// LIMITES DECLARADOS (no son telemetria): Pr ~1e-19 W sale del presupuesto
// Friis con 23 W TX, 3.7 m de antena de nave, 8.42 GHz, DSN de 70 m y
// eficiencias tipicas 0.55/0.65. El tiempo-luz de ida a 172.75 AU es ~23.9 h.
// El barrido del pulso se anima en 10 s y el factor xN (compresion) se
// muestra al lado: se comprime el transito real, no se esconde.
// =============================================================================
#include <atomic>

#include "Data/VoyagerAdapter.h"

struct VoyagerGeom {
    float x = -1.0f, y = -1.0f;  // x<0 = default (centro de la pantalla)
    float w = 470.0f, h = 300.0f;
};

struct VoyagerPanelInput {
    const VoyagerAdapter* adapter = nullptr;  // fuente (obligatorio)
    double utc = 0.0;                         // ahora (edad del dato)
    std::atomic<bool>* pKick = nullptr;       // out: Refresh -> fetch ya
    bool* pVisible = nullptr;                 // inout (X de la ventana)
};

class VoyagerPanel {
public:
    void draw(const VoyagerPanelInput& in, VoyagerGeom& geom);

private:
    bool geomInit_ = false;
};
