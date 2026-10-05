// =============================================================================
// test_layer_winner — banda de validez F2 del modo Chapman (ruling 124).
//
// Test de logica pura: sin GL, sin GLFW, sin red. Llama a PRODUCCION
// (f2FloorKm + evalNeTotal, inline de src/Ionosphere/LayerProfile.h).
//
// Pines: suelos exactos donde la regla es estructural (early-out hmE,
// NINGUNO -1 sin hasE, cruce E-fuerte == 167.0 medido con produccion),
// winners robustos por margenes amplios, e invariante best
// contra replica independiente del max() viejo (aislacion del gate).
// =============================================================================
#include "Ionosphere/LayerProfile.h"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-58s [%s]\n", what, cond ? "OK" : "FAIL");
}

static void expectSame(const char* what, float a, float b) {
    ++g_checks;
    const bool ok = (a == b);
    if (!ok) ++g_failures;
    std::printf("  %-58s [%s]\n", what, ok ? "OK" : "FAIL");
}

// Replica INDEPENDIENTE del max() viejo (sin gate): lineas 76-109 de
// LayerProfile.h transcritas una a una. Si divergen, este TU cae ruidoso.
static float evalRefMax(const LayerProfile& p, float hKm, float cosChi, int* w) {
    float best = 0.0f;
    int win = -1;
    if (cosChi > 0.0f) {
        float NmD = 1.0e9f * sqrtf(cosChi);
        float v = chapmanAlpha(NmD, 75.0f, 10.0f, hKm);
        if (v > best) { best = v; win = 0; }
    }
    if (p.hasE) {
        float v = chapmanAlpha(p.NmE, p.hmE, 10.0f, hKm);
        if (v > best) { best = v; win = 1; }
    }
    if (p.hasF1) {
        float v = chapmanAlpha(p.NmF1, p.hmF1, 25.0f, hKm);
        if (v > best) { best = v; win = 2; }
    }
    if (p.hasF2) {
        float NeF2 = 0.0f;
        if (hKm <= p.hmF2) {
            float B0 = p.B0 > 0.0f ? p.B0 : 100.0f;
            float x = (p.hmF2 - hKm) / B0;
            const float xmax = 1.6f;
            float xc = x < xmax ? x : xmax;
            float base = p.NmF2 * expf(-powf(xc, p.B1) / coshf(xc));
            if (x > xmax) base *= expf(-(x - xmax) / 1.2f);
            NeF2 = base;
        } else {
            float Htop = 60.0f * (1.0f + (hKm - p.hmF2) / 300.0f);
            NeF2 = p.NmF2 * expf(-(hKm - p.hmF2) / Htop);
        }
        if (NeF2 > best) { best = NeF2; win = 3; }
    }
    if (w) *w = win;
    return best;
}

static LayerProfile dayNoF1() {
    LayerProfile p;
    p.NmE = 1.116e11f; p.hasE = true;
    p.NmF2 = 1.24e12f; p.hmF2 = 300.0f; p.B0 = 100.0f; p.B1 = 3.0f; p.hasF2 = true;
    return p;
}

