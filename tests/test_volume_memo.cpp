// =============================================================================
// test_volume_memo — ruling 190 (a): equivalencia naive vs memoizado.
//
// Llama a PRODUCCION (buildDensityVolume con tabla de suelos por columna +
// evalNeTotal naive como referencia). Compara data[] y layer[] celda a celda
// EXACTA (sin epsilon: no bit-exacto = wiring bug). >=2 combos epoch/set
// (aqui 3: dia early-out, cruce E-fuerte con flare, noche sin-E).
// Mide ademas naive-vs-memo en ms (evidencia tabla antes/despues del §4).
// Logica pura: sin GL, sin GLFW, sin red.
// =============================================================================
#include "Ionosphere/DensityVolume.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-58s [%s]\n", what, cond ? "OK" : "FAIL");
}

// Referencia naive: replica el bucle de buildDensityVolume 1:1 pero con
// evalNeTotal (f2FloorKm por celda). Solo cambia la VIA del suelo.
static DensityVolume buildNaive(const std::vector<LayerProfile>& profiles,
                                int lonRes, int latRes, int altRes,
                                float altMinKm, float altMaxKm,
                                float sunX, float sunY, float sunZ,
                                float flareFactor) {
    DensityVolume vol;
    vol.lonRes = lonRes;
    vol.latRes = latRes;
    vol.altRes = altRes;
    vol.altMinKm = altMinKm;
    vol.altMaxKm = altMaxKm;
    vol.data.assign((size_t)lonRes * latRes * altRes, 0.0f);
    vol.layer.assign((size_t)lonRes * latRes * altRes, 0);
    vol.valid = false;
    if ((int)profiles.size() != lonRes * latRes) return vol;
    const float kPi = 3.14159265f;
    for (int latIdx = 0; latIdx < latRes; latIdx++) {
        float latDeg = -90.0f + 180.0f * (latIdx + 0.5f) / latRes;
        float phi = (90.0f - latDeg) * kPi / 180.0f;
        for (int lonIdx = 0; lonIdx < lonRes; lonIdx++) {
            float lonDeg = -180.0f + 360.0f * (lonIdx + 0.5f) / lonRes;
            float theta = kPi - lonDeg * kPi / 180.0f;
            float cx = sinf(phi) * cosf(theta);
            float cy = cosf(phi);
            float cz = sinf(phi) * sinf(theta);
            float cosChi = cx * sunX + cy * sunY + cz * sunZ;
            const LayerProfile& p = profiles[(size_t)latIdx * lonRes + lonIdx];
            for (int a = 0; a < altRes; a++) {
                float h = volumeAltKm(a, altRes, altMinKm, altMaxKm);
                int w = -1;
                float ne = evalNeTotal(p, h, cosChi, flareFactor, &w);
                if (w < 0) w = 0;
                if (w > 3) w = 3;
                float l10 = (ne > 0.0f) ? log10f(ne) : 0.0f;
                float n = (l10 - 8.0f) / (12.5f - 8.0f);
                n = std::max(0.0f, std::min(1.0f, n));
                vol.data[((size_t)a * latRes + latIdx) * lonRes + lonIdx] = n;
                vol.layer[((size_t)a * latRes + latIdx) * lonRes + lonIdx] = (unsigned char)w;
            }
        }
    }
    vol.valid = true;
    return vol;
}

// Rejilla 72x72 determinista que mezcla ramas del suelo: columnas dia
// (early-out), cruce E-fuerte (barrido), noche sin-E (NINGUNO) y sin-F2.
static std::vector<LayerProfile> mixedProfiles() {
    std::vector<LayerProfile> out(72 * 72);
    for (int y = 0; y < 72; y++) {
        for (int x = 0; x < 72; x++) {
            LayerProfile p;
            const int kind = (x + y) % 4;
            if (kind == 0) {          // dia E+F1+F2: early-out -> suelo hmE
                p.NmE = 1.116e11f; p.hasE = true;
                p.NmF1 = 3.1e11f; p.hmF1 = 240.0f; p.hasF1 = true;
                p.NmF2 = 1.24e12f; p.hmF2 = 300.0f;
                p.B0 = 100.0f; p.B1 = 3.0f; p.hasF2 = true;
            } else if (kind == 1) {   // cruce E-fuerte: barrido interior
                p.NmE = 1.8e12f; p.hasE = true;
                p.NmF2 = 1e12f; p.hmF2 = 300.0f;
                p.B0 = 60.0f; p.B1 = 2.5f; p.hasF2 = true;
            } else if (kind == 2) {   // noche sin-E: NINGUNO
                p.NmF2 = 3e11f; p.hmF2 = 350.0f;
                p.B0 = 120.0f; p.B1 = 3.0f; p.hasF2 = true;
            } else {                  // sin-F2 (D/E/F1): suelo irrelevante
                p.NmE = 5e10f; p.hasE = true;
                p.NmF1 = 1e11f; p.hmF1 = 180.0f; p.hasF1 = true;
            }
            out[(size_t)y * 72 + x] = p;
        }
    }
    return out;
}

static double nowMs() {
    return (double)std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
           .count() /
           1000.0;
}

static void runCombo(const char* name, const std::vector<LayerProfile>& profs,
                     float sx, float sy, float sz, float flare) {
    char what[96];
    const double t0 = nowMs();
    DensityVolume ref =
        buildNaive(profs, 72, 72, 60, 60.0f, 700.0f, sx, sy, sz, flare);
    const double tNaive = nowMs() - t0;
    const double t1 = nowMs();
    DensityVolume got =
        buildDensityVolume(profs, 72, 72, 60, 60.0f, 700.0f, sx, sy, sz, flare);
    const double tMemo = nowMs() - t1;
    std::snprintf(what, sizeof(what), "%s: valid y tamanos", name);
    expectTrue(what, got.valid && got.data.size() == ref.data.size() &&
                         got.layer.size() == ref.layer.size());
    size_t badData = 0, badLayer = 0;
    for (size_t i = 0; i < ref.data.size(); i++) {
        if (!(got.data[i] == ref.data[i])) badData++;
        if (got.layer[i] != ref.layer[i]) badLayer++;
    }
    std::snprintf(what, sizeof(what), "%s: data bit-exacta (%zu celdas)",
                  name, ref.data.size());
    expectTrue(what, badData == 0);
    std::snprintf(what, sizeof(what), "%s: layer bit-exacta", name);
    expectTrue(what, badLayer == 0);
    std::printf("    naive %.0f ms / memo %.0f ms (x%.1f)\n", tNaive, tMemo,
                tNaive / (tMemo > 0.0 ? tMemo : 1.0));
}

int main() {
    std::printf("== ruling 190 (a): naive vs memoizado bit-exacto ==\n");
    const std::vector<LayerProfile> profs = mixedProfiles();
    // Combo 1: sol ecuatorial diurno, flare 1.
    runCombo("dia flare1", profs, 1.0f, 0.0f, 0.0f, 1.0f);
    // Combo 2: sol inclinado + flare (rama D con flare), otro epoch/set.
    runCombo("tilt flare2.5", profs, 0.3f, 0.5f, 0.2f, 2.5f);
    // Combo 3: noche (cosChi<0 en gran zona) + flare 1.
    runCombo("noche flare1", profs, -0.8f, -0.2f, 0.1f, 1.0f);

    if (g_failures == 0) {
        std::printf("\ntest_volume_memo: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_volume_memo: %d FALLOS de %d checks\n", g_failures,
                g_checks);
    return 1;
}
