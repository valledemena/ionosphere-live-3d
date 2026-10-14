// =============================================================================
// test_solar_epoch - espejo C++ del fix J2000 (H1-261, ruling 266 sec.4).
//
// Test de logica pura: sin GL, sin red. Llama a PRODUCCION
// (SolarPosition::getDirection) con el motor corregido (-10957.5).
//
// Pines (nota 263 sec.2 + puerto Python independiente, veredicto 262):
//   J2000.0 (Unix 946728000): j2000 == 0.0 por definicion (GMST IAU
//     280.46061837) -> subsolar (-23.03, +0.83) +-0.01 (declinacion de
//     perihelio + EoT ~-3.5 min).
//   EPOCH_12Z (2026-10-07T12:00Z = 1791374400): subsolar (-5.62, -3.04)
//     +-0.05 (el valor viejo (-5.79, -3.07) era el bug por ambos lados).
// Decodificacion subsolar: espejo de motorToLatLon (web solar.ts),
// misma convencion que IonosondeLayer.cpp:249 (theta = pi - lon).
// =============================================================================
#include "Utils/SolarPosition.h"

#include <cmath>
#include <cstdio>

static const double PI = 3.14159265358979323846;
static const double RAD = 180.0 / PI;

static int g_failures = 0;
static int g_checks = 0;

static void expectNear(const char* what, double got, double want, double tol) {
    ++g_checks;
    const bool ok = std::fabs(got - want) <= tol;
    if (!ok) ++g_failures;
    std::printf("  %-58s got=%+.4f want=%+.4f tol=%.3f [%s]\n",
                what, got, want, tol, ok ? "OK" : "FAIL");
}

// Espejo motorToLatLon: dir unitario (marco motor) -> (lat, lon) grados.
static void motorToLatLon(double x, double y, double z, double& lat, double& lon) {
    const double r = std::sqrt(x * x + y * y + z * z);
    lat = std::asin(y / r) * RAD;
    lon = 180.0 - std::atan2(z, x) * RAD;
    while (lon < -180.0) lon += 360.0;
    while (lon > 180.0) lon -= 360.0;
}

int main() {
    std::printf("== solar_epoch: anclaje J2000.0 + EPOCH_12Z ==\n");
    {
        // Ancla definicional: en J2000.0 el dia juliano es 0 por construccion
        // (con el signo erroneo daba 21915.0 = 60 anos julianos exactos).
        const glm::vec3 d = SolarPosition::getDirection(946728000.0);
        double lat = 0.0, lon = 0.0;
        motorToLatLon(d.x, d.y, d.z, lat, lon);
        expectNear("J2000.0 subsolar lat", lat, -23.03, 0.01);
        expectNear("J2000.0 subsolar lon", lon, 0.83, 0.01);
    }
    {
        const glm::vec3 d = SolarPosition::getDirection(1791374400.0);
        double lat = 0.0, lon = 0.0;
        motorToLatLon(d.x, d.y, d.z, lat, lon);
        expectNear("EPOCH_12Z subsolar lat", lat, -5.62, 0.05);
        expectNear("EPOCH_12Z subsolar lon", lon, -3.04, 0.05);
    }
    std::printf("== solar_epoch: %d checks, %d failures ==\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
