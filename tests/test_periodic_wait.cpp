// =============================================================================
// test_periodic_wait - C1b/tec-ext: el plazo de la espera entre ciclos del
// backfill periodico (periodicwait::waitNextCycle, la funcion produccion
// que usa App::histPreloadThread).
//
// Hoyo que lo justifica: el commit a80ec06 dejo la espera SIN plazo (solo
// salia por cambio de profundidad o cierre); el ciclo horario dejo de
// correr nunca con la UI quieta, y el CI no lo atrapo porque no
// habia NINGUN test del bucle periodico. Aqui el reloj y el 'step' de espera
// son inyectados: cada paso espera 1 s falsificado (y puede activar cambio
// de profundidad o cierre en un instante dado).
//
// Tres casos del contrato:
//   A) nada cambia       -> espera TODO el plazo (3600 s) y corre el ciclo.
//   B) cambio de prof.   -> sale AL INSTANTE (12 pasos) y corre el ciclo.
//   C) cierre            -> sale (7 pasos) y NO corre el ciclo.
// =============================================================================
#include "Data/PeriodicWait.h"

#include <cstdio>

static int g_ok = 0, g_fail = 0;

static void check_bool(const char* what, bool cond) {
    (cond ? g_ok : g_fail) += 1;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

// Reloj falsificado: llega 1 s por paso (el reloj solo avanza en waitStep,
// como steady_clock de verdad entre slices de cv).
struct FakeClock {
    double t = 0.0;
    double now() const { return t; }
    void step() { t += 1.0; }
};

// Bateria: waitNextCycle(3600) con hooks que disparan eventos en el paso N.
// Devuelve pasos dados y si corrio el ciclo.
struct Run {
    int steps = 0;
    bool runCycle = false;
    double elapsed = 0.0;
};

static Run scenario(int fireDepthAt, int fireStopAt) {
    FakeClock clock;
    int depthVal = 36;
    bool stopped = false;
    Run r;
    r.runCycle = periodicwait::waitNextCycle(
        3600.0,
        [&stopped]() { return stopped; },
        [&depthVal]() { return depthVal; },
        [&]() {
            ++r.steps;
            clock.step();
            if (fireDepthAt > 0 && r.steps == fireDepthAt) depthVal = 144;
            if (fireStopAt > 0 && r.steps == fireStopAt) stopped = true;
        },
        [&clock]() { return clock.now(); });
    r.elapsed = clock.now();
    return r;
}

int main() {
    std::printf("--- A: nada cambia -> espera TODO el plazo (3600 s) ---\n");
    {
        const Run r = scenario(-1, -1);
        check_bool("espera el plazo completo y corre el ciclo",
                   r.runCycle && r.steps == 3600 && r.elapsed == 3600.0);
    }

    std::printf("--- B: cambio de profundidad en el paso 12 -> sale YA ---\n");
    {
        const Run r = scenario(12, -1);
        check_bool("salida inmediata por cambio de profundidad (12 pasos)",
                   r.runCycle && r.steps == 12);
        check_bool("sale bien antes del plazo (no perdio el plazo de 1 h)",
                   r.elapsed < 3600.0);
    }

    std::printf("--- C: cierre en el paso 7 -> sale y NO corre el ciclo ---\n");
    {
        const Run r = scenario(-1, 7);
        check_bool("salida por cierre (7 pasos)", !r.runCycle && r.steps == 7);
    }

    std::printf("\n[test_periodic_wait] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
