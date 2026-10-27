#pragma once
// =============================================================================
// Panel "Geospace": datos de las tres capas externas del lote 3 del port web.
//
// A diferencia de las ventanas de diagnostico (Sun, Circuit...), este panel no
// dibuja escena: resume lo que las capas externas han traido de la red y lo
// que aun no ha llegado. Es el equivalente del panel de Radio Propagation,
// pero para WSPR / ENLIL / avisos NWS.
//
// CONTENIDO (todo de snapshots reales; nunca valores sinteticos):
//   - WSPR: tabla por banda (spots + SNR medio), con el mismo color de banda
//     que los arcos (WsprArcs::bandColor) y una sparkline del numero de spots
//     muestreada cada 60 s mientras el panel esta abierto.
//   - CME: evento ENLIL vigente (cuenta atras, velocidad, semiangulo, Kp) con
//     barra de progreso del transito y el radio dibujado. Debajo, el historico
//     del boletin (CmeSnapshot::sims): modelCompletionTime, ETA y velocidad.
//   - Tormentas: celdas de aviso NWS (CONUS) con su fin previsto.
//
// HONESTIDAD: el panel no rellena huecos. Si un adaptador no ha respondido o
// la fuente no publica evento, se muestra la nota del snapshot ("sin ETA",
// "sin avisos", error de red) y la fila desaparece; no hay placeholders con
// pinta de dato. La sparkline solo acumula lo observado en esta sesion: sus
// primeros puntos son historia parcial, no una serie historica de la fuente.
//
// Mismo patron que SunPanel: App reune los punteros a los snapshots y la
// geometria persistente; el panel solo dibuja.
// =============================================================================
#include <deque>

#include "Data/CmeAdapter.h"
#include "Data/FlareAdapter.h"
#include "Data/RbeAdapter.h"
#include "Data/StormAdapter.h"
#include "Data/WsprAdapter.h"

struct GeospaceGeom {
    float x = -1.0f, y = -1.0f;  // x<0 = default (bajo el panel Sun)
    float w = 430.0f, h = 470.0f;
};

struct GeospacePanelInput {
    const WsprSnapshot* wspr = nullptr;
    const CmeSnapshot* cme = nullptr;
    const StormSnapshot* storm = nullptr;
    const FlareSnapshot* flares = nullptr;   // Lote 4: DONKI FLR (72 h)
    const RbeSnapshot* rbe = nullptr;        // Lote 5: DONKI RBE (58 d)
    double utc = 0.0;                    // instante de referencia (edad, TTL)
    bool wsprFetching = false;
    bool cmeFetching = false;
    bool stormFetching = false;
    bool flaresFetching = false;
    bool rbeFetching = false;
    float cmeProgress = 0.0f;            // 0..1 del transito (CmeAdapter::cmeProgress)
    float cmeFrontR = 0.0f;              // R_E dibujadas por CmeLayer (comprimidas)
    bool* pWsprVisible = nullptr;        // inout: mismas casillas del menu Geospace
    bool* pCmeVisible = nullptr;
    bool* pStormVisible = nullptr;
    bool* pVisible = nullptr;            // inout (X de la ventana)
};

class GeospacePanel {
public:
    void draw(const GeospacePanelInput& in, GeospaceGeom& geom);

private:
    struct HistPoint {
        double utc = 0.0;
        int spots = 0;
    };
    bool geomInit_ = false;
    double histLast_ = 0.0;
    std::deque<HistPoint> hist_;   // muestreo 60 s, tope 120 puntos (~2 h)
};
