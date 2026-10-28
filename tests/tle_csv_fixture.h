#pragma once
// =============================================================================
// Fixture compartido del GP CSV de CelesTrak (FORMAT=csv).
//
// La cabecera es la REAL que publica CelesTrak para gp.php?FORMAT=csv, con las
// 19 columnas del formato OMM/GP. Se usa en dos tests: el del parser (puro) y
// el de orquestacion (que ahora recibe CSV, no TLE de 3 lineas).
//
// Los elementos de las filas son los del TLE real de la ISS (epoch
// 26299.59614366) para que ambos caminos -TLE y CSV- describan la misma orbita
// y las aserciones de posicion sigan siendo comparables. Los NORAD 90001+ son
// ficticios y estan declarados como tales.
// =============================================================================
#include <cstdio>
#include <string>

namespace tlefixture {

// Cabecera de 19 columnas, en el orden en que la publica CelesTrak.
inline const char* csvHeader() {
    return "OBJECT_NAME,OBJECT_ID,NORAD_CAT_ID,OBJECT_TYPE,CLASSIFICATION,INTL_DESIG,EPOCH,"
           "MEAN_MOTION,ECCENTRICITY,INCLINATION,RA_OF_ASC_NODE,ARG_OF_PERICENTER,"
           "MEAN_ANOMALY,EPHEMERIS_TYPE,ELEMENT_SET_NO,REV_AT_EPOCH,BSTAR,MEAN_MOTION_DOT,"
           "MEAN_MOTION_DDOT\n";
}

// Fila con los campos que importan y el resto con valores plausibles. `name`
// se escribe TAL CUAL: el llamante decide si lo entrecomilla (un nombre con
// coma DEBE ir entre comillas, y una comilla literal se dobla).
inline std::string csvRow(const std::string& name, int norad, const std::string& epochIso,
                          const std::string& objectId = "98067A") {
    char buf[640];
    std::snprintf(buf, sizeof(buf),
                  "%s,%s,%d,PAYLOAD,U,1998-067A,%s,15.49065369,0.0006932,51.6336,9.8132,"
                  "312.5452,47.4951,0,999,59212,0.00011933,0.00006146,0\n",
                  name.c_str(), objectId.c_str(), norad, epochIso.c_str());
    return buf;
}

// Catalogo de 3 objetos (los mismos NORAD que el fixture TLE de
// test_orchestration: 25544, 90001, 90002).
inline std::string catalogCsv() {
    return std::string(csvHeader()) +
           csvRow("ISS (ZARYA)", 25544, "2026-10-25T14:18:26.839680") +
           csvRow("SAT-90001", 90001, "2026-10-25T14:18:26.839680") +
           csvRow("SAT-90002", 90002, "2026-10-25T14:18:26.839680");
}

// Epoch de 2026-10-26T14:18:26.839680 UTC, calculado aparte (no con el codigo
// bajo prueba): 1792972800 + 14*3600 + 18*60 + 26.83968.
inline constexpr double kCatalogEpochUtc = 1792937906.839680;

// --- TLE de 3 lineas para el camino SatNOGS y para ficheros v1 --------------
// parseTle exige 69 columnas y checksum valido, asi que las lineas se generan
// a partir del TLE real de la ISS cambiando solo el numero de catalogo y
// recalculando el digito de control.
inline std::string tleWithChecksum(std::string body) {
    int sum = 0;
    for (char c : body) {
        if (c >= '0' && c <= '9') sum += c - '0';
        else if (c == '-') sum += 1;
    }
    body += (char)('0' + (sum % 10));
    return body;
}

inline void tleLines(int norad, std::string& l1, std::string& l2) {
    char n[8];
    std::snprintf(n, sizeof(n), "%05d", norad);
    std::string b1 = "1 25544U 98067A   26299.59614366  .00006146  00000-0  11933-3 0  999";
    std::string b2 = "2 25544  51.6336   9.8132 0006932 312.5452  47.4951 15.4906536959212";
    b1.replace(2, 5, n);
    b2.replace(2, 5, n);
    l1 = tleWithChecksum(b1);
    l2 = tleWithChecksum(b2);
}

}  // namespace tlefixture

