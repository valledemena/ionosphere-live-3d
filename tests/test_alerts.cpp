// =============================================================================
// test_alerts — S-5 alertas de propagacion (ruling 130 sec.6.5).
//
// Test de logica pura: sin GL, sin red. Llama a PRODUCCION
// (alerts::Engine, src/Data/Alerts.h — motor de reglas sobre bundles).
//
// Pines: 4 umbrales del catalogo (rojo tras 2 muestras + verde/ambar),
// antiparpadeo (1 muestra roja no latchea), dato faltante (!ok -> OFF, no
// alerta), historia insuficiente (mediana no computable -> OFF declarado),
// cola de eventos acotada y semaforo global = peor regla.
// =============================================================================
#include "Data/Alerts.h"

#include <cstdio>
#include <string>

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-58s [%s]\n", what, cond ? "OK" : "FAIL");
}

static alerts::Input base(double utc) {
    alerts::Input in;
    in.utc = utc;
    in.kpOk = true;
    in.kp = 2.0f;
    in.bzOk = true;
    in.bz = 1.0f;
    in.xrayOk = true;
    in.xrayClass = 'B';
    in.mufOk = true;
    in.muf = 20.0f;
    return in;
}

// Siembra historia MUF suficiente: 6 muestras a 20 MHz en 4 h.
static void seedMuf(alerts::Engine& e, double t0) {
    for (int i = 0; i < 6; ++i) {
        alerts::Input in = base(t0 + i * 3600.0 * 4.0 / 5.0);
        e.evaluate(in);
    }
}

int main() {
    std::printf("== alerts: umbrales del catalogo ==\n");
    {
        alerts::Engine e;
        seedMuf(e, 1000000.0);
        expectTrue("base todo GREEN", e.overall() == alerts::GREEN);
        alerts::Input a = base(2000000.0);
        a.kp = 5.0f;
        e.evaluate(a);  // 1a muestra roja: aun no latchea
        expectTrue("Kp 1 muestra -> no RED", e.level(alerts::KP) != alerts::RED);
        e.evaluate(a);  // 2a seguida: rojo
        expectTrue("Kp 2 seguidas -> RED", e.level(alerts::KP) == alerts::RED);
        expectTrue("global RED", e.overall() == alerts::RED);
        alerts::Input b = base(2000100.0);
        b.kp = 3.0f;
        e.evaluate(b);
        expectTrue("Kp 3.0 -> GREEN", e.level(alerts::KP) == alerts::GREEN);
    }
    {
        alerts::Engine e;
        seedMuf(e, 1000000.0);
        alerts::Input a = base(2000000.0);
        a.bz = -11.0f;
        e.evaluate(a);
        e.evaluate(a);
        expectTrue("Bz -11 -> RED", e.level(alerts::BZ) == alerts::RED);
        alerts::Input b = base(2000100.0);
        b.bz = 0.0f;
        e.evaluate(b);
        expectTrue("Bz 0 -> GREEN", e.level(alerts::BZ) == alerts::GREEN);
        alerts::Input c = base(2000200.0);
        c.bz = -7.0f;
        e.evaluate(c);
        expectTrue("Bz -7 -> AMBER", e.level(alerts::BZ) == alerts::AMBER);
    }
    {
        alerts::Engine e;
        seedMuf(e, 1000000.0);
        alerts::Input a = base(2000000.0);
        a.xrayClass = 'M';
        e.evaluate(a);
        e.evaluate(a);
        expectTrue("X-ray M -> RED", e.level(alerts::XRAY) == alerts::RED);
        alerts::Input b = base(2000100.0);
        b.xrayClass = 'C';
        e.evaluate(b);
        expectTrue("X-ray C -> AMBER", e.level(alerts::XRAY) == alerts::AMBER);
    }
    {
        alerts::Engine e;
        seedMuf(e, 1910000.0);  // mediana 20 MHz (ventana <24 h de las muestras)
        alerts::Input a = base(1930000.0);
        a.muf = 14.0f;  // caida 30%
        e.evaluate(a);
        e.evaluate(a);
        expectTrue("MUF -30% -> RED", e.level(alerts::MUF) == alerts::RED);
        alerts::Input b = base(1930100.0);
        b.muf = 17.0f;  // caida 15%
        e.evaluate(b);
        expectTrue("MUF -15% -> AMBER", e.level(alerts::MUF) == alerts::AMBER);
        alerts::Input c = base(1930200.0);
        c.muf = 20.0f;
        e.evaluate(c);
        expectTrue("MUF sin caida -> GREEN", e.level(alerts::MUF) == alerts::GREEN);
    }

    std::printf("== alerts: dato faltante e historia insuficiente ==\n");
    {
        alerts::Engine e;
        seedMuf(e, 1000000.0);
        alerts::Input a = base(2000000.0);
        a.bzOk = false;
        a.bz = -50.0f;  // valor basura: sin ok NO debe alertar
        e.evaluate(a);
        e.evaluate(a);
        expectTrue("Bz ausente -> OFF (no alerta)", e.level(alerts::BZ) == alerts::OFF);
        expectTrue("global sigue GREEN", e.overall() == alerts::GREEN);
    }
    {
        alerts::Engine e;  // sin historia MUF
        alerts::Input a = base(1000000.0);
        e.evaluate(a);
        expectTrue("MUF sin historia -> OFF", e.level(alerts::MUF) == alerts::OFF);
        alerts::Input b = base(1000600.0);  // 2a muestra, 10 min: aun insuficiente
        e.evaluate(b);
        expectTrue("MUF 2 muestras -> OFF", e.level(alerts::MUF) == alerts::OFF);
    }

    std::printf("== alerts: cola de eventos ==\n");
    {
        alerts::Engine e;
        seedMuf(e, 1000000.0);
        const size_t n0 = e.events().size();
        alerts::Input a = base(2000000.0);
        a.kp = 5.0f;
        e.evaluate(a);
        e.evaluate(a);
        expectTrue("evento registrado al enrojecer", e.events().size() > n0);
        const alerts::Event& ev = e.events().back();
        expectTrue("evento Kp ...->RED", ev.rule == alerts::KP && ev.to == alerts::RED);
        expectTrue("cola acotada a 32", e.events().size() <= 32);
    }

    if (g_failures == 0) {
        std::printf("\ntest_alerts: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_alerts: %d FALLOS de %d checks\n", g_failures, g_checks);
    return 1;
}
