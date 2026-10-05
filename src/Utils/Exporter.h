#pragma once
// Exporter (S-1, ruling 130 sec.6.1): evidencia dura exportable.
// TODO logico puro (sin GL, sin red, sin hilos): el hilo GL solo aporta el
// pixel buffer via glReadPixels y llama a estas funciones. Testeado en
// tests/test_exporter.cpp (nombres, flip-Y, CSV exacto, PNG round-trip).
#include <string>
#include <vector>

struct GridData;

namespace exporter {

// Nombre determinista UTC: "iono-YYYYMMDD-HHMMSSZ.png".
std::string buildPngName(long long utcEpoch);

// Flip vertical in-place (glReadPixels entrega bottom-up; PNG es top-down).
void flipRowsY(std::vector<unsigned char>& px, int w, int h, int comp);

// Codifica PNG (stb_image_write, sin chunks de texto). Vacio si entrada invalida.
std::vector<unsigned char> encodePng(int w, int h, int comp, const unsigned char* px);

// CSV de la capa activa. Layout documentado (GridLayer.cpp:269-275):
// values[lat_idx*width+lon_idx], fila 0 = lat -90 (sur), col 0 = lon -180.
// Cabecera + una fila por celda con el timestamp DEL DATO (doctrina rep:
// el dato manda, no el reloj). Valores con %.9g (round-trip exacto float).
// Grid invalido -> cabecera + linea "# empty".
std::string gridCsv(const std::string& varName, const GridData& grid);

}  // namespace exporter
