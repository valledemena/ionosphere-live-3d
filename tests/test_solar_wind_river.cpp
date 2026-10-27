// =============================================================================
// test_solar_wind_river Ã¢â‚¬â€ Lote 5 / modulo E: rio de viento solar (logica pura).
//
// Test sin GL y sin red: llama a PRODUCCION y comprueba
//   (i)   shuemath (Shue-1998): Dp, r0 pineado, flaring, suelo de Dp y Bz sur;
//   (ii)  swriver: paso por frame, reparto inicial, reciclado y determinismo;
//   (iii) el obstaculo: empuje radial fuera de la cavidad (solo y,z), sin NaN
//         con r=0, y sensibilidad a Bz;
//   (iv)  la rampa de color web (windramp) es finita y sube brillo con densidad.
// =============================================================================
#include "Data/ShueMath.h"
#include "Data/SolarWindRamp.h"
#include "Ionosphere/SolarWindRiverSim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-66s [%s]\n", what, cond ? "OK" : "FAIL");
}

static bool near(double a, double b, double tol) {
    return std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) <= tol;
}

int main() {
    // ---------------------------------------------------------------------
    std::printf("== shuemath: Shue-1998 pineado ==\n");
    {
        const float dp = shuemath::dynamicPressure(5.0f, 400.0f);
        expectTrue("Dp(5 cm^-3, 400 km/s) = 1.33808 nPa", near(dp, 1.33808, 1e-5));
        expectTrue("Dp con densidad negativa = 0",
                   near(shuemath::dynamicPressure(-3.0f, 400.0f), 0.0, 1e-12));

        const float r0 = shuemath::r0(0.0f, dp);
        expectTrue("r0(Bz=0, Dp=1.338) = 10.895 R_E", near(r0, 10.895, 0.01));
        expectTrue("alpha(Bz=0, Dp=1.338) = 0.5841", near(shuemath::alpha(0.0f, dp), 0.5841, 1e-3));

        // theta = 0 es el morro subsolar: r(0) debe ser exactamente r0.
        expectTrue("r(theta=0) == r0", near(shuemath::radius(0.0f, 0.0f, dp), r0, 1e-4));
        const float r60 = shuemath::radius(swriver::kPi / 3.0f, 0.0f, dp);
        const float r90 = shuemath::radius(swriver::kPi / 2.0f, 0.0f, dp);
        expectTrue("r(60 deg) = 12.89 R_E", near(r60, 12.89, 0.02));
        expectTrue("r(90 deg) = 16.33 R_E", near(r90, 16.33, 0.02));
        expectTrue("flaring monotono: r(0) < r(60) < r(90)", r0 < r60 && r60 < r90);

        // Bz sur comprime la cavidad; Dp=0 cae al suelo sin NaN ni inf.
        const float r0s = shuemath::r0(-10.0f, dp);
        expectTrue("Bz=-10 nT reduce r0 (9.37 R_E)", r0s < r0 && near(r0s, 9.37, 0.02));
        expectTrue("Dp=0 usa el suelo de 0.05 nPa",
                   near(shuemath::r0(0.0f, 0.0f), shuemath::r0(0.0f, 0.05f), 1e-6));
        expectTrue("r0(Bz=0,Dp=0) finito y > 0",
                   std::isfinite(shuemath::r0(0.0f, 0.0f)) && shuemath::r0(0.0f, 0.0f) > 0.0f);
        expectTrue("r(theta=179 deg) finito", std::isfinite(
                   shuemath::radius(3.12413936f, 0.0f, dp)));
    }

    // ---------------------------------------------------------------------
    std::printf("== swriver: paso por frame (v*60 declarado) ==\n");
    {
        const float d400 = swriver::stepLength(1.0f, 400.0f);
        expectTrue("stepLength(1 s, 400 km/s) = 3.7671 R_E", near(d400, 3.76707, 1e-4));
        expectTrue("lineal en dt: 0.5 s = mitad",
                   near(swriver::stepLength(0.5f, 400.0f), d400 * 0.5, 1e-5));
        expectTrue("lineal en v: 600 km/s = 1.5x",
                   near(swriver::stepLength(1.0f, 600.0f), d400 * 1.5, 1e-5));
        expectTrue("dt=0 no mueve", swriver::stepLength(0.0f, 400.0f) == 0.0f);
        expectTrue("dt<0 no mueve", swriver::stepLength(-0.5f, 400.0f) == 0.0f);
        expectTrue("v=0 no mueve", swriver::stepLength(1.0f, 0.0f) == 0.0f);
        expectTrue("v<0 no mueve", swriver::stepLength(1.0f, -300.0f) == 0.0f);
    }

    // ---------------------------------------------------------------------
    std::printf("== swriver: reparto inicial de la web ==\n");
    {
        swriver::RiverSim sim;
        const float* p = sim.positions();
        bool xOk = true, rOk = true, finite = true;
        float xMin = 1e9f, xMax = -1e9f, rMax = 0.0f;
        for (int i = 0; i < swriver::kCount; ++i) {
            const float x = p[i * 3], y = p[i * 3 + 1], z = p[i * 3 + 2];
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) finite = false;
            if (x < -25.0f - 1e-4f || x > 18.0f + 1e-4f) xOk = false;
            const float rr = std::sqrt(y * y + z * z);
            if (rr > swriver::kSpawnRadius + 1e-4f) rOk = false;
            xMin = std::min(xMin, x); xMax = std::max(xMax, x);
            rMax = std::max(rMax, rr);
        }
        expectTrue("400 particulas", swriver::kCount == 400);
        expectTrue("todas finitas", finite);
        expectTrue("x en (-25, 18]", xOk);
        expectTrue("radio del chorro <= 12 R_E", rOk);
        expectTrue("el reparto cubre el rango (xMin <= -20, xMax >= 15)", xMin <= -20.0f && xMax >= 15.0f);
        expectTrue("el reparto llena el disco (rMax > 11)", rMax > 11.0f);

        // Determinismo: misma semilla, mismas posiciones.
        swriver::RiverSim a, b;
        bool same = true;
        for (int i = 0; i < swriver::kCount * 3; ++i)
            if (a.positions()[i] != b.positions()[i]) same = false;
        expectTrue("misma semilla -> rio identico", same);

        swriver::RiverSim c;
        c.reset(12345u);
        bool differs = false;
        for (int i = 0; i < swriver::kCount * 3; ++i)
            if (c.positions()[i] != a.positions()[i]) differs = true;
        expectTrue("semilla distinta -> rio distinto", differs);
    }

    // ---------------------------------------------------------------------
    std::printf("== swriver: deriva y reciclado ==\n");
    {
        swriver::RiverSim sim;
        std::vector<float> before(sim.positions(), sim.positions() + swriver::kCount * 3);
        const float dt = 0.1f;
        sim.step(dt, 400.0f, 5.0f, 0.0f);
        const float dist = swriver::stepLength(dt, 400.0f);
        const float* p = sim.positions();
        bool moved = true;
        for (int i = 0; i < swriver::kCount; ++i) {
            // Ninguna nace por debajo de -25, con 0.38 R_E de paso no se recicla
            // ninguna, y el empuje lateral no toca x.
            if (!near(p[i * 3], before[i * 3] - dist, 1e-4)) moved = false;
        }
        expectTrue("x avanza -stepLength en todas", moved);
        expectTrue("stepLength(0.1 s, 400) = 0.3767 R_E", near(dist, 0.376707, 1e-5));

        std::vector<float> frozen(sim.positions(), sim.positions() + swriver::kCount * 3);
        sim.step(0.0f, 400.0f, 5.0f, 0.0f);
        bool unchanged = true;
        for (int i = 0; i < swriver::kCount * 3; ++i)
            if (sim.positions()[i] != frozen[i]) unchanged = false;
        expectTrue("dt=0 deja el rio quieto", unchanged);

        // Reciclado: una particula pasada la cola vuelve a nacer en el lado dia.
        sim.setParticle(0, swriver::kRecycleX - 0.01f, 30.0f, -20.0f);
        sim.step(0.1f, 400.0f, 5.0f, 0.0f);
        const float rx = sim.positions()[0];
        const float ry = sim.positions()[1];
        const float rz = sim.positions()[2];
        expectTrue("x < -28 se recicla dentro del rango de nacimiento",
                   rx >= -25.0f - 1e-4f && rx <= 18.0f + 1e-4f);
        expectTrue("al reciclar el radio vuelve a <= 12 R_E",
                   std::sqrt(ry * ry + rz * rz) <= swriver::kSpawnRadius + 1e-4f);

        // Post-condiciones tras 200 frames (3.3 s simulados a 500 km/s).
        swriver::RiverSim run;
        for (int f = 0; f < 200; ++f) run.step(1.0f / 60.0f, 500.0f, 8.0f, -8.0f);
        const float* q = run.positions();
        bool finite = true, inRange = true;
        for (int i = 0; i < swriver::kCount; ++i) {
            for (int k = 0; k < 3; ++k)
                if (!std::isfinite(q[i * 3 + k])) finite = false;
            if (q[i * 3] < swriver::kRecycleX - 1e-3f) inRange = false;
        }
        expectTrue("200 frames: todo finito", finite);
        expectTrue("200 frames: ninguna pasa de x = -28 sin reciclarse", inRange);
    }

    // ---------------------------------------------------------------------
    std::printf("== obstaculo: empuje fuera de la magnetopausa ==\n");
    {
        const float dp = shuemath::dynamicPressure(5.0f, 400.0f);
        expectTrue("empuje > 1 en el morro subsolar (x=6)",
                   swriver::obstaclePush(6.0f, 0.2f, 0.2f, 0.0f, dp) > 1.0f);
        expectTrue("empuje = 1 bien fuera (x=20)",
                   swriver::obstaclePush(20.0f, 0.0f, 0.0f, 0.0f, dp) == 1.0f);
        expectTrue("empuje = 1 con r=0 (sin NaN)",
                   swriver::obstaclePush(0.0f, 0.0f, 0.0f, 0.0f, dp) == 1.0f);
        expectTrue("Bz sur empuja menos (cavidad mas pequena)",
                   swriver::obstaclePush(6.0f, 1.0f, 1.0f, -20.0f, dp) <
                   swriver::obstaclePush(6.0f, 1.0f, 1.0f, 0.0f, dp));

        // El empuje deja la particula justo en el 98% del radio de Shue.
        const float x = 6.0f, y = 0.2f, z = 0.2f;
        const float push = swriver::obstaclePush(x, y, z, 0.0f, dp);
        const float r0 = std::sqrt(x * x + y * y + z * z);
        const float c = std::min(1.0f, std::max(-1.0f, x / r0));
        const float theta = std::acos(c);
        expectTrue("tras el empuje r = 0.98 * r_Shue(theta)",
                   near(r0 * push, shuemath::radius(theta, 0.0f, dp) * swriver::kClearance, 1e-3));
        expectTrue("el empuje coincide con la formula (2.0 aprox)", push > 1.5f && push < 2.5f);
    }

    // ---------------------------------------------------------------------
    std::printf("== rampa de color web ==\n");
    {
        const glm::vec3 slow = windramp::webColor(300.0f, 0.0f, 5.0f, 0.0f);
        const glm::vec3 fast = windramp::webColor(800.0f, 0.0f, 5.0f, 0.0f);
        expectTrue("v=300 y v=800 dan colores distintos",
                   slow.r != fast.r || slow.g != fast.g || slow.b != fast.b);
        const glm::vec3 south = windramp::webColor(800.0f, -15.0f, 5.0f, 0.0f);
        expectTrue("Bz sur tinta el color (distinto de Bz=0)",
                   south.r != fast.r || south.g != fast.g || south.b != fast.b);

        const glm::vec3 dark = windramp::webColor(400.0f, 0.0f, 0.0f, 0.5f);
        const glm::vec3 bright = windramp::webColor(400.0f, 0.0f, 20.0f, 0.5f);
        expectTrue("densidad 20 sube el brillo (x1/0.86)",
                   near(bright.r, dark.r / 0.86, 1e-4) &&
                   near(bright.g, dark.g / 0.86, 1e-4) &&
                   near(bright.b, dark.b / 0.86, 1e-4));
        expectTrue("componentes en [0, 1.02]",
                   bright.r >= 0.0f && bright.r <= 1.02f &&
                   bright.g >= 0.0f && bright.g <= 1.02f &&
                   bright.b >= 0.0f && bright.b <= 1.02f);

        bool finite = true, bounded = true;
        for (int i = 0; i < 64; ++i) {
            const float seed = std::fmod((float)i * 0.61803398875f, 1.0f);
            const glm::vec3 c = windramp::webColor(200.0f + 12.0f * i, 20.0f - 10.0f * i, 0.5f * i, seed);
            if (!std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b)) finite = false;
            if (c.r > 1.02f || c.g > 1.02f || c.b > 1.02f) bounded = false;
        }
        expectTrue("64 combinaciones v/Bz/n: finitas", finite);
        expectTrue("64 combinaciones v/Bz/n: acotadas", bounded);
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