int main() {
    std::printf("== f2FloorKm: casos estructurales ==\n");
    {
        LayerProfile p = dayNoF1();
        expectSame("sumergido B0=100 -> suelo == hmE exacto", f2FloorKm(p), 110.0f);
    }
    {
        LayerProfile p;
        p.NmF2 = 3e11f; p.hmF2 = 350.0f; p.B0 = 120.0f; p.B1 = 3.0f; p.hasF2 = true;
        expectSame("noche sin-E -> NINGUNO (-1) exacto", f2FloorKm(p), -1.0f);
    }
    {
        // Cruce real (E fuerte): el suelo vive estrictamente dentro (hmE, hmF2].
        LayerProfile p;
        p.NmE = 1.8e12f; p.hasE = true;
        p.NmF2 = 1e12f; p.hmF2 = 300.0f; p.B0 = 60.0f; p.B1 = 2.5f; p.hasF2 = true;
        const float f = f2FloorKm(p);
        expectTrue("cruce: hmE < suelo <= hmF2", f > 110.0f && f <= 300.0f);
        // Pin E-fuerte exacto (veredicto 126 sec.4.2): valor MEDIDO con produccion,
        // no rango. Caza el early-out defectuoso (daba 110.0 aqui).
        expectSame("cruce E-fuerte B0=60 -> suelo == 167.0 exacto", f, 167.0f);
    }
    {
        // Continuidad: B0 a +-2 km del mismo perfil -> suelos a pocos km.
        LayerProfile a, b;
        a.NmE = b.NmE = 1.8e12f; a.hasE = b.hasE = true;
        a.NmF2 = b.NmF2 = 1e12f; a.hmF2 = b.hmF2 = 300.0f;
        a.B0 = 58.0f; b.B0 = 62.0f; a.B1 = b.B1 = 2.5f; a.hasF2 = b.hasF2 = true;
        const float fa = f2FloorKm(a), fb = f2FloorKm(b);
        expectTrue("continuidad B0+-2 -> suelos a <=15 km",
                   fa > 110.0f && fb > 110.0f && std::fabs(fa - fb) <= 15.0f);
    }

    std::printf("== winners: dia sin-F1 (75->D, 110->E, 200/300->F2) ==\n");
    {
        LayerProfile p = dayNoF1();
        const float cosChi = 0.9f;
        int w = -9;
        evalNeTotal(p, 75.0f, cosChi, 1.0f, &w);
        expectTrue("75 dia -> D", w == 0);
        evalNeTotal(p, 110.0f, cosChi, 1.0f, &w);
        expectTrue("110 dia -> E", w == 1);
        evalNeTotal(p, 200.0f, cosChi, 1.0f, &w);
        expectTrue("200 dia -> F2", w == 3);
        evalNeTotal(p, 300.0f, cosChi, 1.0f, &w);
        expectTrue("300 dia -> F2", w == 3);
    }

    std::printf("== winners: dia E-sumergido B0=100 (60->D, 100->E) ==\n");
    {
        LayerProfile p = dayNoF1();
        p.NmF1 = 3.1e11f; p.hmF1 = 240.0f; p.hasF1 = true;
        const float cosChi = 0.9f;
        int w = -9;
        expectSame("suelo == hmE (early-out)", f2FloorKm(p), 110.0f);
        evalNeTotal(p, 60.0f, cosChi, 1.0f, &w);
        expectTrue("60 dia -> D", w == 0);
        evalNeTotal(p, 100.0f, cosChi, 1.0f, &w);
        expectTrue("100 dia -> E", w == 1);
    }

    std::printf("== winners: noche (todo F2) y dia sin-E (F2 a 60) ==\n");
    {
        LayerProfile p;
        p.NmF2 = 3e11f; p.hmF2 = 350.0f; p.B0 = 120.0f; p.B1 = 3.0f; p.hasF2 = true;
        int w = -9;
        evalNeTotal(p, 60.0f, -0.5f, 1.0f, &w);
        expectTrue("noche 60 -> F2", w == 3);
        evalNeTotal(p, 300.0f, -0.5f, 1.0f, &w);
        expectTrue("noche 300 -> F2", w == 3);
    }
    {
        LayerProfile p = dayNoF1();
        p.hasE = false;
        int w = -9;
        expectSame("sin-E -> NINGUNO", f2FloorKm(p), -1.0f);
        evalNeTotal(p, 60.0f, 0.9f, 1.0f, &w);
        expectTrue("dia sin-E 60 -> F2", w == 3);
    }

    std::printf("== invariante best: replica viejo-max bit-identico ==\n");
    {
        LayerProfile ps[4];
        ps[0] = dayNoF1();
        ps[1] = dayNoF1();
        ps[1].NmF1 = 3.1e11f; ps[1].hmF1 = 240.0f; ps[1].hasF1 = true;
        ps[2].NmF2 = 3e11f; ps[2].hmF2 = 350.0f; ps[2].B0 = 120.0f; ps[2].B1 = 3.0f; ps[2].hasF2 = true;
        ps[3] = dayNoF1();
        ps[3].hasE = false;
        const float hs[] = {60.0f, 80.0f, 100.0f, 120.0f, 150.0f, 180.0f,
                            200.0f, 250.0f, 300.0f, 400.0f, 500.0f, 700.0f};
        for (int pi = 0; pi < 4; pi++) {
            for (unsigned hi = 0; hi < sizeof(hs) / sizeof(hs[0]); hi++) {
                const float cc = (pi == 2) ? -0.5f : 0.9f;
                int w1 = -9, w2 = -9;
                const float a = evalNeTotal(ps[pi], hs[hi], cc, 1.0f, &w1);
                const float b = evalRefMax(ps[pi], hs[hi], cc, &w2);
                if (!(a == b)) {
                    ++g_checks;
                    ++g_failures;
                    std::printf("  FAIL best perfil %d h=%.0f\n", pi, hs[hi]);
                } else {
                    ++g_checks;
                }
            }
        }
        std::printf("  barrido best 4 perfiles x 12 alturas: bit-identico\n");
    }

    if (g_failures == 0) {
        std::printf("\ntest_layer_winner: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_layer_winner: %d FALLOS de %d checks\n", g_failures, g_checks);
    return 1;
}
