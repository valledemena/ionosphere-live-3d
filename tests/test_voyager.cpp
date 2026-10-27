// =============================================================================
// test_voyager — adaptador Voyager 1/2 de JPL Horizons (lote 5, modulo B).
//
// Puro: sin red, sin GL, sin disco, sin reloj fiable. Llama a PRODUCCION
// (VoyagerAdapter), no replica el parser. Cubre: 3 filas reales del 2026-10-27
// (AU y fechas), seleccion de la fila PASADA mas cercana (nunca la futura),
// fallback a la primera si todo es futuro, presupuesto Friis en orden de
// magnitud, tiempo-luz ~23.9 h, formula del factor de barrido, texto vacio o
// basura (false) y descarte de filas fuera de la banda 100-200 AU.
// =============================================================================
#include "Data/VoyagerAdapter.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* what, bool cond) {
    if (cond)
        ++g_ok;
    else
        ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static void check_near(const char* what, double got, double want, double tol) {
    const bool ok = std::fabs(got - want) <= tol;
    if (ok)
        ++g_ok;
    else
        ++g_fail;
    std::printf("[%s] %s (got %.9g, want %.9g +/- %.3g)\n", ok ? "OK" : "FAIL", what,
                got, want, tol);
}

// Fragmento REAL de Horizons (Voyager 1, -31, quantity 20), capturado hoy
// 2026-10-27: cabecera, tabla entre $$SOE/$$EOE y cola de documentacion.
static const char* kFragment = R"TXT(API VERSION: 1.2
API SOURCE: NASA/JPL Horizons API

*******************************************************************************
Ephemeris / API_USER Tue Oct 27 00:56:13 2026 Pasadena, USA      / Horizons
*******************************************************************************
Target body name: Voyager 1 (spacecraft) (-31)    {source: Voyager_1_ST+refit2022_m}
Center body name: Earth (399)                     {source: Voyager_1_ST+refit2022_m}
Center-site name: GEOCENTRIC
*******************************************************************************
 Date__(UT)__HR:MN                delta      deldot
***************************************************
$$SOE
 2026-Oct-26 00:00     172.732204628703  33.7197642
 2026-Oct-27 00:00     172.751591207326  33.4172627
 2026-Oct-28 00:00     172.770801698361  33.1099465
$$EOE
*******************************************************************************
Column meaning:

 'delta      deldot' =
   Apparent range ("delta", light-time aberrated) and range-rate ("delta-dot")
of the target center relative to the observer. Units: AU and KM/S
)TXT";

