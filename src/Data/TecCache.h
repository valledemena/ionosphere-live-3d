#pragma once
// =============================================================================
// M10: cache TEC en disco — frame binario + barrido de directorio.
//
// Modulo PURO: solo std (cstdio/cstdint/string/vector/filesystem). Sin curl,
// sin GL, sin App. Testeable en cualquier CI (tests/test_tec_cache.cpp).
//
// Formato por frame ("tec_<epoch_s>.bin", endianness nativo — cache local):
//   u32 magic ("TCC1") | u32 version | f64 epoch | i32 w | i32 h | w*h f32
// =============================================================================
#include <cstdint>
#include <string>
#include <vector>

namespace teccache {

struct Frame {
    double epoch = 0.0;          // segundos UTC (== GridData::timestamp)
    int w = 0, h = 0;
    std::vector<float> v;        // w*h, row-major como GridData::values
};

inline constexpr uint32_t kMagic = 0x54434331u;  // "TCC1"
inline constexpr uint32_t kVersion = 1u;
inline constexpr int kCapFrames = 432;           // mismo cap que tecHist (TEC-ext 034)
inline constexpr double kHistWindowSec = 259200.0;
    // 72 h — 432 x 10 min (sondeo Q-TEC-1, 0D632DA2)

// Gate de escritura (twin-testeable): simulated JAMAS en cache.
bool shouldCacheFrame(bool valid, bool simulated, bool enabled);

// Current-eligible: edad <= maxAgeH. Viejos pueblan replay, no current.
bool isFresh(double epoch, double nowUtc, double maxAgeH);

std::string frameName(double epoch);   // "tec_<s>.bin"
bool ensureDir(const std::string& dir);
bool saveFrame(const std::string& dir, const Frame& f);  // atomica tmp+rename
bool loadFrame(const std::string& path, Frame& out);     // false si corrupto

struct ScanResult {
    std::vector<Frame> frames;  // orden asc por epoch, ya podado a cap
    int pruned = 0;             // borrados por cap (los mas viejos)
    int dropped = 0;            // corruptos borrados (self-heal)
};

// Barrido completo: crea dir si falta, carga validos, borra corruptos,
// poda a cap. Nunca lanza.
ScanResult loadCached(const std::string& dir);

// Delta de precarga: entradas del indice mas nuevas que max(cached).
// Cache vacio -> todo el indice.
std::vector<double> missingSince(const std::vector<double>& cachedEpochs,
                                 const std::vector<double>& indexEpochs);

// Delta de precarga por VENTANA (TEC-ext 034): entradas del indice con
// epoch >= nowUtc - kHistWindowSec ausentes en cachedEpochs (dedup exacto:
// ambas poblaciones derivan del mismo time_tag). Orden ASC (fetch
// oldest-first); si faltan > cap se conservan los cap MAS NUEVOS (el anillo
// borra los viejos — no se pide lo que se pierde). Puro.
std::vector<std::size_t> missingInWindow(const std::vector<double>& cachedEpochs,
                                         const std::vector<double>& indexEpochs,
                                         double nowUtc, std::size_t cap);

}  // namespace teccache
