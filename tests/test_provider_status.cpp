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
        // 5b: el motivo suma causa + espera (antes el retry se perdia cuando
        // habia un error parcial, p.ej. "backoff 1 st" de GIRO).
        Snapshot dr;
        dr.id = PROV_GIRO;
        dr.hasData = true;
        dr.error = "backoff 1 st";
        dr.retryInMin = 7.0;
        const std::string rsn = buildRows(&dr, 1, now)[(int)PROV_GIRO].reason;
        check_bool("5b: reason = error + retry",
                   rsn.find("backoff 1 st") != std::string::npos &&
                       rsn.find("retry 7 min") != std::string::npos);
        // (6) IRTAM gate-wait = degraded, NO rate-limited.
        Snapshot g;
        g.id = PROV_IRTAM;
        g.hasData = true;
        g.error = "gate: 15 s (courtesy)";
        check_bool("6: gate: -> degraded",
                   classify(g, now) == State::Degraded);
        check_bool("6b: gate: no es rate-limited",
                   classify(g, now) != State::RateLimited);
        // 6c: gate SIN datos tambien es espera declarada, no fallo.
        Snapshot g0;
        g0.id = PROV_IRTAM;
        g0.error = "gate: 15 s (courtesy)";
        check_bool("6c: gate sin datos -> degraded",
                   classify(g0, now) == State::Degraded);
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
        // (13) umbrales de las fuentes del lote 3 (2x su cadencia nominal).
        Snapshot w;
        w.id = PROV_WSPR;
        w.hasData = true;
        w.dataEpoch = now - 700.0;
        check_bool("13: WSPR 700 s > 600 -> stale",
                   classify(w, now) == State::Stale);
        Snapshot en;
        en.id = PROV_ENLIL;
        en.hasData = true;
        en.dataEpoch = now - 3700.0;
        check_bool("13b: ENLIL 3700 s > 3600 -> stale",
                   classify(en, now) == State::Stale);
        Snapshot nw;
        nw.id = PROV_NWS;
        nw.hasData = true;
        nw.dataEpoch = now - 700.0;
        check_bool("13c: NWS 700 s > 600 -> stale",
                   classify(nw, now) == State::Stale);
        // (13d) umbrales del lote 4.
        Snapshot tl;
        tl.id = PROV_TLE;
        tl.hasData = true;
        tl.dataEpoch = now - 22000.0;
        check_bool("13d: TLE 22000 s > 21600 -> stale",
                   classify(tl, now) == State::Stale);
        Snapshot ls;
        ls.id = PROV_LASCO;
        ls.hasData = true;
        ls.dataEpoch = now - 1900.0;
        check_bool("13e: LASCO 1900 s > 1800 -> stale",
                   classify(ls, now) == State::Stale);
        Snapshot fl;
        fl.id = PROV_FLR;
        fl.hasData = true;
        fl.dataEpoch = now - 3700.0;
        check_bool("13f: DONKI flares 3700 s > 3600 -> stale",
                   classify(fl, now) == State::Stale);
        // (13g) frescos -> ok (los cuatro del lote 4).
        Snapshot fresh[4];
        fresh[0].id = PROV_TLE;
        fresh[1].id = PROV_LASCO;
        fresh[2].id = PROV_DRAP;
        fresh[3].id = PROV_FLR;
        bool freshOk = true;
        for (Snapshot& f : fresh) {
            f.hasData = true;
            f.dataEpoch = now - 60.0;
            if (classify(f, now) != State::Ok) freshOk = false;
        }
        check_bool("13g: lote 4 fresco -> ok", freshOk);
        // (13h) umbrales del lote 5.
        Snapshot aex;
        aex.id = PROV_AE;
        aex.hasData = true;
        aex.dataEpoch = now - 1000.0;
        check_bool("13h: AE 1000 s > 900 -> stale",
                   classify(aex, now) == State::Stale);
        Snapshot vy;
        vy.id = PROV_VOYAGER;
        vy.hasData = true;
        vy.dataEpoch = now - 44000.0;
        check_bool("13i: Voyager 44000 s > 43200 -> stale",
                   classify(vy, now) == State::Stale);
        Snapshot rb;
        rb.id = PROV_RBE;
        rb.hasData = true;
        rb.dataEpoch = now - 3700.0;
        check_bool("13j: DONKI RBE 3700 s > 3600 -> stale",
                   classify(rb, now) == State::Stale);
        // (14) board vacio -> todas off.
        std::vector<Row> e = buildRows(nullptr, 0, now);
        bool allOff = e.size() == (size_t)PROV_COUNT;
        for (const Row& r : e)
            if (r.state != State::Off) allOff = false;
        check_bool("14: vacio -> todas off", allOff);
        // (15) labels unicos no vacios.
        bool uniq = true;
        for (int i = 0; i < (int)PROV_COUNT; i++) {
            const char* l = providerLabel((ProviderId)i);
            if (l == nullptr || l[0] == '\0') uniq = false;
            for (int j = i + 1; j < (int)PROV_COUNT; j++)
                if (std::string(l) == providerLabel((ProviderId)j))
                    uniq = false;
        }
        check_bool("15: labels unicos", uniq);
    }

    std::printf("[test_provider_status] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
