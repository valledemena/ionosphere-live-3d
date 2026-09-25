// =============================================================================
// test_provider_status — matriz de estado por proveedor (048 prov-matrix).
//
// Puro, sin red, sin GL. Llama a PRODUCCION (provstatus), no replica.
// Cubre: off/failed/rate-limited/degraded/stale/ok, precedencia, (fetching),
// kStaleSec incl. lag IRTAM 72 h, edad andante, determinismo, board vacio,
// labels unicos.
// =============================================================================
#include "Data/ProviderStatus.h"

#include <cstdio>
#include <string>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* what, bool cond) {
    if (cond)
        ++g_ok;
    else
        ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

int main() {
    using namespace provstatus;
    const double now = 1789500000.0;

    std::printf("--- prov-matrix: clasificacion ---\n");
    {
        // (1) off stub NOAA (siempre, con y sin datos).
        Snapshot s;
        s.id = PROV_NOAA_STUB;
        s.hasData = true;
        check_bool("1: stub NOAA -> off",
                   classify(s, now) == State::Off);
        // (2) failed: sin datos + error.
        Snapshot f;
        f.id = PROV_TEC;
        f.error = "HTTP 500";
        check_bool("2: sin datos + error -> failed",
                   classify(f, now) == State::Failed);
        // (3) rate-limited: fallback GIRO declarado.
        Snapshot r;
        r.id = PROV_GIRO;
        r.hasData = true;
        r.rateLimited = true;
        check_bool("3: fallback -> rate-limited",
                   classify(r, now) == State::RateLimited);
        // (4) 429-duro: detector puro + clasificacion.
        check_bool("4: hard429(3,0) true, hard429(2,0)/hard429(5,2) false",
                   hard429(3, 0) && !hard429(2, 0) && !hard429(5, 2));
        Snapshot h;
        h.id = PROV_GIRO;
        h.hasData = true;
        h.rateLimited = hard429(4, 0);
        h.error = "round 0/5 (429)";
        check_bool("4b: 429-duro -> rate-limited",
                   classify(h, now) == State::RateLimited);
        // (5) degraded con retry.
        Snapshot d;
        d.id = PROV_GIRO;
        d.hasData = true;
        d.retryInMin = 5.0;
        check_bool("5: retry 5 -> degraded",
                   classify(d, now) == State::Degraded);
        // (6) IRTAM gate-wait = degraded, NO rate-limited.
        Snapshot g;
        g.id = PROV_IRTAM;
        g.hasData = true;
        g.error = "gate: 15 s (courtesy)";
        check_bool("6: gate: -> degraded",
                   classify(g, now) == State::Degraded);
        check_bool("6b: gate: no es rate-limited",
                   classify(g, now) != State::RateLimited);
        // (7) stale por kStaleSec.
        Snapshot st;
        st.id = PROV_TEC;
        st.hasData = true;
        st.dataEpoch = now - 1300.0;
        check_bool("7: TEC 1300 s > 1200 -> stale",
                   classify(st, now) == State::Stale);
        // (8) lag IRTAM 72 h NO stale (73 h dentro del margen), 76 h SI.
        Snapshot i1;
        i1.id = PROV_IRTAM;
        i1.hasData = true;
        i1.dataEpoch = now - 73.0 * 3600.0;
        check_bool("8: IRTAM 73 h no stale",
                   classify(i1, now) != State::Stale);
        Snapshot i2;
        i2.id = PROV_IRTAM;
        i2.hasData = true;
        i2.dataEpoch = now - 76.0 * 3600.0;
        check_bool("8b: IRTAM 76 h stale",
                   classify(i2, now) == State::Stale);
        // (9) ok con edad andante.
        Snapshot o;
        o.id = PROV_WIND;
        o.hasData = true;
        o.dataEpoch = now - 60.0;
        o.data = "live";
        check_bool("9: fresco -> ok", classify(o, now) == State::Ok);
        
        std::string d1 = buildRows(&o, 1, now)[(int)PROV_WIND].data;
        std::string d2 =
            buildRows(&o, 1, now + 3600.0)[(int)PROV_WIND].data;
        check_bool("9b: edad andante (difiere en 1 h)", d1 != d2);
        // (10) sufijo (fetching) ortogonal.
        Snapshot fe;
        fe.id = PROV_DIAS;
        fe.hasData = true;
        fe.dataEpoch = now - 10.0;
        fe.fetching = true;
        check_bool(
            "10: fetching en stateText",
            stateText(classify(fe, now), fe).find("(fetching)") !=
                std::string::npos);
        // (11) precedencia off>failed>rate-limited>degraded>stale>ok.
        Snapshot p1;
        p1.id = PROV_NOAA_STUB;
        p1.error = "x";
        p1.rateLimited = true;
        check_bool("11: stub manda sobre failed/rate-limited",
                   classify(p1, now) == State::Off);
        Snapshot p2;
        p2.id = PROV_SDO;
        p2.rateLimited = true;
        p2.retryInMin = 3.0;
        p2.hasData = true;
        p2.dataEpoch = now - 100000.0;
        check_bool("11b: rate-limited manda sobre degraded/stale",
                   classify(p2, now) == State::RateLimited);
        // (12) determinismo x2.
        Snapshot dx;
        dx.id = PROV_AURORA;
        dx.hasData = true;
        dx.dataEpoch = now - 100.0;
        
        check_bool(
            "12: buildRows determinista",
            buildRows(&dx, 1, now)[(int)PROV_AURORA].stateText ==
                buildRows(&dx, 1, now)[(int)PROV_AURORA].stateText);
        // (13) board vacio -> 14 filas off.
        std::vector<Row> e = buildRows(nullptr, 0, now);
        bool allOff = e.size() == (size_t)PROV_COUNT;
        for (const Row& r : e)
            if (r.state != State::Off) allOff = false;
        check_bool("13: vacio -> 14 off", allOff);
        // (14) labels 14 unicos no vacios.
        bool uniq = true;
        for (int i = 0; i < (int)PROV_COUNT; i++) {
            const char* l = providerLabel((ProviderId)i);
            if (l == nullptr || l[0] == '\0') uniq = false;
            for (int j = i + 1; j < (int)PROV_COUNT; j++)
                if (std::string(l) == providerLabel((ProviderId)j))
                    uniq = false;
        }
        check_bool("14: 14 labels unicos", uniq);
    }

    std::printf("[test_provider_status] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
