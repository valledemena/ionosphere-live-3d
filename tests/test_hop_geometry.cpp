// =============================================================================
// test_hop_geometry — validación numérica de la geometría de saltos HF
// (src/Utils/HfMath.h, portado de la demo web ya validada visualmente).
//
// Test de lógica pura: sin GL, sin GLFW, sin red (ejecutable en cualquier CI).
// Referencias:
//   - Ancla de libro: D=2511 km con h'=310 km -> despegue ~7.9 grados
//     (valor exacto de la bisección: 7.865, incidencia 70.84 grados).
//   - Límite tangente: elev 0 con h'=310 -> salto máximo ~3897 km.
//   - M3000 de Dudeney con hmF2=310 -> 2.8327.
//
// Parte de la Fase 0 (condición 5 de la revisión MUSE): M5 añadirá
// test_model_foF2 y M4 test_getbest_parse con fixture real.
// =============================================================================
#include "Utils/HfMath.h"

#include <cmath>
#include <cstdio>

static int g_failures = 0;

static void expectNear(const char* what, double got, double want, double tol) {
    const bool ok = std::fabs(got - want) <= tol;
    if (!ok) ++g_failures;
    std::printf("  %-44s got %10.4f  want %10.4f  tol %6.3f  [%s]\n",
                what, got, want, tol, ok ? "OK" : "FAIL");
}

static void expectTrue(const char* what, bool cond) {
    if (!cond) ++g_failures;
    std::printf("  %-44s [%s]\n", what, cond ? "OK" : "FAIL");
}

int main() {
    std::printf("[test_hop_geometry] ancla de libro: D=2511 km, h'=310 km\n");
    const double e = hfmath::elevationForHop(2511.0, 310.0);
    expectNear("elevación de despegue (grados)", e, 7.9, 0.15);
    const hfmath::HopGeometry geo = hfmath::hopGeometry(e, 310.0);
    expectNear("salto de ida y vuelta (km)", geo.hopKm, 2511.0, 5.0);
    expectNear("incidencia desde la vertical (grados)", geo.incidenceDeg, 70.844, 0.5);

    std::printf("[test_hop_geometry] monotonicidad y límites\n");
    expectNear("salto a elev 0 (tangente, km)", hfmath::hopGeometry(0.0, 310.0).hopKm, 3896.8, 5.0);
    expectNear("salto a elev 88 (km)", hfmath::hopGeometry(88.0, 310.0).hopKm, 20.55, 1.0);
    expectTrue("el salto decrece al subir la elevación (10 vs 7.865)",
               hfmath::hopGeometry(10.0, 310.0).hopKm < geo.hopKm);
    expectTrue("el salto decrece al subir la elevación (30 vs 10)",
               hfmath::hopGeometry(30.0, 310.0).hopKm < hfmath::hopGeometry(10.0, 310.0).hopKm);
    expectNear("saturación: objetivo inalcanzable -> elev mínima",
               hfmath::elevationForHop(3900.0, 310.0), 1.0, 1.0e-9);
    expectNear("D=3000 h'=310 -> elev (grados)", hfmath::elevationForHop(3000.0, 310.0), 4.611, 0.05);

    std::printf("[test_hop_geometry] M3000 / M(D) (ITU-R P.1240 aproximado)\n");
    expectNear("M3000 Dudeney hmF2=310", hfmath::m3000(310.0), 2.8327, 0.001);
    expectNear("M(D=3000) == M3000", hfmath::mFactor(310.0, 3000.0), hfmath::m3000(310.0), 1.0e-9);
    expectNear("M(2511, hmF2=310)", hfmath::mFactor(310.0, 2511.0), 2.7143, 0.001);
    expectTrue("M(D) crece con D (200 km floor incluido)",
               hfmath::mFactor(310.0, 500.0) < hfmath::mFactor(310.0, 2500.0));

    std::printf("[test_hop_geometry] conteo de saltos\n");
    expectNear("saltos D=2511", hfmath::hopCountForDistance(2511.0), 1, 1.0e-9);
    expectNear("saltos D=5000", hfmath::hopCountForDistance(5000.0), 2, 1.0e-9);
    expectNear("saltos D=7500", hfmath::hopCountForDistance(7500.0), 3, 1.0e-9);
    expectNear("saltos D=12000", hfmath::hopCountForDistance(12000.0), 4, 1.0e-9);
    expectNear("saltos D=19000 (techo 6)", hfmath::hopCountForDistance(19000.0), 6, 1.0e-9);

    if (g_failures == 0) {
        std::printf("[test_hop_geometry] TODOS los checks OK\n");
        return 0;
    }
    std::printf("[test_hop_geometry] %d check(s) FALLARON\n", g_failures);
    return 1;
}
