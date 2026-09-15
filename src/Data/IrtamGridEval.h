#pragma once
// =============================================================================
// M-IRTAM-F2 (P1b): evaluacion Jones-Gallet a la malla 46x45 (port del reader).
//
// Port auditado de IrtamReader_Fortran_V1.0.zip (drop 002, custodia 997A43C7):
//   src/IrtamReader.for (363 lin) + src/igrf.for (solo closure igrf_dip) —
//   sun.for SIN llamadas desde el reader (fuera de scope, documentado).
//
// MAPA DE TIPOS (hallazgo critico GLM #2 — fila exigida en la matriz):
//   Fortran REAL*4 (float)  -> C++ float : args GAMMA2, SFE (coeffs TRUNCADOS
//     a float al leer 4E16.8 — el parser P1a guarda double, el eval castea),
//     UMR/PI, XMIN, HOU, SS/S3/XSINX/S0/S1/S2, TODO FELDG/igrf_dip.
//   Fortran REAL*8 (double) -> C++ double: C/S/COEF/SUM (GAMMA2), X/F0/F
//     (FELDCOF/Schmidt). `GAMMA2=SUM` trunca a float al retornar; el producto
//     de pendiente SFE(MMM+I)*XMIN redondea en SINGLE antes del add.
//   Un port double-en-bloque difiere en el 7o digito — NO es exacto.
//
// K-LOOP DE FELDG (hallazgo critico GLM #3): el arithmetic-IF `IF(I-1) 5,4,2`
// con FALL-THROUGH secuencial — la rama label-2 (DO 3 M) CAE a label-4 y
// label-5 SIEMPRE por ciclo; pases K=1 y K=3 escriben los mismos bloques,
// K=3 es el ultimo escritor. Transcripcion con fall-through explicito abajo.
//
// Scope taxativo (GLM #1): solo rama EXTRASHC year>=2015 (oracle 2016.6 y
// produccion 2026.x); solo igrf2015.dat + igrf2015s.dat embebidos
// (IrtamIgrfCoeffs.inc); dgrf2000/dgrf2005/igrf2010 dormidos. FELDCOF con
// hoisting permitido (una vez por ano — determinista, identico).
//
// Malla oficial pineada (drop 006): width=45 lons, height=46 lats;
//   lat(i) = -90 + 4*i (i 0..45), lon(j) = -180 + 8*j (j 0..44 -> -180..172);
//   values[lat*width+lon], fila 0 = SUR (convencion E2). ryear produccion =
//   ano decimal del TOV; altura modip 300 km (ejemplo). Sin gfortran en este
//   lado (desviacion documentada): oraculo cruzado via replica float32.
// =============================================================================
#include "Data/IrtamCoeffParse.h"  // Message (pares evaluados F2-B0B1-R1)
#include "Ionosphere/GridLayer.h"  // GridData (mallas del bundle)

#include <string>
#include <vector>

namespace irtameval {

inline constexpr int kGridW = 45;
inline constexpr int kGridH = 46;
inline constexpr int kCoeffN = 1064;  // 76 espacial x 13 diurno + 76 slope
inline constexpr int kSpatial = 76;
inline constexpr int kTemporal = 14;  // 2*IHARM+1, IHARM=6
inline constexpr float kModipHeightKm = 300.0f;

double latForRow(int i);  // -90 + 4*i
double lonForCol(int j);  // -180 + 8*j

// Ano decimal para modip desde epoch TOV (UTC s): year + (doy-1)/365or366.
double decimalYearForTov(double tovEpoch);

// xmodip via closure igrf_dip (float por construccion). false si year<2015
// (fuera del scope EXTRASHC pineado) o entrada no finita.
bool igrfDip(double latDeg, double lonDeg, double decimalYear, double heightKm,
             double& xmodipOut);

// Un punto Jones-Gallet (FOUT1->GAMMA2, semantica mixta). coeffs = 1064
// doubles P1a (se truncan a float internamente). hourUt y tovHoras en horas
// decimales UT (ejemplo: TOV=12).
float fout1(float xmodip, float lat, float lon, float hourUt, float tov,
            const double* coeffs1064);

// Malla completa 46x45 a HOUR==TOV (XMIN=720). Devuelve row-major E2
// (46*45 = 2070). false si coeffs.size()!=1064 o year<2015.
bool evalGrid(const std::vector<double>& coeffs1064, double decimalYear,
              float hourUt, float tov, std::vector<float>& out2070);

// F2-B0B1-R1 Q2 (gate por parejas): evalua un par coherente a mallas 46x45.
// Exige mensajes validos, basis standard en ambos y MISMO TOV (coherencia
// de instante del par). Rellena g1/g2 (GridData 45x46 + timestamp + valid)
// o devuelve false sin tocar nada (fallo del par = capas ausentes, Q2).
// Puro (sin I/O): testeable en CI. Lo usa App para B0xB1.
bool evalPairGrids(const irtamcoeff::Message& m1, const irtamcoeff::Message& m2,
                   double decimalYear, float hourUt, float tov, GridData& g1,
                   GridData& g2);

}  // namespace irtameval
