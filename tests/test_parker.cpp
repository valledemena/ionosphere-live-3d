// =============================================================================
// test_parker — Lote 5 / modulo D: espiral de Parker (logica pura).
//
// Test sin GL y sin red: llama a PRODUCCION (namespace parker,
// src/Data/ParkerMath.h) y comprueba el modelo Parker 1958 pineado del demo
// web, con el v_sw real como unico input.
//
// Pines: (i) lag a 1 AU con v=400 (formula exacta, tolerancia 0.5 grados);
// (ii) brazo de 0.05 a 1.5 AU con 65 puntos y phi=0 en la Tierra (1,0);
// (iii) v=0 degenerado sin NaN; (iv) polaridad declarada (0 = toward);
// (v) cruces de sector con 0 = toward, huecos > 2 h ignorados y serie vacia;
// (vi) todo finito para v=300..800 km/s.
// =============================================================================
#include "Data/ParkerMath.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-62s [%s]\n", what, cond ? "OK" : "FAIL");
}

static bool near(double a, double b, double tol) {
    return std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) <= tol;
}

static bool allFinite(const std::vector<parker::Pt>& pts) {
    for (const parker::Pt& p : pts)
        if (!std::isfinite(p.r) || !std::isfinite(p.phi) || !std::isfinite(p.x) ||
            !std::isfinite(p.y))
            return false;
    return true;
}

