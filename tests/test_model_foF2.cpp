// =============================================================================
// test_model_foF2 — validacion del fondo climatologico de M5 (plan 8.2/8.3)
// y de la distancia haversine (8.1).
//
// Test de logica pura: sin GL, sin GLFW, sin red. Llama a PRODUCCION:
// smoothstep/havDeg/dayOfYear/modelFoF2/modelHmF2 (inline de
// src/Ionosphere/LayerProfile.h), densityDir (src/Ionosphere/DensityVolume.h)
// y SolarPosition::getDirection (src/Utils/SolarPosition.cpp) — nada que
// migrar a helper manana.
//
// Esperanzas: computadas con una SEGUNDA implementacion en Python del 8.2
// (transcripcion independiente de la formula) y dayOfYear verificado con
// datetime — nada transcrito a mano (mismo criterio que test_getbest_parse).
//
// Check de convencion (P1, correccion de rumbo de MUSE): el punto subsolar
// derivado de la convencion del MOTOR (densityDir: theta = pi - lon,
// -X = lon 0, ver DensityVolume.h) debe ver cosChi > 0.9 con el Sol de
// PRODUCCION (SolarPosition). Con la convencion opuesta (latLonToUnit:
// +X = lon 0) daria < -0.9 — mediodia del modelo donde el renderer pone
// medianoche, la clase de incoherencia que M2 mato — y el check cazaria
// la regresion.
// =============================================================================
#include "Ionosphere/LayerProfile.h"
#include "Ionosphere/DensityVolume.h"
#include "Utils/SolarPosition.h"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
static int g_checks = 0;

static void expectNear(const char* what, double got, double want, double tol) {
    ++g_checks;
    const bool ok = std::fabs(got - want) <= tol;
    if (!ok) ++g_failures;
    std::printf("  %-52s got %12.5f  want %12.5f  tol %8.5f  [%s]\n",
                what, got, want, tol, ok ? "OK" : "FAIL");
}

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-52s [%s]\n", what, cond ? "OK" : "FAIL");
}

static void expectSame(const char* what, double a, double b) {
    ++g_checks;
    const bool ok = (a == b);
    if (!ok) ++g_failures;
    std::printf("  %-52s %.6f == %.6f  [%s]\n", what, a, b, ok ? "OK" : "FAIL");
}

