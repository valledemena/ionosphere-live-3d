#pragma once
// =============================================================================
// M11: ventana "Sun" (disco SDO + regiones activas). Plan §13.4.
//
// Clase UI pequena (patron SidePanel): App reune inputs (textura ya subida,
// spots copiados, producto, X-ray, geometria persistente) y SunPanel dibuja.
// Primer uso de ImGui::Image en el motor (backend intacto, sin cambios).
// =============================================================================
#include <string>
#include <vector>

#include "Utils/SunDiskMath.h"

struct SunGeom {
    float x = -1.0f, y = -1.0f;  // x<0 = default (arriba-derecha)
    float w = 332.0f, h = 470.0f;
};

struct SunPanelInput {
    unsigned int texId = 0;
    const std::vector<sundisk::Sunspot>* spots = nullptr;
    double utc = 0.0;
    int* pProduct = nullptr;        // inout 0=HMIIF 1=A304 (click alterna)
    bool* pVisible = nullptr;       // inout (X de la ventana)
    const char* prodName = "";      // "SDO · HMI (fotosfera)"
    const char* prodTag = "";       // "HMIIF" (caption)
    const char* imgTime = "--:--";  // HH:MM de la imagen mostrada
    const char* xrayText = "--";    // "C2.1" (clase X-ray actual)
};

class SunPanel {
public:
    void draw(const SunPanelInput& in, SunGeom& geom);

private:
    bool geomInit_ = false;
};
