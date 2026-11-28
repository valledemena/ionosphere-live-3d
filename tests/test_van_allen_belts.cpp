// DW.19: ley del cinturon externo (pur, sin GL; incluye el header con glad).
#include "Ionosphere/VanAllenBelts.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int g_ok = 0, g_fail = 0;
static void check(const char* name, bool cond) {
    if (cond) ++g_ok; else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", name);
}
static bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

int main() {
    check("DW.19-1: L(0) = 4.2 R_E", near(beltOuterLForKp(0.0f), 4.2f));
    check("DW.19-2: L(5) = 3.8 R_E", near(beltOuterLForKp(5.0f), 3.8f));
    check("DW.19-3: L(9) = 3.48 R_E", near(beltOuterLForKp(9.0f), 3.48f));
    bool mono = true;
    float prev = beltOuterLForKp(0.0f);
    for (int k = 1; k <= 9; ++k) {
        const float v = beltOuterLForKp((float)k);
        if (v >= prev) mono = false;
        prev = v;
    }
    check("DW.19-4: L decrece monotonicamente con Kp 0..9", mono);
    check("DW.19-5: NaN e infinito -> 4.2 (default seguro)",
          near(beltOuterLForKp(std::nanf("")), 4.2f) &&
          near(beltOuterLForKp(std::numeric_limits<float>::infinity()), 4.2f));
    check("DW.19-6: Kp fuera de rango acotado (0..9)",
          near(beltOuterLForKp(-3.0f), 4.2f) &&
          near(beltOuterLForKp(20.0f), 3.48f));
    std::printf("\n[test_van_allen_belts] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
