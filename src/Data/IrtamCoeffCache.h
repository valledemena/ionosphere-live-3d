#pragma once
// =============================================================================
// M-IRTAM-F2 (P2): cache numerica gambit-coeffs en disco — familia irtamc_.
//
// Modulo PURO: solo std (cstdio/cstdint/string/vector/filesystem/map).
// Sin curl, sin GL, sin App. Testeable en CI (tests/test_irtamc_cache.cpp).
// Valida contenido con irtamcoeff::parseMessage (P1a) — un bucket corrupto
// se rechaza al guardar y se auto-sana al barrer.
//
// Familia DISTINTA de la GIF (C7): "cache/irtamc_<param>_<epoch>.txt" con los
// BYTES CRUDOS del endpoint + sidecar "<...>.txt.meta" (lastmod/fetch, patron
// F1). Cap kCapBuckets = 384 POR PARAMETRO (= 96 h a cadencia IRTAM 15 min).
// Tamaño ~KBs por bucket → disco irrelevante (R4 de F2 cerrado).
// =============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace irtamccache {

// Nombres declarados para el fold (grep): irtamccache::Bucket,
// irtamccache::saveBucket, irtamccache::loadBucket, irtamccache::loadCached,
// irtamccache::shouldCacheBucket, irtamccache::frameName.

inline constexpr int kCapBuckets = 384;  // por parametro (foF2, hmF2, ...)
inline constexpr std::size_t kMinBucketBytes = 256u;
inline constexpr std::size_t kMaxBucketBytes = 1024u * 1024u;

struct Bucket {
    double epoch = 0.0;  // epoch del fetch (nombre de fichero)
    std::string param;   // saneado: "fof2", "hmf2", ...
    std::string path;
    std::string serverLastMod;  // "" si no hay sidecar (fallback honesto)
    std::vector<uint8_t> bytes;  // crudos del endpoint
    int nCoeffs = 0;             // validados por parse (P1a)
    double tovEpoch = 0.0;       // eco TOV del parse (DATA-age, C3)
};

// Gate rapido (twin-testeable): solo tamaño. El gate semantico (parse) corre
// en saveBucket/loadBucket.
bool shouldCacheBucket(std::size_t sizeBytes);

// "irtamc_<param>_<epoch>.txt" (param saneado [a-z0-9], max 12).
std::string frameName(const std::string& param, double epoch);
bool ensureDir(const std::string& dir);

// Guarda bucket + sidecar .meta (atomico tmp+rename). Valida contenido con
// parseMessage: bucket no-estructural se rechaza (fail-soft). false si gate,
// parse, dir o escritura fallan.
bool saveBucket(const std::string& dir, const std::string& param,
                double epoch, const uint8_t* data, std::size_t size,
                const std::string& serverLastMod);

// Carga y valida (parseMessage). Rellena bytes + lastmod + nCoeffs + tovEpoch.
// false si corrupto (el sweep lo borra: self-heal).
bool loadBucket(const std::string& path, Bucket& out);

struct ScanResult {
    // Ordenado asc por epoch; podado a cap POR PARAMETRO (los nuevos sobreviven).
    std::vector<Bucket> buckets;
    int pruned = 0;   // buckets borrados por cap (los mas viejos de su param)
    int dropped = 0;  // corruptos borrados (self-heal)
};

// F2-B0B1-R1 Q2 (gate por parejas, aditivo; existente intacto): TOV comun
// mas fresco presente en AMBOS params (comparacion exacta de tovEpoch,
// misma convencion que el gate foF2xhmF2). -1 si no hay interseccion.
// Puro (sin I/O): testeable en CI.
double commonTovEpoch(const std::vector<Bucket>& buckets,
                      const std::string& paramA, const std::string& paramB);

// Barrido completo: crea dir si falta, carga validos, borra corruptos, poda
// a cap por parametro. Solo toca irtamc_*.txt(.meta). Nunca lanza.
ScanResult loadCached(const std::string& dir);

}  // namespace irtamccache
