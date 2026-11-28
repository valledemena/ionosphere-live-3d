// DW.20: retardo L1 x/v (helper puro; sin red ni GL).
#include "Utils/SolarWindDelay.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int g_ok = 0, g_fail = 0;
static void check(const char* name, bool cond) {
    if (cond) ++g_ok; else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", name);
}
static bool near(double a, double b) { return std::fabs(a - b) < 0.5; }

static SolarWindSample mk(double t, float v, float bz, bool valid = true,
                          bool hasBx = false) {
    SolarWindSample s;
    s.timeUtc = t;
    s.speed = v;
    s.density = 5.0f;
    s.bz = bz;
    s.valid = valid;
    s.hasBx = hasBx;
    return s;
}

int main() {
    const double nowWall = 4000.0;  // fijo: reloj de pared, no epoch de replay

    std::vector<SolarWindSample> h;
    h.push_back(mk(0.0, 400.0f, 2.0f));
    swdelay::DelayedWind d = swdelay::sampleAt(h, nowWall);
    check("DW.20-1: 1.5e6 km / 400 km/s = 3750 s",
          d.available && near(d.travelSec, 3750.0));
    check("DW.20-2: se devuelve la muestra llegada (v=400)",
          near(d.sample.speed, 400.0f));

    std::vector<SolarWindSample> f;
    f.push_back(mk(0.0, 1000.0f, 1.0f));
    swdelay::DelayedWind df = swdelay::sampleAt(f, 2000.0);
    check("DW.20-3: 1000 km/s -> 1500 s y available",
          df.available && near(df.travelSec, 1500.0));

    swdelay::DelayedWind pending = swdelay::sampleAt(h, 3000.0);
    check("DW.20-4: ahora insuficiente -> unavailable, travelSec finito",
          !pending.available && std::isfinite(pending.travelSec) &&
          near(pending.travelSec, 3750.0));

    std::vector<SolarWindSample> two;
    two.push_back(mk(0.0, 400.0f, 1.0f));
    two.push_back(mk(1000.0, 400.0f, 3.0f));
    swdelay::DelayedWind latestArrived = swdelay::sampleAt(two, 5000.0);
    check("DW.20-5: elige la muestra mas reciente que ya llego",
          latestArrived.available && near(latestArrived.sample.timeUtc, 1000.0));

    std::vector<SolarWindSample> bad;
    bad.push_back(mk(0.0, 0.0f, 1.0f));
    bad.push_back(mk(0.0, std::numeric_limits<float>::quiet_NaN(), 1.0f));
    bad.push_back(mk(0.0, std::numeric_limits<float>::infinity(), 1.0f));
    swdelay::DelayedWind db = swdelay::sampleAt(bad, nowWall);
    check("DW.20-6: v=0/NaN/inf -> unavailable y travelSec finito",
          !db.available && std::isfinite(db.travelSec));

    swdelay::DelayedWind de = swdelay::sampleAt({}, nowWall);
    check("DW.20-7: historial vacio -> unavailable", !de.available);

    std::vector<SolarWindSample> noBz;
    noBz.push_back(mk(0.0, 400.0f, 0.0f, false));
    check("DW.20-8: sin Bz real no es dato valido",
          !swdelay::sampleAt(noBz, nowWall).available);

    std::vector<SolarWindSample> noBx;
    noBx.push_back(mk(0.0, 400.0f, 2.0f, true, false));
    check("DW.20-9: falta Bx (opcional) si es dato valido",
          swdelay::sampleAt(noBx, nowWall).available);

    check("DW.20-10: travelSec = kL1Km / v exacto",
          near(d.travelSec, swdelay::kL1Km / 400.0));

    std::printf("\n[test_solar_wind_delay] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
