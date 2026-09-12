// =============================================================================
// test_sdo_projection — matemática del disco solar (M11, plan §13 verbatim).
//
// Test de logica pura: sin GL, sin GLFW, sin red. Llama a PRODUCCION
// (namespace sundisk en src/Utils/SunDiskMath.h + SolarPosition::
// getApparentEclipticLonDeg), no replica. TDD: compila ANTES que M11a.
//
// Anclas: JD 2000-01-01 12:00 UTC = 2451545.0; L0 Carrington monotono con
// wrap 27.2753 d; B0 acotado ±7.25°; centro->(0,0); inversion E-W
// (Δlon=+45 -> x<0); limbo |Δlon|=90 visible; parseLocation; ultimo-por-Region.
// =============================================================================
#include "Utils/SunDiskMath.h"
#include "Utils/SolarPosition.h"

#include <cmath>
#include <cstdio>

static int g_ok = 0, g_fail = 0;

static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static void check_near(const char* what, double got, double want, double tol) {
    const bool ok = std::fabs(got - want) <= tol;
    if (ok) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s: got %f want %f\n", ok ? "OK" : "FAIL", what, got, want);
}

int main() {
    using namespace sundisk;
    std::printf("--- M11a: JD + L0 Carrington ---\n");
    check_near("JD 2000-01-01 12:00 UTC = 2451545.0", unixToJD(946728000.0), 2451545.0, 1.0e-9);
    const double jd0 = unixToJD(1789000000.0);
    check_bool("L0 en [0,360)", carringtonL0(jd0) >= 0.0 && carringtonL0(jd0) < 360.0);
    check_bool("L0 monotono dia a dia (lejos del wrap)",
               carringtonL0(jd0 + 1.0) > carringtonL0(jd0) ||
               carringtonL0(jd0 + 1.0) < 1.0);  // o cruzo el wrap
    check_near("L0 wrap 27.2753 d (una rotacion exacta)",
               carringtonL0(jd0 + 27.2753), carringtonL0(jd0), 1.0e-6);

    std::printf("--- M11a: B0 acotado ---\n");
    double bmax = 0.0;
    for (int d = 0; d < 360; d += 5) {
        double b = solarB0((double)d);
        if (std::fabs(b) > bmax) bmax = std::fabs(b);
    }
    check_bool("B0 acotado ±7.25", bmax <= 7.25 + 1.0e-9);
    check_near("B0 = 0 en el nodo (λs = 75.77)", solarB0(75.77), 0.0, 1.0e-9);

    std::printf("--- M11a: proyeccion ortografica ---\n");
    {
        SpotProj c = projectSpot(0.0, 0.0, 0.0);
        check_bool("centro visible", c.visible);
        check_near("centro x = 0", c.x, 0.0, 1.0e-12);
        check_near("centro y = 0", c.y, 0.0, 1.0e-12);
    }
    {
        SpotProj e = projectSpot(0.0, 45.0, 0.0);
        check_bool("inversion E-W: Δlon=+45 -> x<0", e.x < 0.0);
        check_near("inversion E-W: |x| = sin45", std::fabs(e.x), 0.7071067811865476, 1.0e-9);
        check_near("ecuador: y = 0", e.y, 0.0, 1.0e-12);
    }
    check_bool("limbo |Δlon|=90 visible", projectSpot(10.0, 90.0, 3.0).visible);
    check_bool("trasera |Δlon|=91 oculta", !projectSpot(10.0, 91.0, 3.0).visible);

    std::printf("--- M11a: parseLocation ---\n");
    {
        double la = 0.0, dl = 0.0;
        check_bool("S12W45 parsea", parseLocation("S12W45", la, dl));
        check_near("S12W45 lat", la, -12.0, 1.0e-12);
        check_near("S12W45 dlon (W negativo)", dl, -45.0, 1.0e-12);
        check_bool("N05E120 parsea", parseLocation("N05E120", la, dl));
        check_near("N05E120 lat", la, 5.0, 1.0e-12);
        check_near("N05E120 dlon", dl, 120.0, 1.0e-12);
        check_bool("S00E00 parsea", parseLocation("S00E00", la, dl));
        check_bool("basura no parsea", !parseLocation("XX", la, dl));
        check_bool("vacio no parsea", !parseLocation("", la, dl));
    }

    std::printf("--- M11a: ultimo-por-Region ---\n");
    {
        std::vector<Sunspot> v = {
            {4441, -12.0, 145.0, true, 0.0, false, "S12W45", 0, 100.0},
            {4441, -13.0, 146.0, true, 0.0, false, "S13W44", 0, 200.0},
            {4442, 5.0, 300.0, true, 0.0, false, "N05E60", 0, 150.0},
        };
        std::map<int, Sunspot> m = latestByRegion(v);
        check_bool("2 regiones", m.size() == 2);
        check_bool("4441 = reporte nuevo", m[4441].timeUtc == 200.0);
        check_bool("4442 intacto", m[4442].lat == 5.0);
    }

    std::printf("--- M11a: λs aparente (SolarPosition) ---\n");
    {
        const double t = 1789000000.0;
        const double a = SolarPosition::getApparentEclipticLonDeg(t);
        check_bool("λs en [0,360)", a >= 0.0 && a < 360.0);
        check_bool("λs determinista",
                   SolarPosition::getApparentEclipticLonDeg(t) == a);
        const double b = SolarPosition::getApparentEclipticLonDeg(t + 86400.0);
        double d = b - a;
        if (d < 0.0) d += 360.0;
        check_near("λs avanza ~0.9856/dia", d, 0.9856, 0.02);
    }

    std::printf("\n[test_sdo_projection] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
