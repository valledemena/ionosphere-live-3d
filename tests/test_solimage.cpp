// =============================================================================
// test_solimage -- imagenes solares reales del lote 4 (modulo B).
//
// Puro: sin red, sin GL. Llama a PRODUCCION (SolImageAdapter), no replica.
// Cubre:
//   1. Mapeo producto -> URL (las dos https esperadas, distintas, sin 404
//      conocidos) + nombres/ficheros de cache de cada producto.
//   2. Decodificacion: un cuerpo que no es imagen (basura, HTML, JPEG
//      truncado) devuelve false y NO deja el snapshot valido. Control positivo
//      con un PNG real generado en memoria (stb_image_write).
//   3. Formato del cache en disco en %TEMP%: cabecera de 8 B (epoch uint64
//      little-endian) + cuerpo tal cual, round-trip de escritura/lectura de la
//      marca de tiempo, rechazo de fichero truncado/epoch imposible.
//   4. Etiqueta de fuente ("live"/"cached"/"none") coherente con valid/fromCache.
// =============================================================================
#include "Data/SolImageAdapter.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// stb_image_write solo se usa para el control positivo (PNG 2x2 generado en
// memoria). Sus avisos -Wmissing-field-initializers son del vendor, no del test.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static int g_ok = 0, g_fail = 0;

