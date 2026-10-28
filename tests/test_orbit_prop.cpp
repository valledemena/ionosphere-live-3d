// =============================================================================
// test_orbit_prop - TLE real -> posicion visual (lote 4, modulo A).
//
// PURO: sin red, sin GL, sin ImGui. Llama a PRODUCCION (orbitprop y los
// parsers de TleAdapter), no replica formulas.
//
// Cubre: checksum, parseo del TLE ISS (epoca/inclinacion/movimiento medio),
// altitud y periodo plausibles, coherencia de |r| a 0 s y a 1 periodo, TLE
// GEO-like (~35786 km), TLE corrupto (false y sin escribir basura), GMST de
// J2000, deriva J2 del RAAN, catalogo de 3 lineas, JSON de SatNOGS y cache de
// disco con su TTL. Cero datos sinteticos de satelites: los dos TLE usados son
// reales (ISS, de SatNOGS/CelesTrak) o construidos a mano con checksum valido
// para el caso GEO (declarado).
// =============================================================================
#include "Data/OrbitProp.h"
#include "Data/TleAdapter.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

static int g_ok = 0, g_fail = 0;

static void checkBool(const char* what, bool cond) {
    if (cond)
        ++g_ok;
    else
        ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static void checkNear(const char* what, double got, double want, double tol) {
    const bool ok = std::fabs(got - want) <= tol;
    if (ok)
        ++g_ok;
    else
        ++g_fail;
    std::printf("[%s] %s (got %.6f, want %.6f +- %.6f)\n", ok ? "OK" : "FAIL", what,
                got, want, tol);
}

static void checkRange(const char* what, double got, double lo, double hi) {
    const bool ok = (got >= lo && got <= hi);
    if (ok)
        ++g_ok;
    else
        ++g_fail;
    std::printf("[%s] %s (got %.6f, rango [%.6f, %.6f])\n", ok ? "OK" : "FAIL", what,
                got, lo, hi);
}

// Dias desde 1970-01-01 (Hinnant), solo para fijar la epoca esperada.
static long long daysFromCivil(int y, int m, int d) {
    y -= (m <= 2) ? 1 : 0;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

// TLE real de la ISS (epoch 26299.59614366 = 2026-10-26 14:18:26.8 UTC).
static const char* kIss1 =
    "1 25544U 98067A   26299.59614366  .00006146  00000-0  11933-3 0  9995";
static const char* kIss2 =
    "2 25544  51.6336   9.8132 0006932 312.5452  47.4951 15.49065369592128";

// TLE GEO-like de test (satnum 99001, i = 0.05, e = 0.0002, n = 1.0027
// rev/dia). Construido a mano con checksum valido: NO es un satelite real y
// solo se usa para comprobar la altitud geoestacionaria.
static const char* kGeo1 =
    "1 99001U 98067A   26299.50000000  .00006146  00000-0  11933-3 0  9999";
static const char* kGeo2 =
    "2 99001  0.0500  40.0000 0002000 100.0000 200.0000  1.00270000  12345";

int main() {
    using namespace orbitprop;
    const double now = 1793000000.0;   // instante de referencia del test

    std::printf("--- orbit-prop: TLE ISS ---\n");
    {
        checkBool("1: checksum valido en las dos lineas ISS",
                  tleChecksumOk(kIss1) && tleChecksumOk(kIss2));

        OrbitElements el;
        const bool ok = parseTle(kIss1, kIss2, el);
        checkBool("2: parseTle ISS devuelve true y valid", ok && el.valid);
        if (ok) {
            // 2026-10-26 = dia 299 de 2026 (0-based: 298 + 0.59614366).
            const double expected =
                (double)daysFromCivil(2026, 1, 1) * 86400.0 +
                (298.0 + 0.59614366) * 86400.0;
            checkNear("3: epoca del TLE (s UTC)", el.epochUtc, expected, 1.0);
            checkNear("4: inclinacion (deg)", el.incDeg, 51.6336, 1e-4);
            checkNear("5: movimiento medio (rev/dia)", el.meanMotionRevDay,
                      15.49065369, 1e-6);
            checkNear("6: excentricidad implicita", el.ecc, 0.0006932, 1e-9);
            checkRange("7: altitud media (km)", meanAltitudeKm(el), 380.0, 460.0);
            checkRange("8: periodo orbital (min)", orbitalPeriodMin(el), 92.0, 93.8);
            checkBool("9: regimen LEO (altitud media < 2000 km)",
                      meanAltitudeKm(el) < 2000.0);

            // |r| a 0 s y a 1 periodo: sin arrastre el radio debe repetirse.
            double r0[3] = {0.0, 0.0, 0.0}, r1[3] = {0.0, 0.0, 0.0};
            const double periodSec = orbitalPeriodMin(el) * 60.0;
            const bool p0 = propagateEcef(el, el.epochUtc, r0);
            const bool p1 = propagateEcef(el, el.epochUtc + periodSec, r1);
            const double n0 = std::sqrt(r0[0] * r0[0] + r0[1] * r0[1] + r0[2] * r0[2]);
            const double n1 = std::sqrt(r1[0] * r1[0] + r1[1] * r1[1] + r1[2] * r1[2]);
            checkBool("10: propagacion a 0 s y a 1 periodo", p0 && p1 && n0 > 0.0);
            checkNear("11: |r| a 1 periodo vs 0 s (< 1%)", std::fabs(n1 - n0) / n0,
                      0.0, 0.01);

            // Punto de escena: radio ~1 + alt/6371 y finito.
            double sx = 0.0, sy = 0.0, sz = 0.0, alt = 0.0;
            const bool ps = toScene(el, el.epochUtc, sx, sy, sz, alt);
            const double rad = std::sqrt(sx * sx + sy * sy + sz * sz);
            checkBool("12: toScene devuelve un punto finito", ps && std::isfinite(rad));
            checkRange("13: radio de escena (R_E)", rad, 1.00, 1.15);
        } else {
            checkBool("2b: sin parseo no se pueden probar mas campos", false);
        }
    }

    std::printf("--- orbit-prop: TLE GEO-like ---\n");
    {
        OrbitElements el;
        const bool ok = parseTle(kGeo1, kGeo2, el);
        checkBool("14: parseTle GEO valido", ok && el.valid);
        if (ok) {
            checkNear("15: excentricidad GEO", el.ecc, 0.0002, 1e-9);
            checkNear("16: altitud media GEO (km)", meanAltitudeKm(el), 35786.0, 500.0);
            checkRange("17: periodo GEO (min)", orbitalPeriodMin(el), 1430.0, 1445.0);
            double r[3] = {0.0, 0.0, 0.0};
            const bool p = propagateEcef(el, el.epochUtc, r);
            const double radius = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            checkNear("18: |r| GEO - Re (km)", radius - kEarthRadiusKm, 35786.0, 500.0);
        }
    }

    std::printf("--- orbit-prop: corrupcion y utilidades ---\n");
    {
        // Un parseo fallido NO puede dejar basura en el destino.
        OrbitElements el;
        el.incDeg = -12345.0;
        el.meanMotionRevDay = -1.0;
        el.valid = true;
        const bool ok = parseTle("1 25544U basura", kIss2, el);
        checkBool("19: TLE corrupto devuelve false", !ok);
        checkBool("20: TLE corrupto no escribe en out",
                  el.incDeg == -12345.0 && el.meanMotionRevDay == -1.0 && el.valid);

        // Checksum roto: ultima cifra cambiada.
        std::string bad = kIss1;
        bad[68] = (bad[68] == '9') ? '0' : (char)(bad[68] + 1);
        OrbitElements el2;
        checkBool("21: checksum roto descartado", !parseTle(bad, kIss2, el2));

        // GMST de J2000.0 (2000-01-01 12:00 UTC) = 280.46061837 deg.
        const double gmstDeg = gmstRad(946728000.0) / kDeg;
        checkNear("22: GMST J2000 (deg)", gmstDeg, 280.46061837, 1e-3);

        OrbitElements iss;
        if (parseTle(kIss1, kIss2, iss)) {
            // Progrado (i < 90) -> el RAAN se mueve hacia el oeste.
            checkBool("23: J2 RAANdot negativa para orbita prograda",
                      j2RaanDotDegPerDay(iss) < 0.0);
        } else {
            checkBool("23: no se pudo parsear ISS para J2", false);
        }

        double eccAnom = 0.0;
        checkBool("24: Kepler con e = 0.7 converge",
                  solveKepler(1.0, 0.7, eccAnom) &&
                      std::fabs(eccAnom - 0.7 * std::sin(eccAnom) - 1.0) < 1e-9);
        checkBool("25: Kepler rechaza e >= 1", !solveKepler(1.0, 1.0, eccAnom));
    }

    std::printf("--- tle-adapter: catalogo y fallback (sin red) ---\n");
    {
        std::string text;
        text += "ISS (ZARYA)\n";
        text += std::string(kIss1) + "\n" + kIss2 + "\n";
        text += "ROTO\n";
        text += "1 12345U basura\n2 12345 basura\n";
        text += "GEO TEST\n";
        text += std::string(kGeo1) + "\n" + kGeo2 + "\n";

        TleSnapshot snap;
        const bool ok = TleAdapter::parseTleCatalog(text, now, snap);
        checkBool("26: catalogo 3 lineas valido", ok && snap.valid);
        checkBool("27: 2 TLE validos y 1 descartado", snap.sats.size() == 2);
        checkBool("28: fuente del catalogo = celestrak",
                  snap.source == TleSource::Celestrak);
        if (snap.sats.size() == 2) {
            checkBool("29: nombre y NORAD del primer registro",
                      snap.sats[0].name == "ISS (ZARYA)" && snap.sats[0].noradId == 25544);
            checkBool("30: las lineas crudas se conservan",
                      snap.sats[0].line1 == kIss1 && snap.sats[0].line2 == kIss2);
        } else {
            checkBool("29: nombres del catalogo", false);
            checkBool("30: lineas crudas", false);
        }

        // JSON de SatNOGS (mismo shape medido en vivo: array de objetos).
        std::string json = "[";
        json += "{\"tle0\":\"0 ISS (ZARYA)\",\"tle1\":\"" + std::string(kIss1) +
                "\",\"tle2\":\"" + kIss2 + "\",\"norad_cat_id\":25544},";
        json += "{\"tle0\":\"0 ROTO\",\"tle1\":\"1 99002U basura\","
                "\"tle2\":\"2 99002 basura\",\"norad_cat_id\":99002}]";
        TleSnapshot sn;
        const bool okJson = TleAdapter::parseSatNogsJson(json, now, sn);
        checkBool("31: JSON SatNOGS valido", okJson && sn.valid);
        checkBool("32: JSON: 1 fila valida y fuente satnogs",
                  sn.sats.size() == 1 && sn.source == TleSource::SatNogs);
        if (sn.sats.size() == 1) {
            checkBool("33: prefijo \"0 \" del nombre eliminado",
                      sn.sats[0].name == "ISS (ZARYA)");
        } else {
            checkBool("33: nombre SatNOGS", false);
        }

        // Cache de disco: se escribe, se relee y caduca por TTL.
        const std::string path =
            (std::filesystem::temp_directory_path() / "test_satellites.tle").string();
        const bool saved = TleAdapter::saveCache(path, snap);
        checkBool("34: saveCache escribe un catalogo valido", saved);

        TleSnapshot fresh;
        const bool loaded = TleAdapter::loadCache(path, now + 60.0, fresh);
        checkBool("35: loadCache relee y declara source = cache",
                  loaded && fresh.valid && fresh.source == TleSource::Cache &&
                      fresh.sats.size() == 2);

        TleSnapshot old;
        checkBool("36: cache caducada (> 6 h) se rechaza",
                  !TleAdapter::loadCache(path, now + 7.0 * 3600.0, old));

        TleSnapshot emptySnap;
        checkBool("37: nunca se escribe cache vacia",
                  !TleAdapter::saveCache(path, emptySnap));
        std::error_code ec;
        std::filesystem::remove(path, ec);

        // C2a: la cache viaja como fs::path, no como std::string. Con un
        // perfil no ASCII, un .tmp creado desde un string UTF-8 no es hermano
        // del destino y el rename falla en silencio.
#if defined(_WIN32)
        const std::filesystem::path sub = L"tle_cache_Caf\u00e9_\u00d1";
#else
        const std::filesystem::path sub = "tle_cache_Caf\u00e9_\u00d1";
#endif
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / sub;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path tildePath = dir / "satellites.tle";
        checkBool("38: saveCache en ruta no ASCII", TleAdapter::saveCache(tildePath, snap));
        TleSnapshot fromTilde;
        checkBool("39: loadCache relee la cache de una ruta no ASCII",
                  TleAdapter::loadCache(tildePath, now + 60.0, fromTilde) &&
                      fromTilde.valid && fromTilde.sats.size() == 2);
        std::filesystem::path leftover = tildePath;
        leftover += ".tmp";
        checkBool("40: el .tmp no queda huerfano", !std::filesystem::exists(leftover));
        std::filesystem::remove_all(dir, ec);
    }

    std::printf("[test_orbit_prop] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