int main() {
    // --- 1. Parseo de las 3 filas reales -----------------------------------
    std::vector<VoyagerFix> rows;
    const bool parsed = VoyagerAdapter::parseRows(kFragment, rows);
    check_bool("3 real Horizons rows parsed", parsed && rows.size() == 3);
    if (rows.size() == 3) {
        check_near("row 1 AU", rows[0].au, 172.732204628703, 1e-9);
        check_near("row 2 AU", rows[1].au, 172.751591207326, 1e-9);
        check_near("row 3 AU", rows[2].au, 172.770801698361, 1e-9);
        check_bool("row 1 date preserved", rows[0].date == "2026-Oct-26 00:00");
        check_bool("row 2 date preserved", rows[1].date == "2026-Oct-27 00:00");
        check_bool("row 3 date preserved", rows[2].date == "2026-Oct-28 00:00");
        check_bool("rows in ascending UTC order",
                   rows[0].timeUtc > 0.0 && rows[0].timeUtc < rows[1].timeUtc &&
                       rows[1].timeUtc < rows[2].timeUtc);
        check_near("row spacing is one day", rows[1].timeUtc - rows[0].timeUtc, 86400.0,
                   1.0);
        check_bool("all rows flagged valid", rows[0].valid && rows[1].valid && rows[2].valid);
    }

    // --- 2. Fila pasada mas cercana (nunca una futura) ---------------------
    if (rows.size() == 3) {
        // "Ahora" justo entre la fila 1 y la fila 2: debe elegir la 1.
        const double now = (rows[0].timeUtc + rows[1].timeUtc) * 0.5;
        VoyagerFix pick;
        check_bool("pickCurrentRow succeeds between rows",
                   VoyagerAdapter::pickCurrentRow(rows, now, pick));
        check_near("pickCurrentRow returns the PAST row (not the future one)", pick.au,
                   rows[0].au, 1e-12);
        check_bool("picked row is not in the future", pick.timeUtc <= now);
        check_bool("picked row keeps its date", pick.date == "2026-Oct-26 00:00");

        // Justo en la frontera: la fila 2 ya es pasado.
        VoyagerFix onEdge;
        VoyagerAdapter::pickCurrentRow(rows, rows[1].timeUtc, onEdge);
        check_near("pickCurrentRow includes a row exactly at now", onEdge.au, rows[1].au,
                   1e-12);

        // Todo futuro: la primera (la mas temprana), nunca la ultima.
        VoyagerFix future;
        check_bool("pickCurrentRow with all-future rows succeeds",
                   VoyagerAdapter::pickCurrentRow(rows, rows[0].timeUtc - 3600.0, future));
        check_near("all-future fallback is the first row", future.au, rows[0].au, 1e-12);
        check_bool("all-future fallback is still declared valid", future.valid);
    }
    {
        std::vector<VoyagerFix> empty;
        VoyagerFix pick;
        check_bool("pickCurrentRow on empty vector is false",
                   !VoyagerAdapter::pickCurrentRow(empty, 1.0e9, pick));
    }

    // --- 3. Modelo declarado (ordenes de magnitud, no telemetria) ----------
    const double pr = VoyagerAdapter::friisReceivedW(172.75);
    check_bool("friisReceivedW(172.75) in 1e-20..1e-18 W", pr > 1e-20 && pr < 1e-18);
    check_bool("friisReceivedW is ~4e-19 W", pr > 3.5e-19 && pr < 4.5e-19);

    const double ltHours = VoyagerAdapter::lightTimeSec(172.75) / 3600.0;
    check_bool("lightTimeSec(172.75) in 23.5..24.2 h", ltHours > 23.5 && ltHours < 24.2);
    check_near("lightTimeSec(172.75) is 23.94 h", ltHours, 23.9419, 0.01);

    check_near("pulseLapse(86000 s, 10 s sweep) = 8600",
               VoyagerAdapter::pulseLapse(86000.0, 10000.0), 8600.0, 1e-9);
    check_near("pulseLapse never below 1", VoyagerAdapter::pulseLapse(0.001, 10000.0), 1.0,
               1e-12);
    check_near("pulseLapse of invalid input is 1", VoyagerAdapter::pulseLapse(-5.0, 0.0),
               1.0, 1e-12);
    check_near("friisReceivedW(0) is 0", VoyagerAdapter::friisReceivedW(0.0), 0.0, 1e-30);
    check_near("lightTimeSec(-1) is 0", VoyagerAdapter::lightTimeSec(-1.0), 0.0, 1e-30);

    // --- 4. parseHorizons sobre el texto completo --------------------------
    {
        VoyagerSnapshot snap;
        check_bool("parseHorizons accepts the full Horizons text",
                   VoyagerAdapter::parseHorizons(kFragment, snap));
        check_bool("parseHorizons marks v1 valid", snap.v1.fix.valid && snap.valid);
        check_bool("parseHorizons ignores $$SOE/$$EOE and the footer",
                   snap.v1.fix.au > 172.0 && snap.v1.fix.au < 173.0);
        check_bool("parseHorizons derives a light time in hours",
                   snap.v1.lightHours > 23.5 && snap.v1.lightHours < 24.2);
        check_bool("parseHorizons derives a Friis power in 1e-20..1e-18",
                   snap.v1.receivedW > 1e-20 && snap.v1.receivedW < 1e-18);
        check_bool("parseHorizons derives a sweep factor above 1",
                   snap.v1.pulseFactor > 1000.0);
    }

    // --- 5. Entradas invalidas --------------------------------------------
    {
        VoyagerSnapshot snap;
        check_bool("empty text is false", !VoyagerAdapter::parseHorizons("", snap));
        check_bool("empty text leaves an invalid snapshot", !snap.valid);
        check_bool("garbage text is false",
                   !VoyagerAdapter::parseHorizons("HTTP 503\n<HTML>oops</HTML>\n", snap));
        std::vector<VoyagerFix> none;
        check_bool("parseRows on garbage yields no rows",
                   !VoyagerAdapter::parseRows("hello world\n$$SOE\n$$EOE\n", none) &&
                       none.empty());
        check_bool("parseRows on empty text is false",
                   !VoyagerAdapter::parseRows("", none));
    }

    // --- 6. Banda 100-200 AU: una fila de 5 AU se descarta ------------------
    {
        const char* mixed =
            "$$SOE\n"
            " 2026-Oct-27 00:00       5.000000000000  1.0000000\n"
            " 2026-Oct-27 00:00     172.751591207326  33.4172627\n"
            " 2026-Oct-27 00:00     250.000000000000  1.0000000\n"
            "$$EOE\n";
        std::vector<VoyagerFix> got;
        const bool ok = VoyagerAdapter::parseRows(mixed, got);
        check_bool("only the in-band row survives", ok && got.size() == 1);
        if (got.size() == 1) check_near("surviving row is the Voyager one", got[0].au,
                                        172.751591207326, 1e-9);

        VoyagerSnapshot snap;
        check_bool("parseHorizons with a 5 AU row still picks the real one",
                   VoyagerAdapter::parseHorizons(mixed, snap) &&
                       snap.v1.fix.au > 172.0 && snap.v1.fix.au < 173.0);
    }

    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