static void check(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static std::string urlOf(SolProduct p) { return std::string(SolImageAdapter::urlFor(p)); }

int main() {
    const SolProduct kLasco = SolProduct::LascoC2;
    const SolProduct kDrap = SolProduct::DrapGlobal;

    // --- 1. Mapeo producto -> URL ------------------------------------------
    std::printf("--- solimage: URLs de las fuentes ---\n");
    {
        const std::string lasco = urlOf(kLasco);
        const std::string drap = urlOf(kDrap);
        std::printf("  LASCO C2 : %s\n", lasco.c_str());
        std::printf("  D-RAP    : %s\n", drap.c_str());
        check("1: LASCO C2 = latest.jpg 1024 (ESPERADA)",
              lasco == "https://soho.nascom.nasa.gov/data/realtime/c2/1024/latest.jpg");
        check("1b: D-RAP = images/d-rap/global.png (ESPERADA)",
              drap == "https://services.swpc.noaa.gov/images/d-rap/global.png");
        check("1c: las dos URLs son https",
              lasco.rfind("https://", 0) == 0 && drap.rfind("https://", 0) == 0);
        check("1d: productos distintos (URL y nombre)",
              lasco != drap && std::strcmp(SolImageAdapter::nameFor(kLasco),
                                           SolImageAdapter::nameFor(kDrap)) != 0);
        check("1e: ficheros de cache esperados",
              SolImageAdapter::cacheFileName(kLasco) == "sol_lasco_c2.img" &&
                  SolImageAdapter::cacheFileName(kDrap) == "sol_drap_global.img");
        check("1f: atribucion no vacia y distinta",
              std::strlen(SolImageAdapter::sourceNameFor(kLasco)) > 0 &&
                  std::strlen(SolImageAdapter::sourceNameFor(kDrap)) > 0 &&
                  std::strcmp(SolImageAdapter::sourceNameFor(kLasco),
                              SolImageAdapter::sourceNameFor(kDrap)) != 0);
        check("1g: cabecera de cache = 8 B", SolImageAdapter::kCacheHeaderBytes == 8);
    }

    // --- 2. Decodificacion invalida / valida -------------------------------
    std::printf("--- solimage: decodificacion ---\n");
    {
        SolImage img;
        img.valid = true;  // veneno: decodeImage debe limpiarlo
        img.w = 99;
        img.rgb.assign(3, 0x7F);
        const std::vector<unsigned char> garbage = {0x00, 0x01, 0x02, 0x03, 0x04,
                                                    0x05, 0x06, 0x07, 0x08};
        const bool ok = SolImageAdapter::decodeImage(garbage, img);
        check("2: basura -> false", !ok);
        check("2b: basura no deja snapshot valido",
              !img.valid && img.w == 0 && img.h == 0 && img.rgb.empty());

        const std::string html = "<html><body>404 Not Found</body></html>";
        const std::vector<unsigned char> htmlBytes(html.begin(), html.end());
        SolImage h;
        check("2c: HTML -> false y invalido",
              !SolImageAdapter::decodeImage(htmlBytes, h) && !h.valid);

        // JPEG truncado: cabecera SOI + basura (stb no puede cerrar la imagen).
        std::vector<unsigned char> cut = {0xFF, 0xD8, 0xFF, 0xE0};
        cut.resize(64, 0x00);
        SolImage c;
        check("2d: JPEG truncado -> false y invalido",
              !SolImageAdapter::decodeImage(cut, c) && !c.valid);

        SolImage emptyImg;
        check("2e: cuerpo vacio -> false",
              !SolImageAdapter::decodeImage(std::vector<unsigned char>(), emptyImg) &&
                  !emptyImg.valid);

        // Control positivo: PNG 2x2 real generado en memoria (no sintetico en
        // la UI: aqui solo se comprueba que el decodificador acepta un PNG).
        unsigned char pix[2 * 2 * 3] = {255, 0, 0, 0, 255, 0,
                                        0, 0, 255, 255, 255, 255};
        int len = 0;
        unsigned char* png = stbi_write_png_to_mem(pix, 2 * 3, 2, 2, 3, &len);
        check("2f: PNG 2x2 generado (control positivo existe)", png != nullptr && len > 0);
        if (png) {
            const std::vector<unsigned char> bytes(png, png + len);
            STBIW_FREE(png);
            SolImage p;
            const bool pok = SolImageAdapter::decodeImage(bytes, p);
            check("2g: PNG valido -> true, 2x2, RGB8", pok && p.valid && p.w == 2 &&
                                                          p.h == 2 &&
                                                          p.rgb.size() == 12);
            check("2h: pixel (0,0) rojo y (1,1) blanco",
                  pok && p.rgb[0] == 255 && p.rgb[1] == 0 && p.rgb[2] == 0 &&
                      p.rgb[9] == 255 && p.rgb[10] == 255 && p.rgb[11] == 255);
        }
    }

    // --- 3. Formato del cache (round-trip en %TEMP%) ------------------------
    std::printf("--- solimage: cache en disco ---\n");
    {
        std::error_code ec;
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path(ec) / "solimage_test";
        std::filesystem::create_directories(dir, ec);
        const std::string path = (dir / "sol_cache_roundtrip.img").string();
        std::filesystem::remove(path, ec);
        std::printf("  fichero de prueba: %s\n", path.c_str());

        // Cuerpo con bytes JPEG al principio y ceros dentro (se guarda TAL CUAL).
        std::vector<unsigned char> body = {0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x10,
                                           0x4A, 0x46, 0x49, 0x46, 0x00, 0x00};
        body.resize(2048, 0xA5);
        const double epoch = 1789500000.0;  // 2026-09-15 ~22:00 UTC

        std::string err;
        check("3: writeCacheFile ok",
              SolImageAdapter::writeCacheFile(path, body, epoch, &err));
        const auto size = std::filesystem::file_size(path, ec);
        check("3b: fichero = 8 B de cabecera + cuerpo",
              !ec && size == body.size() + SolImageAdapter::kCacheHeaderBytes);

        std::vector<unsigned char> back;
        double ts = 0.0;
        const bool rok = SolImageAdapter::readCacheFile(path, back, ts);
        check("3c: readCacheFile ok", rok);
        check("3d: epoch round-trip exacto", rok && ts == epoch);
        check("3e: cuerpo byte a byte igual", rok && back == body);

        // Cabecera: epoch en uint64 little-endian en los primeros 8 bytes.
        FILE* f = std::fopen(path.c_str(), "rb");
        unsigned char raw[8] = {0};
        const size_t got = f ? std::fread(raw, 1, sizeof(raw), f) : 0;
        if (f) std::fclose(f);
        uint64_t manual = 0;
        for (int i = 0; i < 8; ++i) manual |= (uint64_t)raw[i] << (8 * i);
        check("3f: 8 primeros bytes = epoch LE",
              got == 8 && manual == (uint64_t)epoch);

        // Rechazos: epoch imposible y cuerpo vacio.
        check("3g: epoch 0 rechazado",
              !SolImageAdapter::writeCacheFile(path, body, 0.0, &err));
        check("3h: cuerpo vacio rechazado",
              !SolImageAdapter::writeCacheFile(path, std::vector<unsigned char>(), epoch,
                                               &err));

        // Fichero truncado: solo cabecera (8 B) -> sin cuerpo, lectura falsa.
        std::filesystem::remove(path, ec);
        check("3i: writeCacheFile recreado",
              SolImageAdapter::writeCacheFile(path, body, epoch, &err));
        {
            FILE* ft = std::fopen(path.c_str(), "wb");
            const unsigned char hdrOnly[8] = {1, 0, 0, 0, 0, 0, 0, 0};
            const size_t w8 = ft ? std::fwrite(hdrOnly, 1, 8, ft) : 0;
            if (ft) std::fclose(ft);
            std::vector<unsigned char> dummy;
            double dts = 0.0;
            check("3j: solo cabecera -> lectura false",
                  w8 == 8 && !SolImageAdapter::readCacheFile(path, dummy, dts));
        }
        std::filesystem::remove(path, ec);
        check("3k: fichero borrado", !std::filesystem::exists(path, ec));
    }

    // --- 4. Etiqueta de fuente ---------------------------------------------
    std::printf("--- solimage: etiquetas de fuente ---\n");
    {
        SolImage live;
        live.valid = true;
        live.fromCache = false;
        live.timeUtc = 1789500000.0;
        SolImage cached;
        cached.valid = true;
        cached.fromCache = true;
        cached.timeUtc = 1789490000.0;
        SolImage none;
        check("4: live -> \"live\"", std::strcmp(SolImageAdapter::sourceTag(live), "live") == 0);
        check("4b: fromCache -> \"cached\"",
              std::strcmp(SolImageAdapter::sourceTag(cached), "cached") == 0);
        check("4c: invalida -> \"none\"",
              std::strcmp(SolImageAdapter::sourceTag(none), "none") == 0);
        check("4d: una cacheada nunca se etiqueta live",
              std::strcmp(SolImageAdapter::sourceTag(cached), "live") != 0);
    }

    std::printf("[test_solimage] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
