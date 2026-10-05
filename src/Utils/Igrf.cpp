// Igrf (M'-1): implementacion. Transcripcion propia de la receta publicada
// (IAGA cookbook / codigo de referencia NOAA): recursion Schmidt con k(n,m),
// componentes (Br, Bteta, Bphi), rotacion ECEF->NED geodesica.
// Coeficientes: IGRF-14 main-field, grado 1..8, epoca 2025.0 congelada
// (columna 2025.0 del fichero oficial; SV 2025-30 NO aplicada por spec).
// Fuente: https://www.ngdc.noaa.gov/IAGA/vmod/coeffs/igrf14coeffs.txt
// (recuperado 2026-10-05; g10(2025) = -29350.0 nT como testigo).
#include "Utils/Igrf.h"

namespace igrf {
namespace {

// Un registro por coeficiente: (n, m, esH, valor nT).
struct Coef {
    int n, m, isH;
    double v;
};

// Tabla estatica grado 1..8, epoca 2025.0 (80 entradas).
// Orden: n ascendente; por n: g_n^0, luego pares (g_n^m, h_n^m).
// (n, m, esH, valor nT). Transcrita de la columna 2025.0 del fichero oficial.
const Coef kCoefs[80] = {
    {1, 0, 0, -29350.00f},
    {1, 1, 0, -1410.30f},
    {1, 1, 1, 4545.50f},
    {2, 0, 0, -2556.20f},
    {2, 1, 0, 2950.90f},
    {2, 1, 1, -3133.60f},
    {2, 2, 0, 1648.70f},
    {2, 2, 1, -814.20f},
    {3, 0, 0, 1360.90f},
    {3, 1, 0, -2404.20f},
    {3, 1, 1, -56.90f},
    {3, 2, 0, 1243.80f},
    {3, 2, 1, 237.60f},
    {3, 3, 0, 453.40f},
    {3, 3, 1, -549.60f},
    {4, 0, 0, 894.70f},
    {4, 1, 0, 799.60f},
    {4, 1, 1, 278.60f},
    {4, 2, 0, 55.80f},
    {4, 2, 1, -134.00f},
    {4, 3, 0, -281.10f},
    {4, 3, 1, 212.00f},
    {4, 4, 0, 12.00f},
    {4, 4, 1, -375.40f},
    {5, 0, 0, -232.90f},
    {5, 1, 0, 369.00f},
    {5, 1, 1, 45.30f},
    {5, 2, 0, 187.20f},
    {5, 2, 1, 220.00f},
    {5, 3, 0, -138.70f},
    {5, 3, 1, -122.90f},
    {5, 4, 0, -141.90f},
    {5, 4, 1, 42.90f},
    {5, 5, 0, 20.90f},
    {5, 5, 1, 106.20f},
    {6, 0, 0, 64.30f},
    {6, 1, 0, 63.80f},
    {6, 1, 1, -18.40f},
    {6, 2, 0, 76.70f},
    {6, 2, 1, 16.80f},
    {6, 3, 0, -115.70f},
    {6, 3, 1, 48.90f},
    {6, 4, 0, -40.90f},
    {6, 4, 1, -59.80f},
    {6, 5, 0, 14.90f},
    {6, 5, 1, 10.90f},
    {6, 6, 0, -60.80f},
    {6, 6, 1, 72.80f},
    {7, 0, 0, 79.60f},
    {7, 1, 0, -76.90f},
    {7, 1, 1, -48.90f},
    {7, 2, 0, -8.80f},
    {7, 2, 1, -14.40f},
    {7, 3, 0, 59.30f},
    {7, 3, 1, -1.00f},
    {7, 4, 0, 15.80f},
    {7, 4, 1, 23.50f},
    {7, 5, 0, 2.50f},
    {7, 5, 1, -7.40f},
    {7, 6, 0, -11.20f},
    {7, 6, 1, -25.10f},
    {7, 7, 0, 14.30f},
    {7, 7, 1, -2.20f},
    {8, 0, 0, 23.10f},
    {8, 1, 0, 10.90f},
    {8, 1, 1, 7.20f},
    {8, 2, 0, -17.50f},
    {8, 2, 1, -12.60f},
    {8, 3, 0, 2.00f},
    {8, 3, 1, 11.50f},
    {8, 4, 0, -21.80f},
    {8, 4, 1, -9.70f},
    {8, 5, 0, 16.90f},
    {8, 5, 1, 12.70f},
    {8, 6, 0, 14.90f},
    {8, 6, 1, 0.70f},
    {8, 7, 0, -16.80f},
    {8, 7, 1, -5.20f},
    {8, 8, 0, 1.00f},
    {8, 8, 1, 3.90f},
};

constexpr double kAkm = 6378.137;          // WGS-84 semieje mayor (km)
constexpr double kInvF = 298.257223563;      // WGS-84 1/f
constexpr double kArefKm = 6371.2;           // radio de referencia IGRF (km)
constexpr int kNmax = 8;

}  // namespace

double faradayK() {
    // CODATA 2022 (exactas donde aplique): e, c exactas por definicion SI.
    constexpr double e = 1.602176634e-19;      // C, carga elemental
    constexpr double eps0 = 8.8541878128e-12;  // F/m, permitividad del vacio
    constexpr double me = 9.1093837015e-31;    // kg, masa del electron
    constexpr double c = 299792458.0;          // m/s, velocidad de la luz
    constexpr double pi = 3.141592653589793;
    return (e * e * e) / (8.0 * pi * pi * eps0 * me * me * c);
}

Ned fieldNed(double latDeg, double lonDeg, double altKm) {
    // Clamp declarado [0, 1000] km.
    if (altKm < 0.0) altKm = 0.0;
    if (altKm > 1000.0) altKm = 1000.0;
    // WGS-84: geodesico (lat, lon, h) -> ECEF (km).
    constexpr double pi = 3.141592653589793;
    const double f = 1.0 / kInvF;
    const double e2 = f * (2.0 - f);
    const double la = latDeg * pi / 180.0;
    const double lo = lonDeg * pi / 180.0;
    const double s = sin(la), c = cos(la);
    const double Np = kAkm / sqrt(1.0 - e2 * s * s);
    const double X = (Np + altKm) * c * cos(lo);
    const double Y = (Np + altKm) * c * sin(lo);
    const double Z = (Np * (1.0 - e2) + altKm) * s;
    const double r = sqrt(X * X + Y * Y + Z * Z);
    const double thc = acos(Z / r);  // colatitud geocentrica
    const double lam = atan2(Y, X);
    // Legendre Schmidt semi-normalizada + derivada (recursion estandar).
    double P[9][9] = {};
    double dP[9][9] = {};
    P[0][0] = 1.0;
    const double st = sin(thc), ct = cos(thc);
    for (int n = 1; n <= kNmax; ++n) {
        for (int m = 0; m <= n; ++m) {
            // OJO: aritmetica en double (la division entera truncaria k).
            const double k = (n > 1) ? (double)((n - 1) * (n - 1) - m * m) /
                                           (double)((2 * n - 1) * (2 * n - 3))
                                     : 0.0;
            const double pN1 = P[n - 1][m];
            const double dN1 = dP[n - 1][m];
            const double pN2 = (m <= n - 2) ? P[n - 2][m] : 0.0;
            const double dN2 = (m <= n - 2) ? dP[n - 2][m] : 0.0;
            if (n == m) {
                P[n][m] = st * P[n - 1][m - 1];
                dP[n][m] = st * dP[n - 1][m - 1] + ct * P[n - 1][m - 1];
            } else if (n == 1) {
                P[n][m] = ct * pN1;
                dP[n][m] = ct * dN1 - st * pN1;
            } else {
                P[n][m] = ct * pN1 - k * pN2;
                dP[n][m] = ct * dN1 - st * pN1 - k * dN2;
            }
        }
    }
    // Suma de armadura esferica (nT).
    double Br = 0.0, Bt = 0.0, Bp = 0.0;
    for (const Coef& cf : kCoefs) {
        const double q = pow(kArefKm / r, cf.n + 2);
        const double cm = cos(cf.m * lam), sm = sin(cf.m * lam);
        const double g = cf.isH ? 0.0 : cf.v;
        const double h = cf.isH ? cf.v : 0.0;
        const double B = g * cm + h * sm;
        Br += (cf.n + 1) * q * B * P[cf.n][cf.m];
        Bt += -q * B * dP[cf.n][cf.m];
        if (st > 1e-9)
            Bp += -q * cf.m * (-g * sm + h * cm) * P[cf.n][cf.m] / st;
    }
    // (Br, Bt, Bp) geocentricos -> ECEF.
    const double stc = sin(thc), ctc = cos(thc);
    const double sl = sin(lam), cl = cos(lam);
    const double Fx = Br * stc * cl + Bt * ctc * cl + Bp * -sl;
    const double Fy = Br * stc * sl + Bt * ctc * sl + Bp * cl;
    const double Fz = Br * ctc + Bt * -stc;
    // ECEF -> NED geodesico.
    const double nx = -s * cos(lo), ny = -s * sin(lo), nz = c;
    const double ex = -sin(lo), ey = cos(lo);
    const double ux = c * cos(lo), uy = c * sin(lo), uz = s;
    Ned out;
    out.n = (float)(Fx * nx + Fy * ny + Fz * nz);
    out.e = (float)(Fx * ex + Fy * ey);
    out.d = (float)-(Fx * ux + Fy * uy + Fz * uz);
    return out;
}

}  // namespace igrf