int main() {
    std::printf("== parker: lag a 1 AU (v=400 km/s) ==\n");
    {
        // Valor exacto por la formula pineada (constantes del demo web).
        const double exact =
            ((1.0 * parker::AU_KM) / 400.0 / 86400.0) / parker::SIDEREAL_ROTATION_DAYS *
            360.0;
        const double lag = parker::spiralLagDeg(400.0, 1.0);
        std::printf("  exact=%.4f deg  spiralLagDeg=%.4f deg\n", exact, lag);
        expectTrue("lag(400,1) == formula exacta (tol 0.5)", near(lag, exact, 0.5));
        expectTrue("lag(400,1) en 61.0..61.8 deg (con las constantes pineadas)",
                   lag > 61.0 && lag < 61.8);
        expectTrue("lag crece al bajar v (v=300 > v=400 > v=800)",
                   parker::spiralLagDeg(300.0, 1.0) > lag &&
                       lag > parker::spiralLagDeg(800.0, 1.0));
        expectTrue("lag escala con r (r=0.5 es la mitad)",
                   near(parker::spiralLagDeg(400.0, 0.5), lag * 0.5, 1e-9));
        expectTrue("SIDEREAL_ROTATION_DAYS == 25.38",
                   parker::SIDEREAL_ROTATION_DAYS == 25.38);
        expectTrue("AU_KM == 1.496e8", parker::AU_KM == 1.496e8);
    }

    std::printf("== parker: brazo espiral (v=400, n=64) ==\n");
    {
        const std::vector<parker::Pt> pts = parker::spiralPoints(400.0, 64);
        expectTrue("65 puntos (n+1)", pts.size() == 65u);
        if (pts.size() == 65u) {
            expectTrue("r[0] == 0.05 AU (tol 1e-12)", near(pts[0].r, 0.05, 1e-12));
            expectTrue("r[64] == 1.5 AU (tol 1e-12)", near(pts[64].r, 1.5, 1e-12));

            // Punto mas cercano a la Tierra (r=1): phi ~ 0, (x,y) ~ (1,0).
            size_t iEarth = 0;
            for (size_t i = 1; i < pts.size(); ++i)
                if (std::fabs(pts[i].r - 1.0) < std::fabs(pts[iEarth].r - 1.0)) iEarth = i;
            const parker::Pt& e = pts[iEarth];
            std::printf(
                "  punto r=%.4f: phi=%.4f deg  x=%.4f  y=%.4f\n", e.r, e.phi, e.x, e.y);
            expectTrue("punto con r~1: |phi| < 0.5 deg", std::fabs(e.phi) < 0.5);
            expectTrue("punto con r~1: x ~ 1 (tol 0.02)", near(e.x, 1.0, 0.02));
            expectTrue("punto con r~1: y ~ 0 (tol 0.02)", near(e.y, 0.0, 0.02));

            // Signo del angulo: positivo dentro de la Tierra, negativo fuera.
            const parker::Pt& inner = pts[0];
            const parker::Pt& outer = pts[64];
            expectTrue("phi > 0 hacia dentro (pie del brazo)", inner.phi > 0.0);
            expectTrue("phi < 0 hacia fuera del brazo", outer.phi < 0.0);
            expectTrue("|x|,|y| <= r en todo el brazo", [&] {
                for (const parker::Pt& p : pts)
                    if (std::fabs(p.x) > p.r + 1e-12 || std::fabs(p.y) > p.r + 1e-12)
                        return false;
                return true;
            }());
        }
        expectTrue("todos los puntos finitos", allFinite(pts));
    }

    std::printf("== parker: sin viento (v=0) degenerado sin NaN ==\n");
    {
        expectTrue("lag(0,1) == 0", parker::spiralLagDeg(0.0, 1.0) == 0.0);
        expectTrue("lag(-400,1) == 0", parker::spiralLagDeg(-400.0, 1.0) == 0.0);
        expectTrue("lag(NaN,1) == 0", parker::spiralLagDeg(std::nan(""), 1.0) == 0.0);
        const std::vector<parker::Pt> pts = parker::spiralPoints(0.0, 64);
        expectTrue("65 puntos con v=0", pts.size() == 65u);
        expectTrue("todos finitos con v=0", allFinite(pts));
        bool degenerate = true;
        for (const parker::Pt& p : pts)
            if (p.phi != 0.0 || p.y != 0.0 || !near(p.x, p.r, 1e-12)) degenerate = false;
        expectTrue("v=0: phi=0 e y=0 (brazo sobre +X, sin rotacion)", degenerate);
    }

    std::printf("== parker: polaridad declarada del Bx ==\n");
    {
        expectTrue("sectorPolarity(0) == toward",
                   std::string(parker::sectorPolarity(0.0f)) == "toward");
        expectTrue("sectorPolarity(-3) == away",
                   std::string(parker::sectorPolarity(-3.0f)) == "away");
        expectTrue("sectorPolarity(2.5) == toward",
                   std::string(parker::sectorPolarity(2.5f)) == "toward");
    }

    std::printf("== parker: cruces de sector (cambio de signo real) ==\n");
    {
        using parker::BxSample;
        const std::vector<BxSample> s = {
            {0.0, 3.0f}, {600.0, -2.0f}, {1200.0, 4.0f}, {1800.0, -5.0f}};
        const std::vector<double> c = parker::findCrossings(s);
        expectTrue("serie +,-,+,- -> 3 cruces", c.size() == 3u);
        if (c.size() == 3u) {
            expectTrue("cruces en 600, 1200, 1800 (tiempo de la muestra nueva)",
                       c[0] == 600.0 && c[1] == 1200.0 && c[2] == 1800.0);
            expectTrue("cruces ascendentes", c[0] < c[1] && c[1] < c[2]);
        }

        // Hueco > 2 h: no se puede afirmar la frontera dentro del hueco.
        const std::vector<BxSample> gap = {{0.0, 3.0f}, {7201.0, -2.0f}};
        expectTrue("hueco > 2 h no genera cruce", parker::findCrossings(gap).empty());
        const std::vector<BxSample> edge = {{0.0, 3.0f}, {7200.0, -2.0f}};
        expectTrue("hueco == 2 h si genera cruce",
                   parker::findCrossings(edge).size() == 1u);

        // 0 exacto cuenta como >= 0 (toward): +,0,- -> 1 cruce al pasar a -.
        const std::vector<BxSample> zeros = {
            {0.0, 3.0f}, {600.0, 0.0f}, {1200.0, -3.0f}};
        const std::vector<double> cz = parker::findCrossings(zeros);
        expectTrue("0 = toward: +,0,- -> 1 cruce en 1200",
                   cz.size() == 1u && cz[0] == 1200.0);
        const std::vector<BxSample> allZero = {
            {0.0, 0.0f}, {600.0, 0.0f}, {1200.0, -1.0f}};
        const std::vector<double> ca = parker::findCrossings(allZero);
        expectTrue("0 constante no cruza hasta el primer negativo",
                   ca.size() == 1u && ca[0] == 1200.0);

        expectTrue("serie vacia -> 0 cruces", parker::findCrossings({}).empty());
        const std::vector<BxSample> one = {{42.0, 1.0f}};
        expectTrue("una sola muestra -> 0 cruces", parker::findCrossings(one).empty());
        const std::vector<BxSample> backwards = {{1200.0, 3.0f}, {600.0, -2.0f}};
        expectTrue("tiempos no ascendentes -> 0 cruces",
                   parker::findCrossings(backwards).empty());
    }

    std::printf("== parker: v=300..800 sin NaN/inf ==\n");
    {
        bool finiteLag = true, finitePts = true, boundOk = true;
        for (int v = 300; v <= 800; v += 25) {
            const double lag = parker::spiralLagDeg((double)v, 1.0);
            if (!std::isfinite(lag) || !(lag > 0.0)) finiteLag = false;
            const std::vector<parker::Pt> pts = parker::spiralPoints((double)v, 32);
            if (pts.size() != 33u || !allFinite(pts)) finitePts = false;
            for (const parker::Pt& p : pts)
                if (p.r < 0.05 - 1e-12 || p.r > 1.5 + 1e-12 || std::fabs(p.x) > 1.5 ||
                    std::fabs(p.y) > 1.5)
                    boundOk = false;
        }
        expectTrue("lag finito y > 0 en 300..800 km/s", finiteLag);
        expectTrue("puntos (n=32) finitos en 300..800 km/s", finitePts);
        expectTrue("r en [0.05, 1.5] y |x|,|y| <= 1.5", boundOk);
    }

    if (g_failures == 0) {
        std::printf("\ntest_parker: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_parker: %d FALLOS de %d checks\n", g_failures, g_checks);
    return 1;
}
