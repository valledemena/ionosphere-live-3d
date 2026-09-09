// test_d_region.cpp — M7 (plan 10): absorcion D Sauer-Wilkinson + LUF-SID.
// GENERADO por scripts/m7_gen_test.py: esperanzas de una SEGUNDA
// implementacion Python independiente — nada transcrito a mano. Llama a
// PRODUCCION (RadioPropagationAdapter real: deriveLUF / flareAbsorption /
// dRegionAbsorptionDb), no replicas. La referencia 16b6fa5 (formula vieja,
// cos(lat) + cap siempre) es lambda local para la regresion aceptacion 1.
#include "Data/RadioPropagationAdapter.h"
#include <cmath>
#include <cstdio>
#include <ctime>

static int g_ok = 0, g_fail = 0;

static void check_near(const char* name, float got, double want, double tol) {
    double d = fabs((double)got - want);
    if (d <= tol) { g_ok++; printf("[OK]   %s: %.6f == %.6f (tol %.1e)\n", name, got, want, tol); }
    else         { g_fail++; printf("[FAIL] %s: %.6f != %.6f (tol %.1e)\n", name, got, want, tol); }
}
static void check_exact(const char* name, float got, double want) {
    if ((double)got == want) { g_ok++; printf("[OK]   %s: %.6f == %.6f exacto\n", name, got, want); }
    else                     { g_fail++; printf("[FAIL] %s: %.6f != %.6f exacto\n", name, got, want); }
}
static void check_bool(const char* name, bool cond) {
    if (cond) { g_ok++; printf("[OK]   %s\n", name); }
    else      { g_fail++; printf("[FAIL] %s\n", name); }
}

// Referencia 16b6fa5 (pre-M7, heuristica plana): para la delta de calma
// (aceptacion 1). Replica EXACTA del cuerpo viejo de deriveLUF.
static float oldLuf16b6fa5(float muf, float foF2, float latDeg, float lonDeg, double utc) {
    if (muf <= 0.0f) return 0.0f;
    std::time_t t = (std::time_t)utc;
    std::tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    float utcH = (float)tmv.tm_hour + (float)tmv.tm_min / 60.0f;
    float lstH = fmodf(utcH + lonDeg / 15.0f + 24.0f, 24.0f);
    float dayF = fmaxf(0.0f, cosf((lstH - 12.0f) * 3.14159265f / 12.0f));
    float absorb = 1.2f + 2.8f * dayF * fmaxf(0.0f, cosf(latDeg * 3.14159265f / 180.0f));
    float luf = 0.35f * muf + 0.5f * absorb;
    if (foF2 > 0.0f) luf = fminf(luf, fmaxf(2.0f, 0.9f * foF2));
    return fmaxf(2.0f, luf);
}

