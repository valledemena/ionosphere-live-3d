// =============================================================================
// test_tle_csv - C2b: GP CSV (OMM) de CelesTrak, puro y sin red.
//
// Por que CSV: el TLE clasico de 5 digitos se agoto el 2026-07-11 y desde
// 2026-05-09 FORMAT es CSV por defecto. Un NORAD de 6 digitos NO cabe en las
// columnas 3-7 de un TLE, asi que el catalogo tiene que venir por CSV y
// parsearse POR CABECERA (RFC 4180), no por posicion.
//
// Cubre: cabecera real de 19 columnas, nombre entrecomillado con coma y
// comillas dobladas, NORAD de 6 digitos, EPOCH ISO-8601 con fraccion de
// segundo, filas invalidas descartadas y contadas, columna requerida ausente
// (falla en vez de inventar), orden de columnas alterado, cache v2 con el CSV
// CRUDO y lectura de un fichero v1 de un build anterior.
// =============================================================================
#include "Data/TleAdapter.h"
#include "tle_csv_fixture.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int g_ok = 0, g_fail = 0;

static void checkBool(const char* what, bool cond) {
    (cond ? g_ok : g_fail) += 1;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static void checkNear(const char* what, double got, double want, double tol) {
    const bool ok = std::fabs(got - want) <= tol;
    (ok ? g_ok : g_fail) += 1;
    std::printf("[%s] %s: got %.6f want %.6f\n", ok ? "OK" : "FAIL", what, got, want);
}

static std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// El instante de referencia del fixture (epoch de sus filas) mas un margen:
// asi las filas no caen fuera del rango de cordura del parser.
static const double kNow = tlefixture::kCatalogEpochUtc + 3600.0;

int main() {
    std::printf("--- C2b-CSV-1: cabecera real, 3 filas ---\n");
    {
        TleSnapshot snap;
        const bool ok = TleAdapter::parseCelesTrakCsv(tlefixture::catalogCsv(), kNow, snap);
        checkBool("el CSV real se parsea", ok && snap.valid);
        checkBool("3 objetos", snap.sats.size() == 3);
        checkBool("el origen es CelesTrak", snap.source == TleSource::Celestrak);
        checkBool("el epoch del snapshot es el de descarga", snap.timeUtc == kNow);
        if (snap.sats.size() == 3) {
            checkBool("NORAD leido como entero", snap.sats[0].noradId == 25544);
            checkBool("nombre sin comillas sobrantes", snap.sats[0].name == "ISS (ZARYA)");
            checkNear("EPOCH con fraccion de segundo", snap.sats[0].el.epochUtc,
                      tlefixture::kCatalogEpochUtc, 1e-6);
            checkNear("movimiento medio (rev/dia)", snap.sats[0].el.meanMotionRevDay,
                      15.49065369, 1e-8);
            checkNear("excentricidad", snap.sats[0].el.ecc, 0.0006932, 1e-10);
            checkNear("inclinacion", snap.sats[0].el.incDeg, 51.6336, 1e-6);
            checkBool("elementos marcados validos", snap.sats[0].el.valid);
            checkBool("en el camino CSV no se guardan lineas TLE",
                      snap.sats[0].line1.empty() && snap.sats[0].line2.empty());
        }
    }

    std::printf("--- C2b-CSV-2: nombre con coma, comillas dobladas y NORAD de 6 digitos ---\n");
    {
        std::string csv = tlefixture::csvHeader();
        csv += tlefixture::csvRow("\"SAT, \"\"TEST\"\" 6DIG\"", 123456,
                                  "2026-10-25T14:18:26.840000", "26001A");
        csv += tlefixture::csvRow("PLANO", 99999, "2026-10-25T15:18:26.000000");

        TleSnapshot snap;
        checkBool("CSV con comillas se parsea", TleAdapter::parseCelesTrakCsv(csv, kNow, snap));
        checkBool("2 objetos", snap.sats.size() == 2);
        if (snap.sats.size() == 2) {
            checkBool("la coma dentro de comillas no parte el campo",
                      snap.sats[0].name == "SAT, \"TEST\" 6DIG");
            checkBool("NORAD de 6 digitos conservado", snap.sats[0].noradId == 123456);
            checkNear("la fraccion de segundo se conserva", snap.sats[0].el.epochUtc,
                      tlefixture::kCatalogEpochUtc + 0.00032, 1e-5);
            // 14:18:26.840000 -> 15:18:26.000000 son 3599.16 s exactos.
            checkNear("la diferencia entre filas es la de los EPOCH",
                      snap.sats[1].el.epochUtc - snap.sats[0].el.epochUtc, 3599.16, 1e-6);
        }
    }

    std::printf("--- C2b-CSV-3: fila invalida, columna ausente y orden alterado ---\n");
    {
        // MEAN_MOTION 0 no es un satelite terrestre: se descarta y se cuenta.
        std::string csv = tlefixture::csvHeader();
        csv += tlefixture::csvRow("BUENO", 25544, "2026-10-25T14:18:26.000000");
        std::string malo = tlefixture::csvRow("MALO", 90001, "2026-10-25T14:18:26.000000");
        const size_t mmAt = malo.find(",15.49065369,");
        malo.replace(mmAt, 14, ",0.00000000,");   // MEAN_MOTION = 0
        csv += malo;

        TleSnapshot snap;
        checkBool("con una fila invalida sigue habiendo catalogo",
                  TleAdapter::parseCelesTrakCsv(csv, kNow, snap) && snap.sats.size() == 1);
        checkBool("la nota declara el descarte", snap.note.find("descartados") != std::string::npos);

        // Sin MEAN_ANOMALY en la cabecera: no se puede construir la orbita, y
        // es mejor fallar que inventar un valor.
        std::string sinCol = tlefixture::catalogCsv();
        const std::string col = "MEAN_ANOMALY";
        const size_t at = sinCol.find(col);
        checkBool("se quita la columna requerida del fixture", at != std::string::npos);
        sinCol.erase(at, col.size());
        TleSnapshot none;
        checkBool("columna requerida ausente -> false (no inventa)",
                  !TleAdapter::parseCelesTrakCsv(sinCol, kNow, none) && !none.valid);

        // Orden distinto de columnas: se localizan por nombre.
        std::string reordered =
            "MEAN_ANOMALY,NORAD_CAT_ID,OBJECT_NAME,EPOCH,MEAN_MOTION,ECCENTRICITY,"
            "INCLINATION,RA_OF_ASC_NODE,ARG_OF_PERICENTER\n";
        reordered += "47.4951,25544,\"ISS, REORDER\",2026-10-26T14:18:26.839680,15.49065369,"
                     "0.0006932,51.6336,9.8132,312.5452\n";
        TleSnapshot re;
        checkBool("el orden de columnas no importa",
                  TleAdapter::parseCelesTrakCsv(reordered, kNow, re) && re.sats.size() == 1);
        if (re.sats.size() == 1) {
            checkBool("nombre y NORAD correctos tras reordenar",
                      re.sats[0].name == "ISS, REORDER" && re.sats[0].noradId == 25544);
        }

        // Cuerpo vacio o solo cabecera: no hay catalogo.
        TleSnapshot empty;
        checkBool("solo cabecera -> false",
                  !TleAdapter::parseCelesTrakCsv(tlefixture::csvHeader(), kNow, empty));
        checkBool("cuerpo vacio -> false", !TleAdapter::parseCelesTrakCsv("", kNow, empty));
    }

    std::printf("--- C2b-CSV-4: cache v2 guarda el CSV crudo y lo relee ---\n");
    {
        const fs::path dir = fs::temp_directory_path() / "tle_csv_v2";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const fs::path cache = dir / "satellites.tle";

        const std::string csv = tlefixture::catalogCsv();
        TleSnapshot snap;
        TleAdapter::parseCelesTrakCsv(csv, kNow, snap);
        checkBool("saveCache con cuerpo crudo", TleAdapter::saveCache(cache, snap, csv));

        const std::string onDisk = readText(cache);
        checkBool("cabecera v2", onDisk.compare(0, 15, "# tle-cache v2 ") == 0);
        checkBool("declara format=csv", onDisk.find("format=csv") != std::string::npos);
        checkBool("declara source=celestrak", onDisk.find("source=celestrak") != std::string::npos);
        checkBool("el CSV se guarda TAL CUAL (cabecera de 19 columnas)",
                  onDisk.find("OBJECT_NAME,OBJECT_ID,NORAD_CAT_ID") != std::string::npos);

        TleSnapshot back;
        checkBool("loadCache relee el CSV",
                  TleAdapter::loadCache(cache, kNow + 60.0, back) && back.valid);
        checkBool("mismo numero de objetos", back.sats.size() == 3);
        checkBool("origen Cache (no Celestrak: viene del disco)",
                  back.source == TleSource::Cache);
        checkNear("el EPOCH por objeto sobrevive al viaje",
                  back.sats[0].el.epochUtc, tlefixture::kCatalogEpochUtc, 1e-6);
        // La cabecera guarda el epoch con %.1f, asi que el instante se conserva
        // con precision de 0,1 s (de sobra para un TTL de horas).
        checkNear("la edad del snapshot es la de la cabecera", back.timeUtc, kNow, 0.1);
        checkBool("caduca a las 7 h (TTL por defecto 6 h)",
                  !TleAdapter::loadCache(cache, kNow + 7.0 * 3600.0, back));

        // Un NORAD de 6 digitos tambien sobrevive a la cache (esto es lo que un
        // TLE de 5 columnas no puede hacer).
        std::string six = tlefixture::csvHeader();
        six += tlefixture::csvRow("SEIS", 123456, "2026-10-25T14:18:26.000000");
        TleSnapshot s6;
        TleAdapter::parseCelesTrakCsv(six, kNow, s6);
        TleAdapter::saveCache(cache, s6, six);
        TleSnapshot s6back;
        checkBool("NORAD de 6 digitos sobrevive a la cache",
                  TleAdapter::loadCache(cache, kNow, s6back) && s6back.sats.size() == 1 &&
                      s6back.sats[0].noradId == 123456);

        fs::remove_all(dir, ec);
    }

    std::printf("--- C2b-CSV-5: fichero v1 de un build anterior sigue leyendose ---\n");
    {
        const fs::path dir = fs::temp_directory_path() / "tle_csv_v1";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const fs::path cache = dir / "satellites.tle";

        // Formato v1: cabecera sin format= y cuerpo TLE de 3 lineas.
        std::string v1 = "# tle-cache v1 epoch=1793000000.0 count=2 source=celestrak\n";
        std::string l1, l2;
        tlefixture::tleLines(25544, l1, l2);
        v1 += "ISS (ZARYA)\n" + l1 + "\n" + l2 + "\n";
        tlefixture::tleLines(90001, l1, l2);
        v1 += "SAT-90001\n" + l1 + "\n" + l2 + "\n";
        {
            std::ofstream f(cache, std::ios::binary | std::ios::trunc);
            f << v1;
        }

        TleSnapshot snap;
        checkBool("un fichero v1 se sigue leyendo",
                  TleAdapter::loadCache(cache, 1793000000.0 + 60.0, snap) && snap.valid);
        checkBool("2 objetos del fichero v1", snap.sats.size() == 2);
        checkNear("la cabecera v1 marca la edad", snap.timeUtc, 1793000000.0, 1e-6);
        checkNear("cacheEpoch tambien entiende v1", TleAdapter::cacheEpoch(cache), 1793000000.0,
                  1e-6);

        fs::remove_all(dir, ec);
    }

    std::printf("\n%d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}



