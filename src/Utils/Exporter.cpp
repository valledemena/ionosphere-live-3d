// Exporter (S-1): implementacion pura. Unica unidad con
// STB_IMAGE_WRITE_IMPLEMENTATION (sin simbolos duplicados).
#include "Utils/Exporter.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "Ionosphere/GridLayer.h"  // GridData (mismo layout)

#define STB_IMAGE_WRITE_IMPLEMENTATION
// Vendored third-party: silencia sus warnings bajo -Wall -Wextra (el TU y el
// espejo compilan con warnings estrictos; el codigo propio sigue limpio).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace exporter {

std::string buildPngName(long long utcEpoch) {
    std::time_t t = (std::time_t)utcEpoch;
    std::tm g = {};
#ifdef _WIN32
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "iono-%04d%02d%02d-%02d%02d%02dZ.png",
                  g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
                  g.tm_hour, g.tm_min, g.tm_sec);
    return std::string(buf);
}

void flipRowsY(std::vector<unsigned char>& px, int w, int h, int comp) {
    if (w <= 0 || h <= 0 || comp <= 0) return;
    if ((int)px.size() < w * h * comp) return;
    const int row = w * comp;
    std::vector<unsigned char> tmp(row);
    for (int y = 0; y < h / 2; ++y) {
        unsigned char* a = px.data() + y * row;
        unsigned char* b = px.data() + (h - 1 - y) * row;
        std::copy(a, a + row, tmp.begin());
        std::copy(b, b + row, a);
        std::copy(tmp.begin(), tmp.end(), b);
    }
}

std::vector<unsigned char> encodePng(int w, int h, int comp, const unsigned char* px) {
    std::vector<unsigned char> out;
    if (w <= 0 || h <= 0 || (comp != 3 && comp != 4) || !px) return out;
    int len = 0;
    unsigned char* mem = stbi_write_png_to_mem(px, w * comp, w, h, comp, &len);
    if (!mem || len <= 0) return out;
    out.assign(mem, mem + len);
    STBIW_FREE(mem);
    return out;
}

std::string gridCsv(const std::string& varName, const GridData& grid) {
    std::string s;
    char head[256];
    std::snprintf(head, sizeof(head), "# var=%s width=%d height=%d\n", varName.c_str(),
                  grid.width, grid.height);
    s += head;
    s += "# layout: values[lat_idx*width+lon_idx], row0=lat-90 south, col0=lon-180\n";
    s += "lat_idx,lon_idx,value,tov_epoch\n";
    const long long tov = (long long)grid.timestamp;
    if (!grid.valid || grid.width <= 0 || grid.height <= 0 ||
        (int)grid.values.size() != grid.width * grid.height) {
        s += "# empty\n";
        return s;
    }
    char row[128];
    for (int la = 0; la < grid.height; ++la) {
        for (int lo = 0; lo < grid.width; ++lo) {
            std::snprintf(row, sizeof(row), "%d,%d,%.9g,%lld\n", la, lo,
                          (double)grid.values[la * grid.width + lo], tov);
            s += row;
        }
    }
    return s;
}

}  // namespace exporter