int main() {
    const double utcA = 1774008000.0;  // 2026-03-20 12:00Z (mediodia lon 0)
    const double utcN = 1774053000.0;  // 2026-03-21 00:30Z (noche lon 0)
    const double utcJ = 1773986400.0;  // 2026-03-20 06:00Z (mediodia en 90E)
    using RPA = RadioPropagationAdapter;

    check_exact("fl1 fondo A F=0", RPA::flareAbsorption(0.0, 1.0), 0.0);
    check_exact("fl2 fondo A F=9.9e-8", RPA::flareAbsorption(9.9e-08, 1.0), 0.0);
    check_exact("fl3 frontera F=1e-7", RPA::flareAbsorption(1e-07, 1.0), 0.0);
    check_near("fl4 B4.1 dayF=0.6", RPA::flareAbsorption(4.2e-07, 0.6), 0.9390948803, 0.002);
    check_near("fl5 M4.7 dayF=1", RPA::flareAbsorption(1e-05, 1.0), 14.05853313, 0.002);
    check_near("fl6 X1 dayF=1", RPA::flareAbsorption(0.0001, 1.0), 79.0569415, 0.00790569415);
    check_near("fl7 M4.7 dayF=0 (noche)", RPA::flareAbsorption(1e-05, 0.0), 4.217559939, 0.002);
    check_bool("fl8 monotonia F (1e-6 < 1e-5 < 1e-4, dayF=1)",
        RPA::flareAbsorption(1e-6f, 1.0f) < RPA::flareAbsorption(1e-5f, 1.0f) &&
        RPA::flareAbsorption(1e-5f, 1.0f) < RPA::flareAbsorption(1e-4f, 1.0f));

    check_near("db1 calma total", RPA::dRegionAbsorptionDb(1.0, 0.0, 0.0), 0.0, 0.005);
    check_near("db2 aceptacion 3: Kp=6", RPA::dRegionAbsorptionDb(1.0, 0.0, 6.0), 15.07251311, 0.005);
    check_near("db3 Kp=2", RPA::dRegionAbsorptionDb(1.0, 0.0, 2.0), 1.500482848, 0.005);
    check_near("db4 noche: solo auroral", RPA::dRegionAbsorptionDb(-0.3, 1e-05, 6.0), 15.07251311, 0.005);
    check_near("db5 B4.1+Kp3 parcial", RPA::dRegionAbsorptionDb(0.7, 4.2e-07, 3.0), 5.159204039, 0.005);
    check_near("db6 frontera F=1e-7", RPA::dRegionAbsorptionDb(1.0, 1e-07, 0.0), 0.8002257345, 0.005);
    check_near("db7 M4.7 cenit", RPA::dRegionAbsorptionDb(1.0, 1e-05, 0.0), 25.30535963, 0.005);
    check_near("db8 Kp negativo (guard)", RPA::dRegionAbsorptionDb(1.0, 1e-05, -2.0), 25.30535963, 0.005);

    check_near("luf1 guardia muf<=0", RPA::deriveLUF(0.0, 0.0, 40.8, 0.5, utcA, 0.0, 2.0), 0.0, 0.002);
    check_near("luf2 calma dia EB040 (cap)", RPA::deriveLUF(15.0, 6.0, 40.8, 0.5, utcA, 0.0, 2.0), 5.4, 0.002);
    check_near("luf3 calma noche sin cap", RPA::deriveLUF(15.0, 0.0, 40.8, 0.5, utcN, 0.0, 2.0), 5.85, 0.002);
    check_near("luf4 flare M4.7 (acep. 2)", RPA::deriveLUF(15.0, 6.0, 40.8, 0.5, utcA, 1e-05, 2.0), 13.85904189, 0.002);
    check_near("luf5 techo 20 (X10)", RPA::deriveLUF(60.0, 0.0, 40.8, 0.5, utcA, 0.001, 2.0), 20.0, 0.002);
    check_near("luf6 suelo 2 (noche)", RPA::deriveLUF(1.0, 0.0, 40.8, 0.5, utcN, 0.0, 2.0), 2.0, 0.002);
    check_near("luf7 mediodia 90E (lon)", RPA::deriveLUF(15.0, 0.0, 18.3, 90.0, utcJ, 0.0, 2.0), 6.83, 0.002);
    check_near("luf8 cap en calma B4.1", RPA::deriveLUF(15.0, 6.0, 40.8, 0.5, utcA, 4.2e-07, 2.0), 7.482094525, 0.002);

    // P3 (cap solo en calma): con F=1e-5 el LUF EXCEDE el cap 0.9*foF2.
    check_bool("p3a flare libera el cap (luf4 > 0.9*foF2 = 5.4)",
        RPA::deriveLUF(15.0f, 6.0f, 40.8f, 0.5f, utcA, 1e-5f, 2.0f) > 5.4f);
    // Aceptacion 2: subida >= 4 MHz respecto a la calma (y a la referencia).
    check_bool("p3b aceptacion 2: subida >= 4 MHz (flotante vs calma)",
        (RPA::deriveLUF(15.0f, 6.0f, 40.8f, 0.5f, utcA, 1e-5f, 2.0f)
           - RPA::deriveLUF(15.0f, 6.0f, 40.8f, 0.5f, utcA, 0.0f, 2.0f)) >= 4.0f);
    check_near("p3c subida exacta vs calma",
        RPA::deriveLUF(15.0f, 6.0f, 40.8f, 0.5f, utcA, 1e-5f, 2.0f)
          - RPA::deriveLUF(15.0f, 6.0f, 40.8f, 0.5f, utcA, 0.0f, 2.0f),
        8.459041892, 5e-3);
    // Referencia vieja con foF2=6: cap identico en calma (aceptacion 1,
    // regimen tapado: el cap domina y nueva == vieja EXACTO).
    check_near("p3d referencia 16b6fa5 (regimen cap)", oldLuf16b6fa5(15.0f, 6.0f, 40.8f, 0.5f, utcA), 5.4, 1e-4);

    check_near("acep1 delta B4.1 EB040 (nueva)", RPA::deriveLUF(15.0f, 0.0f, 40.8f, 0.5f, utcA, 4.2e-7f, 2.0f), 7.482094525, 2e-3);
    check_near("acep1 delta B4.1 EB040 (vieja)", oldLuf16b6fa5(15.0f, 0.0f, 40.8f, 0.5f, utcA), 6.909752724, 2e-3);
    check_bool("acep1 B4.1 EB040 dentro del ruido (|delta| < 0.8723418005)", fabs(RPA::deriveLUF(15.0f, 0.0f, 40.8f, 0.5f, utcA, 4.2e-7f, 2.0f) - oldLuf16b6fa5(15.0f, 0.0f, 40.8f, 0.5f, utcA)) < 0.8723418005);
    check_near("acep1 delta B4.1 JC135 (nueva)", RPA::deriveLUF(15.0f, 0.0f, 18.3f, 0.5f, utcA, 4.2e-7f, 2.0f), 7.482094525, 2e-3);
    check_near("acep1 delta B4.1 JC135 (vieja)", oldLuf16b6fa5(15.0f, 0.0f, 18.3f, 0.5f, utcA), 7.179145057, 2e-3);
    check_bool("acep1 B4.1 JC135 dentro del ruido (|delta| < 0.6029494678)", fabs(RPA::deriveLUF(15.0f, 0.0f, 18.3f, 0.5f, utcA, 4.2e-7f, 2.0f) - oldLuf16b6fa5(15.0f, 0.0f, 18.3f, 0.5f, utcA)) < 0.6029494678);
    check_near("acep1 delta B4.1 ecuador (nueva)", RPA::deriveLUF(15.0f, 0.0f, 0.0f, 0.5f, utcA, 4.2e-7f, 2.0f), 7.482094525, 2e-3);
    check_near("acep1 delta B4.1 ecuador (vieja)", oldLuf16b6fa5(15.0f, 0.0f, 0.0f, 0.5f, utcA), 7.249946692, 2e-3);
    check_bool("acep1 B4.1 ecuador dentro del ruido (|delta| < 0.5321478325)", fabs(RPA::deriveLUF(15.0f, 0.0f, 0.0f, 0.5f, utcA, 4.2e-7f, 2.0f) - oldLuf16b6fa5(15.0f, 0.0f, 0.0f, 0.5f, utcA)) < 0.5321478325);

    // P2 sellado: latDeg no modula la base (el 0.7 la absorbe).
    check_bool("p2a lat-independencia (lat 0 == lat 60)",
        RPA::deriveLUF(15.0f, 0.0f, 0.0f, 0.5f, utcA, 0.0f, 2.0f)
            == RPA::deriveLUF(15.0f, 0.0f, 60.0f, 0.5f, utcA, 0.0f, 2.0f));
    // Firma simetrica con deriveSpreadF: kp no entra en el modelo LUF.
    check_bool("p2b kp no-op (0 == 9)",
        RPA::deriveLUF(15.0f, 6.0f, 40.8f, 0.5f, utcA, 0.0f, 0.0f)
            == RPA::deriveLUF(15.0f, 6.0f, 40.8f, 0.5f, utcA, 0.0f, 9.0f));
    // Determinismo + dia/noche (mismo circuito).
    check_bool("det dia > noche (sin cap)",
        RPA::deriveLUF(15.0f, 0.0f, 40.8f, 0.5f, utcA, 0.0f, 2.0f)
            > RPA::deriveLUF(15.0f, 0.0f, 40.8f, 0.5f, utcN, 0.0f, 2.0f));
    check_bool("det determinismo (misma llamada, mismo bit)",
        RPA::deriveLUF(15.0f, 6.0f, 40.8f, 0.5f, utcA, 1e-5f, 2.0f)
            == RPA::deriveLUF(15.0f, 6.0f, 40.8f, 0.5f, utcA, 1e-5f, 2.0f));

    printf("test_d_region: %d/%d checks OK\n", g_ok, g_ok + g_fail);
    return g_fail == 0 ? 0 : 1;
}
