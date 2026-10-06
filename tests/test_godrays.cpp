// =============================================================================
// test_godrays — M'-2 god rays screen-space (ruling 142 sec.5).
//
// Test de logica pura: sin GL, sin red. Llama a PRODUCCION
// (namespace godrays, src/Utils/Godrays.h — espejo CPU del pase).
//
// Pines: (i) proyeccion sol->NDC en 3 casos (delante / detras w<=0 / tras el
// limbo con puerta); (ii) acumulacion: exactamente 12 taps con pesos no
// crecientes; (iii) mascara: corredor con la Tierra en medio -> 0, cielo -> 1;
// (iv) puerta: off-screen -> skip; (v) stepping tapUV (origen + clamp).
// =============================================================================
#include "Utils/Godrays.h"

#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-58s [%s]\n", what, cond ? "OK" : "FAIL");
}

int main() {
    const glm::mat4 proj =
        glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const glm::vec3 cam(0.0f, 0.0f, 3.0f);
    const glm::mat4 view = glm::lookAt(cam, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 vp = proj * view;

    std::printf("== godrays: proyeccion sol->NDC (3 casos) ==\n");
    {
        // Sol de frente (ligeramente al este y arriba, dentro del frustum).
        const godrays::SunProj s =
            godrays::projectSun(vp, cam, glm::normalize(glm::vec3(0.2f, 0.05f, -1.0f)));
        expectTrue("delante: ni behind ni offscreen", !s.behind && !s.offscreen);
        // Sol tras la camara.
        const godrays::SunProj b =
            godrays::projectSun(vp, cam, glm::vec3(0.0f, 0.0f, 1.0f));
        expectTrue("detras: behind (w<=0)", b.behind);
        expectTrue("detras: gate skip", godrays::gateSkip(b, false));
        // Sol de frente pero tras el limbo -> skip por oclusion.
        expectTrue("tras limbo: gate skip",
                   godrays::gateSkip(s, godrays::rayHitsEarth(cam, glm::normalize(glm::vec3(-1.0f, 0.0f, -2.9f)))));
    }

    std::printf("== godrays: 12 taps con decay monotono ==\n");
    {
        expectTrue("NTAPS == 12", godrays::NTAPS == 12);
        bool mono = true;
        for (int i = 1; i < godrays::NTAPS; ++i)
            if (!(godrays::tapWeight(i) <= godrays::tapWeight(i - 1) && godrays::tapWeight(i) > 0.0f))
                mono = false;
        expectTrue("pesos no crecientes y positivos", mono);
        expectTrue("w0 == 1 exacto", godrays::tapWeight(0) == 1.0f);
    }

    std::printf("== godrays: mascara del corredor ==\n");
    {
        const glm::vec3 c(0.0f, 0.0f, 3.0f);
        // Rayo al centro de la Tierra: cruza -> tap 0.
        expectTrue("hacia la Tierra -> 0",
                   godrays::tapMask(c, glm::vec3(0.0f, 0.0f, -1.0f)) == 0.0f);
        // Rayo al cielo: no cruza -> tap 1.
        expectTrue("al cielo -> 1", godrays::tapMask(c, glm::vec3(0.0f, 0.0f, 1.0f)) == 1.0f);
        // Falloff: decrece con la distancia al sol.
        expectTrue("falloff monotono",
                   godrays::falloff(0.0f) > godrays::falloff(0.5f) &&
                       godrays::falloff(0.5f) > godrays::falloff(2.0f));
    }

    std::printf("== godrays: puerta y stepping ==\n");
    {
        godrays::SunProj off;
        off.behind = false;
        off.x = 1.5f;
        off.y = 0.0f;
        off.offscreen = true;
        expectTrue("off-screen -> skip", godrays::gateSkip(off, false));
        const glm::vec2 uv(0.5f, 0.5f), sun(0.9f, 0.5f);
        const glm::vec2 t0 = godrays::tapUV(0, uv, sun, 16.0f / 9.0f);
        expectTrue("tap0 == pixel", fabsf(t0.x - uv.x) < 1e-6f && fabsf(t0.y - uv.y) < 1e-6f);
        const glm::vec2 t11 = godrays::tapUV(11, uv, sun, 16.0f / 9.0f);
        expectTrue("tap11 avanza al sol (mismo eje)",
                   fabsf(t11.y - uv.y) < 1e-6f && t11.x > uv.x && t11.x <= sun.x);
    }

    if (g_failures == 0) {
        std::printf("\ntest_godrays: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_godrays: %d FALLOS de %d checks\n", g_failures, g_checks);
    return 1;
}
