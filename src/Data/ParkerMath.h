#pragma once
// =============================================================================
// Lote 5 / modulo D: espiral de Parker (geometria pura, sin GL ni red).
//
// FUENTE (verificada, pineada): Parker 1958 — la espiral sale de la rotacion
// solar + viento radial constante:
//   phi(r) = phi0 - OMEGA * (r - r0) / v_sw
// con OMEGA sideral = 2*pi/25.38 d (pineado) y v_sw REAL vivo que ya descarga
// la app (SolarWindAdapter). Espejo exacto de
// ionosphere-web/src/lib/iono/parker.ts.
//
// ESQUEMA DECLARADO: panel 2D cenital (plano ecliptico), Sol en el origen y
// la Tierra FIJA en (1, 0) AU; la espiral es la FOTO INSTANTANEA con el v vivo.
// 1 UA no cabe en la vista 3D (que trabaja en radios terrestres R_E), por eso
// este modulo NO tiene capa 3D: solo el panel.
//
// POLARIDAD DE SECTOR: convencion DECLARADA, no una medida del sector:
//   Bx >= 0 -> "toward" (hacia el Sol, +X GSE)
//   Bx <  0 -> "away"
// El 0 exacto cae del lado "toward" (misma regla que findCrossings).
//
// FRONTERAS DE SECTOR: cruce CALCULADO del dato (cambio de signo entre
// muestras adyacentes). Limites declarados:
//   - 0 exacto cuenta como >= 0 ("toward"), no rompe la serie.
//   - Huecos mayores de 2 h entre muestras se IGNORAN (no hay cruce: la
//     frontera pudo pasar dentro del hueco y no se puede afirmar).
//   - Muestras no finitas o tiempos no ascendentes se ignoran.
//   - Salida ASCENDENTE (mismo orden que la entrada).
// Sin cruce real en la serie no hay cruce: nunca se sintetiza una frontera.
//
// Sin red, sin GL, sin ImGui: solo <vector>/<cstddef>.
// =============================================================================
#include <cstddef>
#include <vector>

namespace parker {

// Constantes pineadas (mismas del demo web).
extern const double SIDEREAL_ROTATION_DAYS;  // 25.38 d (rotacion sideral)
extern const double AU_KM;                   // 1.496e8 km

// Punto del brazo espiral. r en AU, phi en grados (0 = Tierra; positivo hacia
// el pie del brazo, negativo hacia fuera). x/y en AU del plano ecliptico,
// convencion display: norte arriba, antihorario positivo.
struct Pt {
    double r = 0.0;
    double phi = 0.0;
    double x = 0.0;
    double y = 0.0;
};

// Muestra de Bx GSE (nT) con su epoch UTC en segundos.
struct BxSample {
    double timeUtc = 0.0;
    float bx = 0.0f;
};

// Angulo de atraso (grados) a distancia rAu con viento vKms (Parker 1958).
// vKms <= 0 (o no finito) -> 0 (sin viento no hay espiral).
double spiralLagDeg(double vKms, double rAu = 1.0);

// Brazo espiral de 0.05 a 1.5 AU con n tramos -> n+1 puntos (n < 1 -> 1).
// phi(r) = lag(1) - lag(r): en la Tierra phi = 0. vKms <= 0 -> puntos
// degenerados (sin NaN: todos los valores finitos).
std::vector<Pt> spiralPoints(double vKms, int n = 64);

// Polaridad declarada del Bx: "toward" (bx >= 0) | "away" (bx < 0).
const char* sectorPolarity(float bxNt);

// Epochs de los cruces de sector (cambio de signo con 0 = "toward", huecos
// > 2 h ignorados). Ascendentes. Sin datos -> vector vacio.
std::vector<double> findCrossings(const std::vector<BxSample>& series);

}  // namespace parker
