#pragma once
// =============================================================================
// Lote 4 / modulo B: ventana "Solar imagery" (LASCO C2 + D-RAP global).
//
// Ventana autocontenida (patron SunPanel/GeospacePanel): App solo pasa el
// adaptador, el epoch actual, el kick del worker y la visibilidad; el panel
// pide las imagenes al adaptador, sube sus PROPIAS texturas GL y dibuja.
//
// TEXTURAS: dos texturas propias (glGenTextures/glTexImage2D, GL_RGB8, filtro
// LINEAR, CLAMP_TO_EDGE). Se suben SOLO cuando cambia el sello del producto
// (SolImageAdapter::getLastFetchTime), no en cada frame: el hilo de render no
// paga una copia de 3 MB por frame. Al terminar cada subida se restaura
// glBindTexture(GL_TEXTURE_2D, 0) y el GL_UNPACK_ALIGNMENT previo; no se toca
// ningun otro estado GL ni el backend de ImGui.
//
// HONESTIDAD: si no hay imagen valida se escribe "No image available yet" y el
// error del adaptador; NUNCA se dibuja una imagen sintetica. El caption declara
// fuente, hora UTC del fetch, edad y si el dato es "live" o "cached".
//
// CICLO DE VIDA: este objeto NO borra texturas en el destructor (podria correr
// despues de destruir el contexto GL). App debe llamar a shutdown() con el
// contexto vivo, antes de cerrar la ventana.
// =============================================================================
#include <atomic>

#include "Data/SolImageAdapter.h"

struct SolImagesGeom {
    float x = -1.0f, y = -1.0f;  // x<0 = default (centro de la pantalla)
    float w = 760.0f, h = 430.0f;
};

struct SolarImagesInput {
    const SolImageAdapter* adapter = nullptr;  // fuente (obligatorio)
    double utc = 0.0;                          // ahora (edad de la imagen)
    std::atomic<bool>* pKick = nullptr;        // out: Refresh -> fetch ya
    bool* pVisible = nullptr;                  // inout (X de la ventana)
};

class SolarImagesPanel {
public:
    void draw(const SolarImagesInput& in, SolImagesGeom& geom);
    // Borra las texturas GL (llamar con el contexto GL vivo).
    void shutdown();
    unsigned int textureFor(SolProduct product) const {
        return tex_[product == SolProduct::DrapGlobal ? 1 : 0];
    }

private:
    // Copia las imagenes del adaptador solo si el sello cambio, y sube textura.
    void syncFromAdapter(const SolImageAdapter& adapter);
    void upload(int idx, const SolImage& img);

    SolImage shown_[2];              // ultimo snapshot dibujado (copia propia)
    double stampSeen_[2] = {-1.0, -1.0};  // getLastFetchTime ya procesado
    unsigned int tex_[2] = {0, 0};
    int texW_[2] = {0, 0};
    int texH_[2] = {0, 0};
    bool texValid_[2] = {false, false};
    bool geomInit_ = false;
};
