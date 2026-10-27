#pragma once
#include <string>

// OrbitProp: TLE -> posicion en la escena. Modulo PURO (sin red, sin GL,
// sin App), testeable en cualquier CI con tests/test_orbit_prop.cpp.
//
// MODELO (declarado): elementos MEDIOS del TLE + deriva secular J2
// (RAANdot, argpdot, Mdot) y Kepler resuelto con Newton. NO hay arrastre,
// ni terminos periodicos cortos, ni resonancia, ni SDP4:
//   "visual-grade: TLE mean elements + J2 secular, no SGP4/SDP4;
//    posicion visual, no para tracking".
// El error esperado crece con la edad del TLE (decenas de km en LEO joven,
// cientos con TLEs viejos). Sirve para DIBUJAR el enjambre, no para apuntar.
//
// EPOCH: columnas 19-32 del TLE (YYDDD.DDDDDDDD). El pivote es el estandar:
// YY 57-99 -> 19YY, YY 00-56 -> 20YY. Se convierte a segundos UTC (Unix).
//
// ECEF: el marco inercial se rota por GMST (Vallado, ecuacion 3-45):
//   JD   = utc/86400 + 2440587.5
//   T    = (JD - 2451545.0)/36525
//   temp = -6.2e-6*T^3 + 0.093104*T^2
//          + (876600*3600 + 8640184.812866)*T + 67310.54841   [segundos]
//   gmst = fmod(temp * pi/180 / 240, 2*pi)
// Sin esta rotacion los puntos caerian en la longitud equivocada: el globo
// del motor es estatico y no gira con la Tierra.
//
// CONSTANTES: mu = 398600.4418 km3/s2, Re = 6378.137 km (WGS84), J2 =
// 1.08262668e-3. El motor usa 1 unidad = 1 R_E = 6371 km.
//
// CONVENCION DEL MOTOR (RefShells/GridLayer/WsprArcs):
//   phi = pi/2 - radians(lat), theta = pi - radians(lon)
//   x = r*sin(phi)*cos(theta), y = r*cos(phi), z = r*sin(phi)*sin(theta)
// con r = 1 + altitud/6371.
namespace orbitprop {

// Elementos medios de un TLE. Angulos en grados, epoca en segundos UTC.
struct OrbitElements {
    double epochUtc = 0.0;          // epoca del TLE (segundos UTC Unix)
    double incDeg = 0.0;            // inclinacion
    double raanDeg = 0.0;           // ascension recta del nodo ascendente
    double ecc = 0.0;               // excentricidad (decimal implicito)
    double argpDeg = 0.0;           // argumento del perigeo
    double meanAnomDeg = 0.0;       // anomalia media en la epoca
    double meanMotionRevDay = 0.0;  // movimiento medio (rev/dia)
    bool valid = false;             // true solo tras un parseo correcto
};

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kDeg = kPi / 180.0;
inline constexpr double kMu = 398600.4418;        // km3/s2
inline constexpr double kEarthRadiusKm = 6378.137;  // WGS84, para el TLE
inline constexpr double kJ2 = 1.08262668e-3;
inline constexpr double kEngineRadiusKm = 6371.0;   // 1 unidad = 1 R_E
inline constexpr int kTleLineLen = 69;

// Checksum estandar del TLE: columnas 1-68 (digitos + signos menos) modulo
// 10 == columna 69. Un TLE con checksum roto se descarta (nunca se corrige).
bool tleChecksumOk(const std::string& line);

// Parsea linea 1 + linea 2. Devuelve false y NO toca `out` si algo no encaja
// (longitud, prefijos, campos no numericos, rangos imposibles o checksum).
bool parseTle(const std::string& line1, const std::string& line2, OrbitElements& out);

// YYDDD.DDDDDDDD -> segundos UTC (Unix).
double tleEpochToUtc(int yy, double dayOfYear);

// GMST (rad) de un instante UTC. Formula documentada arriba.
double gmstRad(double utc);

// Semieje mayor (km), periodo (min) y altitud media (a - Re, km).
double semiMajorAxisKm(const OrbitElements& el);
double orbitalPeriodMin(const OrbitElements& el);
double meanAltitudeKm(const OrbitElements& el);

// Deriva secular J2 del RAAN en grados/dia (negativa si es progrado).
// Se expone para documentar el modelo y para el test; no hace falta dibujar.
double j2RaanDotDegPerDay(const OrbitElements& el);

// Kepler: M (rad) + e -> E (rad) por Newton. Normaliza M a [-pi, pi].
bool solveKepler(double meanAnomRad, double ecc, double& eccAnomRad);

// Posicion inercial (marco del TLE) en km para el instante UTC dado.
bool propagateTeme(const OrbitElements& el, double utc, double rKm[3]);
// Posicion ECEF (km): propagateTeme + rotacion GMST del mismo instante.
bool propagateEcef(const OrbitElements& el, double utc, double rKm[3]);

// Rotacion TEME -> ECEF por GMST (solo rotacion sobre z; sin nutacion).
void temeToEcef(const double rTemeKm[3], double gmst, double rEcefKm[3]);

// ECEF (km) -> lat/lon geodeticas (grados, WGS84) y altitud (km).
void ecefToGeodetic(const double rEcefKm[3], double& latDeg, double& lonDeg, double& altKm);

// lat/lon/alt -> coordenadas del motor (1 unidad = 1 R_E = 6371 km).
void latLonAltToScene(double latDeg, double lonDeg, double altKm,
                      double& x, double& y, double& z);

// Atajo: TLE -> punto de la escena + altitud geodetica (km).
bool toScene(const OrbitElements& el, double utc,
             double& x, double& y, double& z, double& altKm);

}  // namespace orbitprop
