#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "Data/GiroAdapter.h"   // StationSample (solo struct; sin link)

// Ruling 190 (b): clave del guard que salta el rebuild del volumen cuando
// todos sus inputs estan estaticos. Clave ENUMERADA (el modo de fallo es
// staleness por clave incompleta):
//   - volEpoch: epoch del cursor (live: wall-clock; replay: cursor fijo).
//   - sunDir: vector solar normalizado YA aplicado al build (bits exactos).
//   - f107: F10.7 del snapshot que lee el HUD (mismo que el build).
//   - flare: multiplicador de fulguracion aplicado (mismo que el build).
//   - samplesFp: huella FNV-1a de TODA la fuente de muestras (tamano +
//     por estacion: code, lat/lon, foE/foF1/foF2, hmF2, nmF2, B0, B1, fmin,
//     timeUtc, valid, stale, cs + historia completa: el replay-cache
//     interpola snapshots y su serie tambien alimenta gsAct).
//   - sourceBits: bit0 kc2gMode, bit1 replayMode, bit2 replayCache no vacia
//     (el SELECTOR de fuente tambien es input: mismo contenido por otra
//     via no garantiza mismo gsAct).
// Comparacion por igualdad EXACTA de bits (deteccion de cambio, no fisica).
struct VolumeBuildKey {
    double volEpoch = 0.0;
    float sunX = 0.0f, sunY = 0.0f, sunZ = 0.0f;
    double f107 = 0.0;
    float flare = 1.0f;
    uint64_t samplesFp = 0;
    unsigned sourceBits = 0;
    bool armed = false;   // false = aun sin build previo: siempre rebuild
};

inline bool volumeKeyEqual(const VolumeBuildKey& a, const VolumeBuildKey& b) {
    if (!a.armed || !b.armed) return false;
    return a.volEpoch == b.volEpoch && a.sunX == b.sunX &&
           a.sunY == b.sunY && a.sunZ == b.sunZ && a.f107 == b.f107 &&
           a.flare == b.flare && a.samplesFp == b.samplesFp &&
           a.sourceBits == b.sourceBits;
}

// FNV-1a 64 sobre los bits de los campos que alimentan interpLayerProfiles
// y el bloque de refshells (ver clave). Puro, determinista, sin estado.
inline uint64_t volumeSamplesFingerprint(const std::vector<StationSample>& ss) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const void* p, size_t n) {
        const unsigned char* b = (const unsigned char*)p;
        for (size_t i = 0; i < n; i++) {
            h ^= b[i];
            h *= 1099511628211ULL;
        }
    };
    auto mixStr = [&](const std::string& s) {
        mix(s.data(), s.size());
        const unsigned char z = 0;
        mix(&z, 1);
    };
    size_t n = ss.size();
    mix(&n, sizeof(n));
    for (const auto& s : ss) {
        mixStr(s.code);
        mix(&s.latDeg, sizeof(float));
        mix(&s.lonDeg, sizeof(float));
        mix(&s.foF2, sizeof(float));
        mix(&s.hmF2, sizeof(float));
        mix(&s.nmF2, sizeof(float));
        mix(&s.foF1, sizeof(float));
        mix(&s.foE, sizeof(float));
        mix(&s.B0, sizeof(float));
        mix(&s.B1, sizeof(float));
        mix(&s.fmin, sizeof(float));
        mix(&s.timeUtc, sizeof(double));
        mix(&s.valid, sizeof(bool));
        mix(&s.stale, sizeof(bool));
        mix(&s.cs, sizeof(int));
        size_t nh = s.history.size();
        mix(&nh, sizeof(nh));
        for (const auto& q : s.history) {
            mix(&q.timeUtc, sizeof(double));
            mix(&q.foF2, sizeof(float));
            mix(&q.hmF2, sizeof(float));
            mix(&q.B0, sizeof(float));
            mix(&q.B1, sizeof(float));
        }
    }
    return h;
}

// Decision del guard: rebuild si la clave cambio o no hay build previo.
inline bool volumeShouldRebuild(const VolumeBuildKey& cur,
                                const VolumeBuildKey& last) {
    if (!last.armed) return true;
    return !volumeKeyEqual(cur, last);
}
