#pragma once
// =============================================================================
// M-IRTAM (F1): cache IRTAM en disco — patron TecCache/Kc2gCache.
//
// Modulo PURO: solo std (cstdio/cstdint/string/vector/filesystem).
// Sin curl, sin GL, sin App. Testeable en CI (tests/test_irtam_cache.cpp).
//
// Guarda BYTES ORIGINALES del .gif (sin re-encode):
//   "cache/irtam_<product>_<epoch>.gif" + sidecar "<...>.gif.meta"
//   (lastmod=<http-date>\nfetch=<epoch>\n — el badge honesto (G7) necesita la
//   edad DEL SERVIDOR, no del fetch).
// scanGif(): walker de bloques puro -> (w,h,nframes) + validacion estructural
// (magic GIF87a/89a, LSD, trailer 0x3B, sin trailing). NO decodifica pixeles
// (el decode del ultimo frame es F1b; R2).
// =============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace irtamcache {

inline constexpr int kCapGifs = 8;                     // ~8 x 3.5 MB peor caso
inline constexpr std::size_t kMinGifBytes = 1024u;     // bajo esto no es un mapa
inline constexpr std::size_t kMaxGifBytes = 16u * 1024u * 1024u;

struct GifInfo {
    int w = 0, h = 0;     // Logical Screen Descriptor
    int nframes = 0;      // descriptores de imagen 0x2C contados
};

// Solo magic (6 B). Nunca lanza.
bool hasGifMagic(const uint8_t* data, std::size_t size);

// Walker completo: valida estructura y rellena info. false si corrupto,
// truncado, sin trailer 0x3B, con trailing, o sin frames. Nunca lanza.
bool scanGif(const uint8_t* data, std::size_t size, GifInfo& out);

// Gate de escritura (twin-testeable): el caller ya valido HTTP 200.
// Acepta solo mapas plausibles (magic + rango de tamano).
bool shouldCacheGif(const uint8_t* data, std::size_t size);

// "irtam_<product>_<epoch>.gif" (product saneado [a-z0-9], max 12).
std::string frameName(const std::string& product, double epoch);
bool ensureDir(const std::string& dir);

// Guarda gif + sidecar .meta (atomico tmp+rename). false si gate/neta falla.
bool saveGif(const std::string& dir, const std::string& product, double epoch,
             const uint8_t* data, std::size_t size,
             const std::string& serverLastMod);

// Carga y valida (scanGif). Rellena bytes + lastmod del sidecar ("" si falta;
// el caller usa fetch-epoch como fallback honesto). false si corrupto.
bool loadGif(const std::string& path, std::vector<uint8_t>& bytesOut,
             std::string& serverLastModOut);

struct Item {
    double epoch = 0.0;  // epoch del fetch (nombre de fichero)
    std::string product;
    std::string path;
    std::string serverLastMod;  // "" si no hay sidecar
    GifInfo info;
    std::size_t bytes = 0;
};

struct ScanResult {
    // Ordenado asc por epoch; podado a cap (los mas nuevos sobreviven).
    std::vector<Item> gifs;
    int pruned = 0;   // gifs borrados por cap (los mas viejos, + su .meta)
    int dropped = 0;  // corruptos borrados (self-heal, + su .meta)
};

// Barrido completo: crea dir si falta, carga validos, borra corruptos,
// poda a cap. Solo toca irtam_*.gif(.meta). Nunca lanza.
ScanResult loadCached(const std::string& dir);

}  // namespace irtamcache
