// =============================================================================
// test_volume_guard — ruling 190 (b): el guard salta el rebuild con estaticos.
//
// Llama a PRODUCCION (VolumeBuildKey.h puro: huella + igualdad + decision).
// Cada input variado -> rebuild dispara; todo estatico -> cero rebuilds en
// >=12 ciclos del timer. Logica pura: sin GL, sin GLFW, sin red, sin App.
// =============================================================================
#include "Ionosphere/VolumeBuildKey.h"

#include <cstdio>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

static void expectTrue(const char* what, bool cond) {
    ++g_checks;
    if (!cond) ++g_failures;
    std::printf("  %-58s [%s]\n", what, cond ? "OK" : "FAIL");
}

static StationSample mkStation(const char* code, float lat, float lon,
                               float foF2, float hmF2) {
    StationSample s;
    s.code = code;
    s.latDeg = lat;
    s.lonDeg = lon;
    s.foF2 = foF2;
    s.hmF2 = hmF2;
    s.nmF2 = 1.24e10f * foF2 * foF2;
    s.foE = 2.0f;
    s.foF1 = 3.0f;
    s.B0 = 100.0f;
    s.B1 = 3.0f;
    s.timeUtc = 1789948800.0;
    s.valid = true;
    s.cs = 80;
    return s;
}

static VolumeBuildKey mkKey(const std::vector<StationSample>& ss) {
    VolumeBuildKey k;
    k.volEpoch = 1789948800.0;
    k.sunX = 0.9f;
    k.sunY = 0.1f;
    k.sunZ = 0.2f;
    k.f107 = 120.0;
    k.flare = 1.0f;
    k.samplesFp = volumeSamplesFingerprint(ss);
    k.sourceBits = 2u;   // replayMode, cache llena
    k.armed = true;
    return k;
}

int main() {
    std::vector<StationSample> base;
    base.push_back(mkStation("EB040", 40.8f, 0.5f, 7.0f, 300.0f));
    base.push_back(mkStation("TR169", 41.0f, 29.0f, 6.0f, 280.0f));

    std::printf("== guard: base ==\n");
    {
        VolumeBuildKey last;   // sin armar: primer tick siempre rebuild
        VolumeBuildKey cur = mkKey(base);
        expectTrue("sin build previo -> rebuild", volumeShouldRebuild(cur, last));
        last = cur;
        expectTrue("clave identica -> no rebuild", !volumeShouldRebuild(cur, last));
    }

    std::printf("== guard: cada input dispara ==\n");
    {
        VolumeBuildKey last = mkKey(base);
        VolumeBuildKey k;
        k = mkKey(base); k.volEpoch += 1.0;
        expectTrue("epoch cambia -> rebuild", volumeShouldRebuild(k, last));
        k = mkKey(base); k.sunX += 0.001f;
        expectTrue("sunX cambia -> rebuild", volumeShouldRebuild(k, last));
        k = mkKey(base); k.f107 = 150.0;
        expectTrue("f107 cambia -> rebuild", volumeShouldRebuild(k, last));
        k = mkKey(base); k.flare = 2.0f;
        expectTrue("flare cambia -> rebuild", volumeShouldRebuild(k, last));
        k = mkKey(base); k.sourceBits = 0u;
        expectTrue("sourceBits cambia -> rebuild", volumeShouldRebuild(k, last));
    }
    {
        VolumeBuildKey last = mkKey(base);
        std::vector<StationSample> v;
        v = base; v[0].foF2 = 7.5f;
        expectTrue("foF2 cambia -> rebuild",
                   volumeShouldRebuild(mkKey(v), last));
        v = base; v[1].stale = true;
        expectTrue("stale cambia -> rebuild",
                   volumeShouldRebuild(mkKey(v), last));
        v = base; v[0].timeUtc += 300.0;
        expectTrue("timeUtc cambia -> rebuild",
                   volumeShouldRebuild(mkKey(v), last));
        v = base; v.pop_back();
        expectTrue("estacion sale -> rebuild",
                   volumeShouldRebuild(mkKey(v), last));
        v = base; v.push_back(mkStation("GM037", 50.0f, 10.0f, 5.0f, 260.0f));
        expectTrue("estacion entra -> rebuild",
                   volumeShouldRebuild(mkKey(v), last));
        v = base; v[0].code = "EB041";
        expectTrue("code cambia -> rebuild",
                   volumeShouldRebuild(mkKey(v), last));
        v = base;
        GiroHistoryPoint q;
        q.timeUtc = 1789948500.0; q.foF2 = 6.9f; q.hmF2 = 295.0f;
        q.B0 = 98.0f; q.B1 = 2.9f;
        v[0].history.push_back(q);
        expectTrue("historia crece -> rebuild",
                   volumeShouldRebuild(mkKey(v), last));
    }

    std::printf("== guard: estatico 12 ciclos -> cero rebuilds ==\n");
    {
        VolumeBuildKey last;   // tick 0: primer build
        int rebuilds = 0;
        for (int tick = 0; tick < 13; tick++) {
            VolumeBuildKey cur = mkKey(base);   // mismos inputs 13 ticks
            if (volumeShouldRebuild(cur, last)) {
                rebuilds++;
                last = cur;
            }
        }
        expectTrue("13 ticks estaticos -> 1 solo rebuild", rebuilds == 1);
    }

    if (g_failures == 0) {
        std::printf("\ntest_volume_guard: TODOS OK (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("\ntest_volume_guard: %d FALLOS de %d checks\n", g_failures,
                g_checks);
    return 1;
}
