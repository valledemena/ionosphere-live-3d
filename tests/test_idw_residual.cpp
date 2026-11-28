// DW.35 F4: test determinista del nucleo IDW de residuo.
#include "Utils/IdwResidual.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

int main(int argc, char** argv) {
    const double utc = 1767268800.0;   // 2026-01-01 12:00 UTC
    const float f107 = 100.0f;
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) {
        std::vector<StationSample> bs;
        for (int i = 0; i < 129; ++i) {
            StationSample s;
            s.latDeg = -80.0f + (i % 17) * 10.0f;
            s.lonDeg = -175.0f + (i % 29) * 12.0f;
            s.foF2 = 6.0f + (i % 7);
            s.valid = true;
            bs.push_back(s);
        }
        std::vector<double> t;
        t.reserve(1000);
        for (int k = 0; k < 1000; ++k) {
            const auto a = std::chrono::high_resolution_clock::now();
            const auto g = idwres::giroResidualGrid(bs, utc, f107, 72, 72);
            const auto m = idwres::residualMetrics(bs, utc, f107);
            const auto b = std::chrono::high_resolution_clock::now();
            (void)g; (void)m;
            t.push_back(std::chrono::duration<double, std::milli>(b - a).count());
        }
        std::sort(t.begin(), t.end());
        std::printf("F4 CPU bench n=1000 p50=%.4fms p95=%.4fms min=%.4f max=%.4f\n",
                    t[500], t[950], t[0], t[999]);
        return 0;
    }
    std::vector<StationSample> st;
    for (int i = 0; i < 9; ++i) {
        StationSample s;
        s.latDeg = -40.0f + (float)(i / 3) * 40.0f;
        s.lonDeg = -60.0f + (float)(i % 3) * 60.0f;
        s.foF2 = idwres::climatologyFoF2(s.latDeg, s.lonDeg, utc, f107);
        s.valid = true;
        st.push_back(s);
    }

    // 1) observacion == fondo => residuo ~0 y RMSE ~0.
    const auto m0 = idwres::residualMetrics(st, utc, f107);
    check(m0.n == 9, "nueve estaciones validas");
    check(m0.nLowModip + m0.nHighModip == m0.n, "estratos MODIP cubren todas");
    check(m0.nDay + m0.nNight == m0.n, "estratos cenit cubren todas");
    check(std::fabs(m0.bias) < 1e-4f, "bias cero con fondo exacto");
    check(m0.rmse < 1e-4f, "RMSE cero con fondo exacto");
    // Un estrato con menos de 2 estaciones es NO MEDIBLE (NaN), nunca 0.
    {
        std::vector<StationSample> one;
        StationSample s = st[0];
        one.push_back(s);
        const auto mm = idwres::residualMetrics(one, utc, f107);
        check(std::isnan(mm.bias) && std::isnan(mm.rmse),
              "menos de 3 estaciones = NO MEDIBLE");
    }

    const auto g0 = idwres::giroResidualGrid(st, utc, f107, 72, 72);
    check(g0.valid && g0.width == 72 && g0.height == 72, "grid 72x72 valido");
    bool nearBg = true;
    for (int idx : {0, 35 * 72 + 36, 71 * 72 + 71}) {
        const float lat = -90.0f + ((idx / 72) + 0.5f) * 2.5f;
        const float lon = -180.0f + ((idx % 72) + 0.5f) * 5.0f;
        const float bg = idwres::climatologyFoF2(lat, lon, utc, f107);
        if (std::fabs(g0.values[idx] - bg) > 0.5f) nearBg = false;
    }
    check(nearBg, "grid sigue el fondo sin offset");

    // 2) offset conocido +2 MHz => el residuo lo recupera.
    for (auto& s : st) s.foF2 += 2.0f;
    const auto m2 = idwres::residualMetrics(st, utc, f107);
    check(std::fabs(m2.bias - 2.0f) < 1e-4f, "bias=2 con offset conocido");
    check(std::fabs(m2.rmse - 2.0f) < 1e-4f, "RMSE=2 con offset conocido");
    const auto g2 = idwres::giroResidualGrid(st, utc, f107, 72, 72);
    const int idx = 35 * 72 + 36;
    const float lat = -90.0f + ((idx / 72) + 0.5f) * 2.5f;
    const float lon = -180.0f + ((idx % 72) + 0.5f) * 5.0f;
    const float expect = idwres::climatologyFoF2(lat, lon, utc, f107) + 2.0f;
    check(std::fabs(g2.values[idx] - expect) < 1.0f, "grid recupera offset +2");

    std::printf("F4 idw residual: failures=%d n=%d bias=%.4f rmse=%.4f\n",
                failures, m2.n, m2.bias, m2.rmse);
    return failures == 0 ? 0 : 1;
}
