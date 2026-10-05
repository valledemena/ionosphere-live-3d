// =============================================================================
// test_volume_shadow — S-2 sombra dura del planeta (ruling 130 sec.6.2).
//
// Test de logica pura: sin GL, sin red. Llama a PRODUCCION
// (volumeshadow::shadowed, src/Utils/VolumeShadow.h — espejo CPU del test
// rayo-esfera del shader del volumen, misma matematica y misma guarda 1e-4).
//
// Pines: subsolar iluminado, antisolar en sombra, limbo iluminado,
// frontera exacta (t0 == 0 -> luz, como el gate estricto 126) y monotonía
// día->noche a lo largo del ecuador solar.
// =============================================================================
#include "Utils/VolumeShadow.h"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-58s [%s]\n", what, cond ? "OK" : "FAIL");
}

int main() {
    // Marco: sol en +X, esfera interior r=1.00942 (60 km sobre r=1).
    const float Lx = 1.0f, Ly = 0.0f, Lz = 0.0f;
    const float kInner = 1.0f + 60.0f / 6371.0f;

    std::printf("== volume_shadow: casos estructurales ==\n");
    {
        // Subsolar: P sobre el eje del sol -> el planeta queda detras.
        expectTrue("subsolar iluminado",
                   !volumeshadow::shadowed(1.05f * Lx, 0.0f, 0.0f, Lx, Ly, Lz, kInner));
    }
    {
        // Antisolar: P tras el planeta -> el rayo al sol cruza la esfera.
        expectTrue("antisolar en sombra",
                   volumeshadow::shadowed(-1.05f * Lx, 0.0f, 0.0f, Lx, Ly, Lz, kInner));
    }
    {
        // Limbo (terminador geometrico): rayo tangencial, sin interseccion.
        expectTrue("limbo iluminado",
                   !volumeshadow::shadowed(0.0f, 1.05f, 0.0f, Lx, Ly, Lz, kInner));
    }
    {
        // Frontera exacta: P sobre la esfera en el lado nocturno (t0 == 0).
        // La guarda deja la frontera en luz (gate estricto: protege el borde).
        expectTrue("frontera t0==0 -> luz",
                   !volumeshadow::shadowed(-kInner * Lx, 0.0f, 0.0f, Lx, Ly, Lz, kInner));
    }

    std::printf("== volume_shadow: monotonia dia->noche ==\n");
    {
        // A lo largo del ecuador solar solo hay UNA transicion luz->sombra,
        // y cae en el hemisferio nocturno (terminador geometrico a 90 grados).
        int flips = 0;
        bool prev = false;
        bool prevInit = false;
        for (int i = 0; i <= 36; ++i) {
            const float a = (float)i * 3.14159265f / 36.0f;  // 0 = subsolar
            const float px = 1.05f * cosf(a), py = 1.05f * sinf(a);
            const bool s = volumeshadow::shadowed(px, py, 0.0f, Lx, Ly, Lz, kInner);
            if (prevInit && s != prev) ++flips;
            prev = s;
            prevInit = true;
        }
        expectTrue("una sola transicion luz->sombra", flips == 1);
    }

    if (g_failures == 0) {
        std::printf("\ntest_volume_shadow: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_volume_shadow: %d FALLOS de %d checks\n", g_failures, g_checks);
    return 1;
}
