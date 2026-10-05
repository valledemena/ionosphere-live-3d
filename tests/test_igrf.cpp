// =============================================================================
// test_igrf - M'-1 Faraday IGRF real (ruling 142 sec.4).
//
// Test de logica pura: sin GL, sin red. Llama a PRODUCCION
// (igrf::fieldNed/omegaRad/faradayK + RadioPropagationAdapter::resolveTecSI/
// medianOf/stationOmegaRad).
//
// Pines: (i) ancla Omega 50 uT/10 TECU/100 MHz = 11.8 rad = 678 deg (+-1%);
// (ii) ley 1/f^2 exacta; (iii) Bpar=0 -> Omega=0; (iv) IGRF 3 anclas vs
// referencia grado-13 del MISMO fichero oficial (+-2% |B|, +-2 dip) +
// monotonia del dip; (v) mediana sintetica; (vi) cadena TEC por eslabones.
// Fuente coefs: https://www.ngdc.noaa.gov/IAGA/vmod/coeffs/igrf14coeffs.txt
// (columna 2025.0, recuperado 2026-10-05; testigo g10 = -29350.0 nT).
// Referencia de anclas: paquete publicado ppigrf 2.1.0 (IGRF14.shc oficial,
// grado 13) corrido el 2026-10-05 — FUENTE INDEPENDIENTE del codigo bajo
// test (otra receta y otros coefs efectivos que los embebidos grado-8).
// La calculadora NOAA corre ese mismo modelo grado-13.
// =============================================================================
#include "Utils/Igrf.h"
#include "Data/RadioPropagationAdapter.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-58s [%s]\n", what, cond ? "OK" : "FAIL");
}

int main() {
    std::printf("== igrf: ancla Omega + ley 1/f2 ==\n");
    {
        // 50 uT, 10 TECU = 1e17 m^-2, 100 MHz -> 11.82399 rad = 677.5 deg.
        const double om = igrf::omegaRad(5e-5, 1e17, 100e6);
        expectTrue("ancla 11.824 rad +-1%", fabs(om - 11.82399) / 11.82399 < 0.01);
        const double dg = igrf::omegaDeg(5e-5, 1e17, 100e6);
        expectTrue("ancla 677.5 deg +-1%", fabs(dg - 677.5) / 677.5 < 0.01);
        expectTrue("K ~2.365e4 +-0.1%", fabs(igrf::faradayK() - 2.365e4) / 2.365e4 < 1e-3);
        const double om2 = igrf::omegaRad(5e-5, 1e17, 200e6);
        expectTrue("ley Om(2f)=Om(f)/4 exacta", fabs(om2 - om / 4.0) / om < 1e-12);
        expectTrue("Bpar=0 -> Om=0 exacto", igrf::omegaRad(0.0, 1e17, 100e6) == 0.0);
    }

    std::printf("== igrf: anclas ppigrf-2.1.0 (+-2%% |B|, +-2 dip) ==\n");
    {
        // Valores ppigrf (grado 13 oficial) medidos el 2026-10-05.
        struct Anchor {
            double la, lo, h, f, dip;
        };
        const Anchor a[] = {
            {0.0, 0.0, 0.0, 31835.0, -30.17},
            {51.5, 0.0, 0.0, 49068.0, 66.52},
            {-33.9, 151.2, 0.0, 57011.0, -64.41},
        };
        for (const Anchor& an : a) {
            const igrf::Ned b = igrf::fieldNed(an.la, an.lo, an.h);
            const float f = igrf::modNed(b);
            const float d = igrf::dipDeg(b);
            char what[96];
            std::snprintf(what, sizeof(what), "(%.1f,%.1f) |B| +-2%%", an.la, an.lo);
            expectTrue(what, fabsf(f - (float)an.f) / (float)an.f < 0.02f);
            std::snprintf(what, sizeof(what), "(%.1f,%.1f) dip +-2deg", an.la, an.lo);
            expectTrue(what, fabsf(d - (float)an.dip) < 2.0f);
        }
    }
    {
        // Monotonia del dip con |lat| en el meridiano 0 (produccion).
        const float d20 = igrf::dipDeg(igrf::fieldNed(20.0, 0.0, 0.0));
        const float d60 = igrf::dipDeg(igrf::fieldNed(60.0, 0.0, 0.0));
        const float dm20 = igrf::dipDeg(igrf::fieldNed(-20.0, 0.0, 0.0));
        expectTrue("dip crece al polo N", d60 > d20 && d20 > 0.0f);
        expectTrue("dip sur negativo", dm20 < 0.0f);
    }
    {
        // Anclas de funcion a mano (receta prescrita, verificables en papel):
        // S[2][0](x) = (3x^2-1)/2; S[2][2] = (sqrt3/2)sin^2T; S[3][0] = (5x^3-3x)/2.
        expectTrue("S[2][0](x=0.5) = -0.125",
                   fabs(igrf::schmidtS(2, 0, acos(0.5)) - (-0.125)) < 1e-9);
        expectTrue("S[2][2](sinT=0.6) = (sqrt3/2)*0.36",
                   fabs(igrf::schmidtS(2, 2, asin(0.6)) - (0.8660254037844386 * 0.36)) < 1e-9);
        expectTrue("S[3][0](x=0.5) = -0.4375",
                   fabs(igrf::schmidtS(3, 0, acos(0.5)) - (-0.4375)) < 1e-9);
    }

    std::printf("== igrf: mediana + cadena TEC ==\n");
    {
        expectTrue("mediana impar", RadioPropagationAdapter::medianOf({3.0f, 1.0f, 2.0f}) == 2.0f);
        expectTrue("mediana 1 elem", RadioPropagationAdapter::medianOf({7.0f}) == 7.0f);
        expectTrue("mediana vacia 0", RadioPropagationAdapter::medianOf({}) == 0.0f);
    }
    {
        // Cadena TEC: cada eslabon cae al siguiente sin inventar.
        int rung = -1;
        float tec = RadioPropagationAdapter::resolveTecSI(true, 12.0f, true, 1e12f, rung);
        expectTrue("grid valido -> rung 0 y 12e16", rung == 0 && tec == 12.0f * 1e16f);
        tec = RadioPropagationAdapter::resolveTecSI(false, 0.0f, true, 1e12f, rung);
        expectTrue("sin grid + nmF2 -> rung 1 y Nm*2e5", rung == 1 && tec == 1e12f * 2e5f);
        tec = RadioPropagationAdapter::resolveTecSI(false, 0.0f, false, 0.0f, rung);
        expectTrue("nada -> rung 2 mock 15e16", rung == 2 && tec == 15.0f * 1e16f);
    }

    if (g_failures == 0) {
        std::printf("\ntest_igrf: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_igrf: %d FALLOS de %d checks\n", g_failures, g_checks);
    return 1;
}
