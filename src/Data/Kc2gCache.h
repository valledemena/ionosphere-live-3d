#pragma once
// =============================================================================
// FASE B (P2): cache kc2g en disco — patron TecCache (teccache).
//
// Modulo PURO: solo std (cstdio/cstdint/string/vector/filesystem/map).
// Sin curl, sin GL, sin App. Testeable en CI (tests/test_kc2g_cache.cpp).
//
// Un frame binario por batch ("kc2g_<epoch>.bin", endianness nativo):
//   u32 magic ("KCC1") | u32 version | u32 nrows | filas serializadas
// Fila: code/name (u32 len + bytes), lat/lon f64, foF2/muf/hmF2/foF1/foE/
//   foEs/hF2/hF/hE/hEs/fmin f32, timeUtc f64, valid/stale u8, cs i32.
// SIN history: el sweep la reconstruye ascendente (acumulacion P1).
// =============================================================================
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kc2gcache {

struct Row {
    std::string code, name;
    double latDeg = 0.0, lonDeg = 0.0;
    float foF2 = 0.0f, muf = 0.0f, hmF2 = 0.0f;
    float foF1 = 0.0f, foE = 0.0f, foEs = 0.0f;
    float hF2 = 0.0f, hF = 0.0f, hE = 0.0f, hEs = 0.0f;
    float fmin = 0.0f;
    double timeUtc = 0.0;
    bool valid = false, stale = false;
    int cs = 0;
};

struct Frame {
    double epoch = 0.0;  // epoch del batch (fetch)
    std::vector<Row> rows;
};

inline constexpr uint32_t kMagic = 0x4B434331u;  // "KCC1"
inline constexpr uint32_t kVersion = 1u;
inline constexpr int kCapBatches = 144;

// Gate de escritura (twin-testeable, paridad shouldCacheFrame):
// SOLO batches con >=1 fila panelUsable.
bool shouldCacheBatch(int panelUsable);

std::string frameName(double epoch);   // "kc2g_<s>.bin"
bool ensureDir(const std::string& dir);
bool saveFrame(const std::string& dir, const Frame& f);  // atomica tmp+rename
bool loadFrame(const std::string& path, Frame& out);     // false si corrupto

struct ScanResult {
    // Epoch -> filas, todo ordenado asc por (epoch); podado a cap.
    std::vector<Frame> frames;
    int pruned = 0;   // batches borrados por cap (los mas viejos)
    int dropped = 0;  // corruptos borrados (self-heal)
};

// Barrido completo: crea dir si falta, carga validos, borra corruptos,
// poda a cap. Nunca lanza.
ScanResult loadCached(const std::string& dir);

}  // namespace kc2gcache
