// =============================================================================
// test_sdo_adapter — parseo sunspot_report (M11b, filas REALES en vivo).
//
// Fixture: 3 filas verbatim de
//   GET https://services.swpc.noaa.gov/json/sunspot_report.json (2026-09-12):
//   normal N08W10, Region null (se salta), y una sin Location (fallback).
// Llama a PRODUCCION (SolarIndicesAdapter::parseSunspotReport), no replica.
// Logica pura: sin GL, sin red en el test (el fetch vive solo en el worker).
// =============================================================================
#include "Data/SolarIndicesAdapter.h"
#include "Utils/SunDiskMath.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_ok = 0, g_fail = 0;

static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

// Fila 1 y 2: verbatim live 2026-09-12. Fila 3 = fila 1 sin "Location".
static const char* kFixture =
    "[{\"time_tag\":\"2026-08-17T06:07:00\",\"Obsdate\":\"2026-08-17T00:00:00\","
    "\"Station\":16320,\"Region\":4508,\"Latitude\":8,\"Report_Longitude\":1,"
    "\"Longitude\":-10,\"Report_Location\":\"N08E01\",\"Location\":\"N08W10\","
    "\"Carlon\":164,\"Extent\":5,\"Area\":50,\"Numspot\":2},"
    "{\"time_tag\":\"2026-08-14T01:30:00\",\"Region\":null,\"Latitude\":-18,"
    "\"Report_Longitude\":11,\"Longitude\":-2,\"Location\":\"S18W02\","
    "\"Carlon\":197,\"Numspot\":2},"
    "{\"time_tag\":\"2026-08-16T05:55:00\",\"Region\":4506,\"Latitude\":11,"
    "\"Report_Longitude\":-22,\"Longitude\":-33,\"Carlon\":214,\"Numspot\":9}]";

int main() {
    std::vector<sundisk::Sunspot> rows;
    check_bool("parse ok", SolarIndicesAdapter::parseSunspotReport(kFixture, rows));
    check_bool("null-Region saltada (2 filas)", rows.size() == 2);

    const sundisk::Sunspot& a = rows[0];
    check_bool("region 4508", a.region == 4508);
    check_bool("lat 8", a.lat == 8.0);
    check_bool("loc N08W10", a.loc == "N08W10");
    check_bool("lonCarr 164", a.hasLonCarr && a.lonCarr == 164.0);
    check_bool("dlon -10 (W negativo)", a.hasDlon && a.dlon == -10.0);
    check_bool("numspot 2", a.numspot == 2);
    check_bool("epoch presente", a.timeUtc > 1786000000.0);

    const sundisk::Sunspot& b = rows[1];
    check_bool("sin Location -> loc vacio", b.loc.empty());
    check_bool("fallback Carlon/dlon vivos",
               b.hasLonCarr && b.lonCarr == 214.0 && b.hasDlon && b.dlon == -33.0);

    // Integracion con M11a: ultimo-por-Region sobre lo parseado.
    std::map<int, sundisk::Sunspot> m = sundisk::latestByRegion(rows);
    check_bool("2 regiones distintas", m.size() == 2);

    std::vector<sundisk::Sunspot> empty;
    check_bool("vacio -> false", !SolarIndicesAdapter::parseSunspotReport("[]", empty));
    check_bool("basura -> false", !SolarIndicesAdapter::parseSunspotReport("not json", empty));

    std::printf("\n[test_sdo_adapter] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
