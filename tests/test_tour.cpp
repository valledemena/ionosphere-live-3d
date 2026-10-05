// =============================================================================
// test_tour — S-4 tour didactico (ruling 130 sec.6.4).
//
// Test de logica pura: sin GL, sin red. Llama a PRODUCCION
// (tour::count/key/total/eval/advance, src/Utils/Tour.h).
//
// Pines: cola (4 tramos, 44 s, duraciones), curva t->estado en 5 puntos
// (inicio, medio suavizado, frontera, final, mas alla), pausa sin dato,
// textos ES/EN exactos de los 4 tramos (ES completos, sin ingles).
// =============================================================================
#include "Utils/Tour.h"

#include <cmath>
#include <cstdio>
#include <string>

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-58s [%s]\n", what, cond ? "OK" : "FAIL");
}

static void expectSameF(const char* what, float a, float b) {
    ++g_checks;
    const bool ok = (a == b);
    if (!ok) ++g_failures;
    std::printf("  %-58s [%s]\n", what, ok ? "OK" : "FAIL");
}

static void expectSameS(const char* what, const std::string& a, const std::string& b) {
    ++g_checks;
    const bool ok = (a == b);
    if (!ok) {
        ++g_failures;
        std::printf("  %-58s [FAIL]\n    got:  %s\n    want: %s\n", what, a.c_str(), b.c_str());
    } else {
        std::printf("  %-58s [OK]\n", what);
    }
}

int main() {
    std::printf("== tour: cola ==\n");
    {
        expectTrue("4 tramos", tour::count() == 4);
        expectSameF("total 44 s", tour::total(), 44.0f);
        expectSameF("durs 12/12/10/10",
                   tour::key(0).dur + tour::key(1).dur + tour::key(2).dur + tour::key(3).dur,
                   44.0f);
    }

    std::printf("== tour: curva t->estado ==\n");
    {
        tour::State s0 = tour::eval(0.0f);
        expectSameF("t=0 ry=0.0", s0.ry, 0.0f);
        expectSameF("t=0 dist=3.2", s0.dist, 3.2f);
        expectTrue("t=0 idx0 sin done", s0.index == 0 && !s0.done);
        tour::State s6 = tour::eval(6.0f);
        expectSameF("t=6 ry=0.3 (smoothstep 0.5)", s6.ry, 0.3f);
        expectSameF("t=6 dist=3.2", s6.dist, 3.2f);
        tour::State s12 = tour::eval(12.0f);
        expectSameF("t=12 ry=0.6", s12.ry, 0.6f);
        expectTrue("t=12 idx1", s12.index == 1 && !s12.done);
        tour::State s44 = tour::eval(44.0f);
        expectSameF("t=44 ry=3.4", s44.ry, 3.4f);
        expectSameF("t=44 dist=2.8", s44.dist, 2.8f);
        expectTrue("t=44 done", s44.done);
        tour::State s99 = tour::eval(99.0f);
        expectTrue("t=99 sigue done en W4", s99.done && s99.ry == 3.4f && s99.dist == 2.8f);
    }

    std::printf("== tour: pausa sin dato ==\n");
    {
        expectSameF("!ready no avanza", tour::advance(10.0f, 0.5f, false), 10.0f);
        expectSameF("ready avanza dt", tour::advance(10.0f, 0.5f, true), 10.5f);
    }

    std::printf("== tour: textos ES/EN exactos ==\n");
    {
        expectSameS("ES0 terminador",
                    tour::key(0).es,
                    "Terminador: la linea dia-noche. La ionosfera despierta con el Sol: la densidad crece en el lado diurno y decae de noche.");
        expectSameS("EN0 terminator",
                    tour::key(0).en,
                    "Terminator: the day-night line. The ionosphere wakes with the Sun: density grows on the dayside and decays at night.");
        expectSameS("ES1 anomalia",
                    tour::key(1).es,
                    "Anomalia ecuatorial: dos crestas de densidad a +-15 grados del ecuador magnetico, con el valle encima del ecuador.");
        expectSameS("EN1 anomaly",
                    tour::key(1).en,
                    "Equatorial anomaly: two density crests at +-15 degrees of the magnetic equator, with the trough above the equator.");
        expectSameS("ES2 aurora",
                    tour::key(2).es,
                    "Aurora: el ovalo auroral se aviva con la actividad geomagnetica (Kp alto, Bz sur).");
        expectSameS("EN2 aurora",
                    tour::key(2).en,
                    "Aurora: the auroral oval brightens with geomagnetic activity (high Kp, southward Bz).");
        expectSameS("ES3 tormenta",
                    tour::key(3).es,
                    "Tormenta: viento solar rapido y Bz sur perturban la propagacion HF (MUF inestable, bandas cerradas).");
        expectSameS("EN3 storm",
                    tour::key(3).en,
                    "Storm: fast solar wind and southward Bz disturb HF propagation (unstable MUF, closed bands).");
    }

    if (g_failures == 0) {
        std::printf("\ntest_tour: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_tour: %d FALLOS de %d checks\n", g_failures, g_checks);
    return 1;
}
