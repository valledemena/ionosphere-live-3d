// =============================================================================
// test_volume_jitter - S-3 jitter del punto de entrada (ruling 130 sec.6.3).
//
// Test de logica pura: sin GL, sin red. Llama a PRODUCCION
// (volumejitter::entryOffset, src/Utils/VolumeJitter.h - espejo CPU de la
// formula IGN del shader del volumen).
//
// Pines: valores exactos MEDIDOS con produccion en 3 coords, rango [0,1) en
// barrido 64x64, determinismo (mismos coords -> mismo offset: sin componente
// temporal por construccion) y descorrelacion (los 4096 no son constantes).
// Nota: el pin es sobre el espejo CPU; el shader usa la expresion identica
// (la ultima ulp puede variar por precision GPU y es irrelevante al uso).
// =============================================================================
#include "Utils/VolumeJitter.h"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-58s [%s]\n", what, cond ? "OK" : "FAIL");
}

static void expectSame(const char* what, float a, float b) {
    ++g_checks;
    const bool ok = (a == b);
    if (!ok) ++g_failures;
    std::printf("  %-58s [%s]\n", what, ok ? "OK" : "FAIL");
}

int main() {
    std::printf("== volume_jitter: valores exactos medidos ==\n");
    {
        expectSame("(0,7) -> 0.164884567", volumejitter::entryOffset(0.0f, 7.0f), 0.164884567f);
        expectSame("(37,98) -> 0.921024323", volumejitter::entryOffset(37.0f, 98.0f), 0.921024323f);
        expectSame("(148,371) -> 0.189423084",
                   volumejitter::entryOffset(148.0f, 371.0f), 0.189423084f);
    }

    std::printf("== volume_jitter: rango + determinismo + descorrelacion ==\n");
    {
        bool inRange = true;
        float first = volumejitter::entryOffset(0.0f, 0.0f);
        bool varied = false;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const float v = volumejitter::entryOffset((float)x, (float)y);
                if (!(v >= 0.0f && v < 1.0f)) inRange = false;
                if (v != first) varied = true;
                // Sin componente temporal: repetir da identico.
                if (volumejitter::entryOffset((float)x, (float)y) != v) inRange = false;
            }
        }
        expectTrue("64x64 todo en [0,1)", inRange);
        expectTrue("no constante (descorrelacion espacial)", varied);
    }

    if (g_failures == 0) {
        std::printf("\ntest_volume_jitter: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_volume_jitter: %d FALLOS de %d checks\n", g_failures, g_checks);
    return 1;
}
