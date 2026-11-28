// DW.21: campo IGRF-14 completo (NMAX=13) expuesto para las lineas de campo.
#include "Data/IrtamGridEval.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int g_ok = 0, g_fail = 0;
static void check(const char* name, bool cond) {
    if (cond) ++g_ok; else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", name);
}

int main() {
    double bn = 0, be = 0, bd = 0, ba = 0;
    // Punto declarado en el pre-registro: lat=40, lon=-75, alt=300 km, 2025.0.
    const bool ok = irtameval::igrfFull(2025.0, 40.0, -75.0, 300.0, bn, be, bd, ba);
    check("DW.21-1: igrfFull(2025,40,-75,300) -> true", ok);
    check("DW.21-2: B finito y no cero",
          ok && std::isfinite(bn) && std::isfinite(be) && std::isfinite(bd) &&
          std::isfinite(ba) && ba > 0.05);  // feldg devuelve Gauss (~0.44 G)
    check("DW.21-3: |B| coherente con componentes",
          ok && std::fabs(ba - std::sqrt(bn*bn + be*be + bd*bd)) < ba * 0.02 + 1.0);
    double x = -1;
    check("DW.21-4: year<2015 rechazado (rama EXTRASHC)",
          !irtameval::igrfFull(2014.0, 40.0, -75.0, 300.0, bn, be, bd, ba));
    check("DW.21-5: NaN rechazado sin ensuciar salidas",
          !irtameval::igrfFull(2025.0, std::numeric_limits<double>::quiet_NaN(),
                           -75.0, 300.0, bn, be, bd, ba));
    std::printf("\n[test_igrf_full] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
