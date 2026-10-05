// =============================================================================
// test_exporter — S-1 exportacion (ruling 130 sec.6.1).
//
// Test de logica pura: sin GL, sin red. Llama a PRODUCCION
// (exporter::buildPngName/flipRowsY/encodePng/gridCsv, src/Utils/Exporter.*).
//
// Pines: nombre determinista UTC exacto, flip-Y byte-exacto, CSV exacto sobre
// bundle sintetico 2x2 (cabecera + filas con timestamp del dato), caso vacio
// declarado, y PNG round-trip (encode -> decode dimensiones + pixeles).
// =============================================================================
#include "Utils/Exporter.h"
#include "Ionosphere/GridLayer.h"  // GridData (definicion completa)

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-58s [%s]\n", what, cond ? "OK" : "FAIL");
}

static void expectSame(const char* what, const std::string& a, const std::string& b) {
    ++g_checks;
    const bool ok = (a == b);
    if (!ok) {
        ++g_failures;
        std::printf("  %-58s [FAIL]\n    got:\n%s\n    want:\n%s\n", what, a.c_str(), b.c_str());
    } else {
        std::printf("  %-58s [OK]\n", what);
    }
}

int main() {
    std::printf("== exporter: nombre determinista UTC ==\n");
    {
        // 1790855100 = 2026-10-01T11:45:00Z (TOV real visto en logs IRTAM).
        expectSame("epoch fijo -> iono-20261001-114500Z.png",
                   exporter::buildPngName(1790855100LL), "iono-20261001-114500Z.png");
        expectSame("epoch 0 -> iono-19700101-000000Z.png",
                   exporter::buildPngName(0LL), "iono-19700101-000000Z.png");
    }

    std::printf("== exporter: flip-Y byte-exacto ==\n");
    {
        // 1x2 RGBA: fila0=[1..4], fila1=[5..8] -> [[5..8],[1..4]].
        std::vector<unsigned char> px = {1, 2, 3, 4, 5, 6, 7, 8};
        exporter::flipRowsY(px, 1, 2, 4);
        const std::vector<unsigned char> want = {5, 6, 7, 8, 1, 2, 3, 4};
        expectTrue("1x2 flip invierte filas", px == want);
        // 2x2: doble flip = identidad.
        std::vector<unsigned char> q;
        for (int i = 0; i < 16; ++i) q.push_back((unsigned char)i);
        const std::vector<unsigned char> orig = q;
        exporter::flipRowsY(q, 2, 2, 4);
        exporter::flipRowsY(q, 2, 2, 4);
        expectTrue("2x2 doble flip = identidad", q == orig);
    }

    std::printf("== exporter: CSV exacto sobre bundle sintetico ==\n");
    {
        GridData g;
        g.width = 2;
        g.height = 2;
        g.values = {1.5f, -2.25f, 0.0f, 100.125f};
        g.timestamp = 1790855100.0;
        g.valid = true;
        const std::string want =
            "# var=TEC width=2 height=2\n"
            "# layout: values[lat_idx*width+lon_idx], row0=lat-90 south, col0=lon-180\n"
            "lat_idx,lon_idx,value,tov_epoch\n"
            "0,0,1.5,1790855100\n"
            "0,1,-2.25,1790855100\n"
            "1,0,0,1790855100\n"
            "1,1,100.125,1790855100\n";
        expectSame("CSV 2x2 exacto (timestamp del dato por fila)", exporter::gridCsv("TEC", g), want);
    }
    {
        GridData g;  // invalido: sin dims ni valores.
        const std::string want =
            "# var=TEC width=0 height=0\n"
            "# layout: values[lat_idx*width+lon_idx], row0=lat-90 south, col0=lon-180\n"
            "lat_idx,lon_idx,value,tov_epoch\n"
            "# empty\n";
        expectSame("CSV vacio declarado", exporter::gridCsv("TEC", g), want);
    }

    std::printf("== exporter: PNG round-trip ==\n");
    {
        // 2x2 RGBA patron; encode -> firma PNG + decode dimensiones + pixeles.
        std::vector<unsigned char> px;
        for (int i = 0; i < 16; ++i) px.push_back((unsigned char)(i * 16));
        const std::vector<unsigned char> png = exporter::encodePng(2, 2, 4, px.data());
        const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        bool okSig = png.size() > 8;
        for (int i = 0; okSig && i < 8; ++i) okSig = (png[i] == sig[i]);
        expectTrue("firma PNG de 8 bytes", okSig);
        int w = 0, h = 0, comp = 0;
        unsigned char* dec = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &comp, 4);
        bool okRt = (dec && w == 2 && h == 2);
        for (int i = 0; okRt && i < 16; ++i) okRt = (dec[i] == px[i]);
        if (dec) stbi_image_free(dec);
        expectTrue("round-trip 2x2 byte-exacto", okRt);
        expectTrue("entrada invalida -> vacio",
                   exporter::encodePng(0, 2, 4, px.data()).empty() &&
                       exporter::encodePng(2, 2, 4, nullptr).empty());
    }

    if (g_failures == 0) {
        std::printf("\ntest_exporter: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_exporter: %d FALLOS de %d checks\n", g_failures, g_checks);
    return 1;
}