int main() {
    std::printf("== smoothstep (8.2) ==\n");
    expectNear("smoothstep(0,1,0) = 0", smoothstep(0.0, 1.0, 0.0), 0.0, 1e-12);
    expectNear("smoothstep(0,1,1) = 1", smoothstep(0.0, 1.0, 1.0), 1.0, 1e-12);
    expectTrue("smoothstep(0,1,-0.5) clamp a 0", smoothstep(0.0, 1.0, -0.5) == 0.0);
    expectTrue("smoothstep(0,1,2) clamp a 1", smoothstep(0.0, 1.0, 2.0) == 1.0);
    expectNear("smoothstep(0,1,0.5) = 0.5", smoothstep(0.0, 1.0, 0.5), 0.5, 1e-12);
    expectTrue("smoothstep(55,25,25) borde invertido = 1", smoothstep(55.0, 25.0, 25.0) == 1.0);
    expectNear("smoothstep(55,25,40) = 0.5 (wG)", smoothstep(55.0, 25.0, 40.0), 0.5, 1e-12);
    expectTrue("smoothstep(55,25,55) borde invertido = 0", smoothstep(55.0, 25.0, 55.0) == 0.0);

    std::printf("\n== havDeg (8.1) ==\n");
    expectNear("havDeg meridiano 10 = 10", havDeg(0, 0, 10, 0), 10, 1e-6);
    expectNear("havDeg ecuador 90E = 90", havDeg(0, 0, 0, 90), 90, 1e-6);
    expectNear("havDeg ecuador 90W = 90", havDeg(0, 0, 0, -90), 90, 1e-6);
    expectNear("havDeg antipodal = 180", havDeg(0, 0, 0, 180), 180, 1e-4);
    expectNear("havDeg (45,0)-(45,90) = 60 (identidad)", havDeg(45, 0, 45, 90), 60, 1e-6);
    expectNear("havDeg (30,-70)-(-30,110) = 180", havDeg(30, -70, -30, 110), 180, 1e-4);

    std::printf("\n== dayOfYear (epoch -> doy UTC) ==\n");
    expectNear("dayOfYear(2026-01-01) = 1", (double)dayOfYear(1767225600.0), 1, 0.0);
    expectNear("dayOfYear(2026-03-01) = 60", (double)dayOfYear(1772323200.0), 60, 0.0);
    expectNear("dayOfYear(2026-07-01) = 182", (double)dayOfYear(1782864000.0), 182, 0.0);

    std::printf("\n== modelFoF2 (8.2, aceptacion 1/3) ==\n");
    double f_np = modelFoF2(-0.4, 10.0, 172, 105.0);
    expectTrue("Pacífico nocturno: foF2 en [2.0, 4.5] MHz (acept. 1)", f_np >= 2.0 && f_np <= 4.5);
    expectNear("Pacífico nocturno: valor exacto", f_np, 3.333104615, 0.02);
    double f_npolar = modelFoF2(-0.4, 85.0, 172, 105.0);
    expectTrue("Polo nocturno: foF2 en [1.5, 2.6] MHz", f_npolar >= 1.5 && f_npolar <= 2.6);
    double f_de = modelFoF2(1.0, 0.0, 172, 105.0);
    expectNear("Dia ecuador (valle EA): foF2", f_de, 10.65379634, 0.02);
    double f_dc = modelFoF2(1.0, 16.0, 172, 105.0);
    expectNear("Dia cresta +-16 (anomalia EA): foF2", f_dc, 15.04839768, 0.02);
    expectTrue("Cresta > 1.2 x valle (acept. 3: banda visible)", f_dc > 1.2 * f_de);
    double f_w = modelFoF2(0.8, 60.0, 355, 105.0);
    double f_s = modelFoF2(0.8, 60.0, 172, 105.0);
    expectTrue("N invierno (doy 355) < N verano (doy 172)", f_w < f_s);
    expectNear("ratio invierno/verano (termino estacional)", f_w / f_s, 0.9230783477, 0.001);
    double f70 = modelFoF2(0.5, 30.0, 100, 70.0);
    double f200 = modelFoF2(0.5, 30.0, 100, 200.0);
    expectNear("ratio f107 200/70 (sensibilidad actividad)", f200 / f70, 1.634819533, 0.001);
    expectSame("f107=10 == f107=60 (clamp bajo)", modelFoF2(0.5, 30.0, 100, 10.0), modelFoF2(0.5, 30.0, 100, 60.0));
    expectSame("f107=999 == f107=220 (clamp alto)", modelFoF2(0.5, 30.0, 100, 999.0), modelFoF2(0.5, 30.0, 100, 220.0));
    expectTrue("crepusculo: fo(0.3) > fo(0.0)", modelFoF2(0.3, 25.0, 172, 105.0) > modelFoF2(0.0, 25.0, 172, 105.0));
    expectTrue("crepusculo: fo(0.0) > fo(-0.3)", modelFoF2(0.0, 25.0, 172, 105.0) > modelFoF2(-0.3, 25.0, 172, 105.0));

    std::printf("\n== modelHmF2 (8.2) ==\n");
    expectNear("hmF2 dia lat 0 = 250", modelHmF2(1.0, 0.0), 250, 1e-9);
    expectNear("hmF2 dia lat 60 = 229", modelHmF2(1.0, 60.0), 229, 1e-9);
    expectNear("hmF2 dia lat 30 = 239.5", modelHmF2(1.0, 30.0), 239.5, 1e-9);
    expectNear("hmF2 noche lat 0 = 300", modelHmF2(-1.0, 0.0), 300, 1e-9);
    expectNear("hmF2 noche lat 60 = 312", modelHmF2(-1.0, 60.0), 312, 1e-9);
    expectNear("hmF2 noche lat 30 = 306", modelHmF2(-1.0, 30.0), 306, 1e-9);
    expectTrue("hmF2 sube de noche (lat 40)", modelHmF2(-0.5, 40.0) > modelHmF2(0.5, 40.0));

    std::printf("\n== convencion subsolar (P1, correccion de MUSE) ==\n");
    {
        const double t = 1772323200.0;   // 2026-03-01 00:00 UTC (doy 60)
        glm::vec3 sd = SolarPosition::getDirection(t);
        const double kPi = 3.14159265358979323846;
        // Inversa de la convencion del MOTOR, escrita aqui a proposito
        // (theta = pi - lon, -X = lon 0; ver DensityVolume.h): si densityDir
        // o SolarPosition giran 180 grados, este check cae.
        double latS = std::asin((double)sd.y) * 180.0 / kPi;
        double thetaS = std::atan2((double)sd.z, (double)sd.x);
        double lonS = (kPi - thetaS) * 180.0 / kPi;
        while (lonS > 180.0) lonS -= 360.0;
        while (lonS < -180.0) lonS += 360.0;
        glm::vec3 n = densityDir((float)latS, (float)lonS);
        // Misma expresion que el caller de M5 (applyDayNight / interpLayerProfiles):
        double cosChi = (double)(n.x * sd.x + n.y * sd.y + n.z * sd.z);
        std::printf("  subsolar: lat %.2f  lon %.2f  cosChi %.4f\n", latS, lonS, cosChi);
        expectTrue("subsolar (densityDir): cosChi > 0.9", cosChi > 0.9);
        double lonA = lonS + 180.0;
        if (lonA > 180.0) lonA -= 360.0;
        glm::vec3 na = densityDir((float)(-latS), (float)lonA);
        double cosChiA = (double)(na.x * sd.x + na.y * sd.y + na.z * sd.z);
        expectTrue("antipoda subsolar: cosChi < -0.9 (noche cerrada)", cosChiA < -0.9);
        expectTrue("modelFoF2 en subsolar > 8 MHz (dia claro)", modelFoF2(cosChi, latS, 60, 105.0) > 8.0);
    }

    if (g_failures == 0) {
        std::printf("\ntest_model_foF2: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_model_foF2: %d FALLOS de %d checks\n", g_failures, g_checks);
    return 1;
}
