// =============================================================================
// test_m2_sun — guardián de regresión del invariante M2 (el sol sigue al dato).
// (src/Utils/SolarPosition.cpp, verificado contra física: 15.04 deg/h).
//
// Test de lógica pura: sin GL, sin GLFW, sin red (ejecutable en cualquier CI).
// Cubre lo que M2 cablea en App::update (epoch efectivo = replayEpoch en
// replay, wall-clock en LIVE): la física que ese cableado alimenta.
//   - Pureza: mismo epoch -> mismo vector (LIVE idéntico pre/post-M2).
//   - Replay -3 h -> el sol rota ~45 deg (aceptación 2 de M2: 44.80 medido).
//   - Continuidad: 1 s apenas mueve el sol (el único "salto" legítimo es el
//     snap intencional al conmutar live<->replay, que también sigue al dato).
//   - Norma: vector unitario.
//
// Promovido de TEMP (sesión de validación M2, 2026-09-08).
// =============================================================================
#include "Utils/SolarPosition.h"

#include <cmath>
#include <cstdio>

static int g_failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) ++g_failures;
    std::printf("  %-44s [%s]\n", what, ok ? "OK" : "FAIL");
}

static double degBetween(const glm::vec3& a, const glm::vec3& b) {
    double d = (double)glm::dot(a, b);
    if (d > 1.0) d = 1.0;
    if (d < -1.0) d = -1.0;
    return std::acos(d) * 180.0 / 3.14159265358979323846;
}

int main() {
    std::printf("[test_m2_sun] invariante M2: el sol sigue al dato\n");
    // Epoch realista (dorada S4, 2026-09-08 ~15:23 UTC).
    const double tLive = 1788873780.0;
    const double tReplay = tLive - 3.0 * 3600.0;

    const glm::vec3 sLive = SolarPosition::getDirection(tLive);
    const glm::vec3 sLive2 = SolarPosition::getDirection(tLive);
    const glm::vec3 sReplay = SolarPosition::getDirection(tReplay);
    std::printf("  sun(LIVE)      = %.6f %.6f %.6f\n", sLive.x, sLive.y, sLive.z);
    std::printf("  sun(REPLAY-3h) = %.6f %.6f %.6f\n", sReplay.x, sReplay.y, sReplay.z);

    check(sLive == sLive2, "pureza (mismo epoch, mismo vector)");
    const double ang = degBetween(sLive, sReplay);
    std::printf("  angulo LIVE vs REPLAY-3h = %.4f deg (esperado ~45)\n", ang);
    check(ang > 40.0 && ang < 50.0, "replay -3h desplaza el sol ~45 deg");
    const double ang1s = degBetween(sLive, SolarPosition::getDirection(tLive + 1.0));
    std::printf("  movimiento en 1 s = %.6f deg\n", ang1s);
    check(ang1s < 0.01, "continuidad por frame");
    check(std::fabs((double)glm::length(sLive) - 1.0) < 1e-5, "vector unitario");

    if (g_failures == 0) std::printf("[test_m2_sun] TODOS los checks OK\n");
    return g_failures;
}
