// test_hf_circuit.cpp — M8 (plan 11): fisica del circuito HF (namespace hfc).
// GENERADO por scripts/m8_gen_test.py: esperanzas de una SEGUNDA
// implementacion Python independiente — nada transcrito a mano. Llama a
// PRODUCCION (hfc:: inline de HFTraceLayer.h sobre HfMath.h), sin GL y
// sin enlazar HFTraceLayer.cpp. Incluye la composicion de la aceptacion 1
// RE-ANCLADA (sello P0): DB049->EG931 real = 481 km, 1 salto, ~50.4 deg.
#include "Ionosphere/HFTraceLayer.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int g_ok = 0, g_fail = 0;

static void check_near(const char* name, double got, double want, double tol) {
    double d = fabs(got - want);
    if (d <= tol) { g_ok++; printf("[OK]   %s: %.6f == %.6f (tol %.1e)\n", name, got, want, tol); }
    else         { g_fail++; printf("[FAIL] %s: %.6f != %.6f (tol %.1e)\n", name, got, want, tol); }
}
static void check_int(const char* name, int got, int want) {
    if (got == want) { g_ok++; printf("[OK]   %s: %d == %d\n", name, got, want); }
    else             { g_fail++; printf("[FAIL] %s: %d != %d\n", name, got, want); }
}
static void check_bool(const char* name, bool cond) {
    if (cond) { g_ok++; printf("[OK]   %s\n", name); }
    else      { g_fail++; printf("[FAIL] %s\n", name); }
}

int main() {
    printf("[test_hf_circuit] M8: fisica del circuito (namespace hfc)\n");
    printf("--- A) geodesia (haversine, R=6371) ---\n");
    check_near("A1 D real DB049->EG931 (km)", hfc::greatCircleKm(50.097, 4.588, 51.685, -1.795), 481.07259457064129, 1.0e-6);
    check_near("A2 cuarto de ecuador (0,0)->(0,90)", hfc::greatCircleKm(0.0, 0.0, 0.0, 90.0), 10007.543398010284, 1.0e-6);
    check_near("A3 antipodal (0,0)->(0,180)", hfc::greatCircleKm(0.0, 0.0, 0.0, 180.0), 20015.086796020572, 1.0e-6);
    check_near("A4 un grado de ecuador", hfc::greatCircleKm(0.0, 0.0, 1.0, 0.0), 111.19492664455873, 1.0e-6);
    check_near("A5 mismo punto -> 0", hfc::greatCircleKm(12.0, -34.0, 12.0, -34.0), 0.0, 0.0);
    check_bool("A6 P0: par demo real es circuito corto (<500 km)", hfc::greatCircleKm(50.097, 4.588, 51.685, -1.795) < 500.0);
    printf("--- B) arco de circulo maximo (slerp) ---\n");
    { double lat, lon;
      hfc::arcPoint(0.0, 0.0, 0.0, 90.0, 0.5, lat, lon);
    check_near("B1 (0,0)->(0,90) f=0.5 lat", lat, 0.0, 1.0e-9);
    check_near("B2 (0,0)->(0,90) f=0.5 lon", lon, 45.0, 1.0e-9);
      hfc::arcPoint(0.0, 0.0, 0.0, 90.0, 0.25, lat, lon);
    check_near("B3 f=0.25 lon", lon, 22.500000000000004, 1.0e-9);
      hfc::arcPoint(0.0, 0.0, 0.0, 90.0, 0.0, lat, lon);
    check_near("B4 f=0 -> TX exacto", lat + lon, 0.0, 1.0e-12);
      hfc::arcPoint(0.0, 0.0, 0.0, 90.0, 1.0, lat, lon);
    check_near("B5 f=1 -> RX exacto", lon, 90.0, 1.0e-12);
      hfc::arcPoint(0.0, 0.0, 60.0, 0.0, 0.5, lat, lon);
    check_near("B6 meridiano f=0.5 lat", lat, 29.999999999999996, 1.0e-9);
      hfc::arcPoint(0.0, 179.0, 0.0, -179.0, 0.25, lat, lon);
    check_near("B7 antimeridiano f=0.25 lon", lon, 179.5, 1.0e-9);
      hfc::arcPoint(10.0, 20.0, 40.0, 60.0, 0.5, lat, lon);
    check_near("B8 diagonal media lat", lat, 26.368621984966001, 1.0e-9);
    check_near("B9 diagonal media lon", lon, 37.396159523445476, 1.0e-9);
      hfc::arcPoint(50.097, 4.588, 51.685, -1.795, 0.5, lat, lon);
    check_near("B10 midpoint DB049->EG931 lat (h' del circuito)", lat, 50.934509964326011, 1.0e-9);
    check_near("B11 midpoint DB049->EG931 lon", lon, 1.4509643122960167, 1.0e-9);
    }
    printf("--- C) altura virtual del circuito ---\n");
    check_near("C1 hmF2=100 -> suelo 140", hfc::virtualHeightKm(100.0), 140.0, 0.0);
    check_near("C2 hmF2=480 -> techo 420", hfc::virtualHeightKm(480.0), 420.0, 0.0);
    check_near("C3 hmF2=310 -> 310 (sin clamp)", hfc::virtualHeightKm(310.0), 310.0, 0.0);
    check_near("C4 borde 140 exacto", hfc::virtualHeightKm(140.0), 140.0, 0.0);
    check_near("C5 borde 420 exacto", hfc::virtualHeightKm(420.0), 420.0, 0.0);
    printf("--- D) MUF por salto (foF2·M(D)) y FOT ---\n");
    check_near("D1 MUF(5.5, 310, 2511) — ancla larga", hfc::mufForHopMHz(5.5, 310.0, 2511.0), 14.92853957181668, 1.0e-9);
    check_near("D2 MUF(5.5, 310, 481.1) — par demo real", hfc::mufForHopMHz(5.5, 310.0, 481.1), 10.041315161774405, 1.0e-9);
    check_near("D3 D<200 usa suelo 200 (M(D))", hfc::mufForHopMHz(5.5, 310.0, 100.0), 8.1339465003417466, 1.0e-9);
    check_near("D4 MUF(4.8, 310, 2511) — banda 13-17", hfc::mufForHopMHz(4.8, 310.0, 2511.0), 13.028543626312738, 1.0e-9);
    check_near("E1 FOT = 0.85·MUF", hfc::fotOfMHz(14.928), 12.688800000000001, 1.0e-12);
    printf("--- F) bandState: open/marginal/closed (bordes) ---\n");
    // luf=3, fot=11.9, muf=14: open [3, 11.9], marginal [2.55, 14], closed resto
    check_int("F1  f=3.0 (borde LUF) -> OPEN", hfc::bandState(3.0, 3.0, 11.9, 14.0), 2);
    check_int("F2  f=11.9 (borde FOT) -> OPEN", hfc::bandState(11.9, 3.0, 11.9, 14.0), 2);
    check_int("F3  f=7.0 (interior) -> OPEN", hfc::bandState(7.0, 3.0, 11.9, 14.0), 2);
    check_int("F4  f=11.95 -> MARGINAL", hfc::bandState(11.95, 3.0, 11.9, 14.0), 1);
    check_int("F5  f=14.0 (borde MUF) -> MARGINAL", hfc::bandState(14.0, 3.0, 11.9, 14.0), 1);
    check_int("F6  f=14.05 -> CLOSED", hfc::bandState(14.05, 3.0, 11.9, 14.0), 0);
    check_int("F7  f=2.55 (borde 0.85·LUF) -> MARGINAL", hfc::bandState(2.55, 3.0, 11.9, 14.0), 1);
    check_int("F8  f=2.54 -> CLOSED", hfc::bandState(2.54, 3.0, 11.9, 14.0), 0);
    check_int("F9  f=2.9 (entre 0.85LUF y LUF) -> MARGINAL", hfc::bandState(2.9, 3.0, 11.9, 14.0), 1);
    check_int("F10 f=1.0 (muy bajo) -> CLOSED", hfc::bandState(1.0, 3.0, 11.9, 14.0), 0);
    check_int("F11 degenerado luf=fot=muf=8, f=8 -> OPEN (orden)", hfc::bandState(8.0, 8.0, 8.0, 8.0), 2);
    check_int("F12 f=luf+eps -> OPEN", hfc::bandState(3.0001, 3.0, 11.9, 14.0), 2);
    printf("--- G) hopViable (solo MUF del salto) ---\n");
    check_near("G1 f=0.85·muf exacto -> 1.0", hfc::hopViable(8.5, 10.0), 1.0, 0.0);
    check_near("G2 f<muf·0.85 -> 1.0", hfc::hopViable(8.4, 10.0), 1.0, 0.0);
    check_near("G3 f=muf exacto -> 0.5", hfc::hopViable(10.0, 10.0), 0.5, 0.0);
    check_near("G4 f>muf -> 0.0", hfc::hopViable(10.1, 10.0), 0.0, 0.0);
    check_near("G5 f=5.0 (muy bajo) -> 1.0", hfc::hopViable(5.0, 10.0), 1.0, 0.0);
    printf("--- H) hopLayout (n, salto, elevacion) ---\n");
    check_int("H1 par demo: n=1", hfc::hopLayout(481.1, 310.0).nHops, 1);
    check_near("H2 par demo: hop = D", hfc::hopLayout(481.1, 310.0).hopKm, 481.10000000000002, 1.0e-9);
    check_near("H3 par demo: elev ~50.44", hfc::hopLayout(481.1, 310.0).elevDeg, 50.435723592290572, 1.0e-6);
    check_int("H4 P0: D=2511 -> 1 salto (no 3 — plan 11.2 corregido)", hfc::hopLayout(2511.0, 310.0).nHops, 1);
    check_near("H5 D=2511: elev ~7.865 (ancla)", hfc::hopLayout(2511.0, 310.0).elevDeg, 7.8647874865233671, 1.0e-6);
    check_int("H6 D=7533 -> 3 saltos (visual multi-salto)", hfc::hopLayout(7533.0, 310.0).nHops, 3);
    check_near("H7 D=7533: hop = 2511", hfc::hopLayout(7533.0, 310.0).hopKm, 2511.0, 1.0e-9);
    check_near("H8 D=7533: elev ~7.865", hfc::hopLayout(7533.0, 310.0).elevDeg, 7.8647874865233671, 1.0e-6);
    check_int("H9 D=5000 -> 2", hfc::hopLayout(5000.0, 310.0).nHops, 2);
    check_int("H10 D=12000 -> 4", hfc::hopLayout(12000.0, 310.0).nHops, 4);
    check_int("H11 D=19000 -> 6 (techo)", hfc::hopLayout(19000.0, 310.0).nHops, 6);
    printf("--- I) reflexiones (k+0.5)/n ---\n");
    check_near("I1 n=3 k=0 -> 1/6", hfc::reflectionFraction(0, 3), 0.16666666666666666, 0.0);
    check_near("I2 n=3 k=1 -> 1/2", hfc::reflectionFraction(1, 3), 0.5, 0.0);
    check_near("I3 n=3 k=2 -> 5/6", hfc::reflectionFraction(2, 3), 0.83333333333333337, 0.0);
    check_near("I4 n=1 k=0 -> 1/2", hfc::reflectionFraction(0, 1), 0.5, 0.0);
    printf("--- J) sampleGrid (fila 0 = lat -90, nodos en esquinas) ---\n");
    const int JW = 72, JH = 72;
    std::vector<float> jrow((size_t)JW * JH);
    std::vector<float> jlat((size_t)JW * JH);
    std::vector<float> jlon((size_t)JW * JH);
    std::vector<float> jmix((size_t)JW * JH);
    std::vector<float> jconst((size_t)JW * JH, 7.0f);
    for (int r = 0; r < JH; r++) {
        for (int c = 0; c < JW; c++) {
            const double nlat = -90.0 + (180.0 / JH) * r;
            const double nlon = -180.0 + (360.0 / JW) * c;
            jrow[(size_t)r * JW + c] = (float)r;
            jlat[(size_t)r * JW + c] = (float)nlat;
            jlon[(size_t)r * JW + c] = (float)nlon;
            jmix[(size_t)r * JW + c] = (float)(0.1 * nlat + 0.01 * nlon);
        }
    }
    check_near("J1 fila 0 = lat -90: nodo (0,0) -> valor fila 0", hfc::sampleGrid(jrow.data(), JW, JH, -90.0, -180.0), 0.0, 0.0);
    check_near("J2 nodo fila 1 (lat -87.5) -> valor 1 (convencion SUR primero)", hfc::sampleGrid(jrow.data(), JW, JH, -87.5, 0.0), 1.0, 0.0);
    check_near("J3 rampa lat: nodo exacto (r=18, lat -45)", hfc::sampleGrid(jrow.data(), JW, JH, -45.0, 0.0), 18.0, 0.0);
    check_near("J4 rampa lat: midpoint entre nodos (-43.75)", hfc::sampleGrid(jlat.data(), JW, JH, -43.75, 0.0), -43.75, 1.0e-9);
    check_near("J5 grid constante -> 7.0 en cualquier punto", hfc::sampleGrid(jconst.data(), JW, JH, 33.3, 71.7), 7.0, 1.0e-12);
    check_near("J6 rampa lon: nodo exacto (c=71, lon 175)", hfc::sampleGrid(jlon.data(), JW, JH, 0.0, 175.0), 175.0, 1.0e-9);
    check_near("J7 lon 177.5 mas alla del ultimo nodo (175) -> clamp", hfc::sampleGrid(jlon.data(), JW, JH, 0.0, 177.5), 175.0, 1.0e-9);
    check_near("J8 2D bilineal en punto interior", hfc::sampleGrid(jmix.data(), JW, JH, 12.34, -56.78), 0.66619998451423645, 1.0e-9);
    check_near("J9 clamp de bordes: (95, 200) -> nodo (71,71)", hfc::sampleGrid(jlat.data(), JW, JH, 95.0, 200.0), 87.5, 1.0e-9);
    check_near("J10 guard null -> 0.0", hfc::sampleGrid(nullptr, 72, 72, 0.0, 0.0), 0.0, 0.0);
    printf("--- K) composicion aceptacion 1 (P0 re-anclada) ---\n");
    // Grids constantes: foF2=5.5, hmF2=310, LUF=3.0 sobre DB049->EG931 real.
    std::vector<float> kfo((size_t)JW * JH, 5.5f);
    std::vector<float> khm((size_t)JW * JH, 310.0f);
    std::vector<float> kluf((size_t)JW * JH, 3.0f);
    const double kD = hfc::greatCircleKm(50.097, 4.588, 51.685, -1.795);
    double kMidLat, kMidLon;
    hfc::arcPoint(50.097, 4.588, 51.685, -1.795, 0.5, kMidLat, kMidLon);
    const double kHV = hfc::virtualHeightKm(hfc::sampleGrid(khm.data(), JW, JH, kMidLat, kMidLon));
    const hfc::HopLayout kLay2 = hfc::hopLayout(kD, kHV);
    check_near("K1 D = 481.1 km (geodesica real)", kD, 481.07259457064129, 1.0e-6);
    check_int("K2 n = 1 salto", kLay2.nHops, 1);
    check_near("K3 h' = 310 (hmF2 grid constante)", kHV, 310.0, 0.0);
    check_near("K4 elev ~50.4 deg (sello P0/MUSE verifico 50.44)", kLay2.elevDeg, 50.43737547363753, 1.0e-6);
    double kRefLat, kRefLon;
    hfc::arcPoint(50.097, 4.588, 51.685, -1.795, hfc::reflectionFraction(0, kLay2.nHops), kRefLat, kRefLon);
    const double kFo = hfc::sampleGrid(kfo.data(), JW, JH, kRefLat, kRefLon);
    const double kHm = hfc::sampleGrid(khm.data(), JW, JH, kRefLat, kRefLon);
    const double kMuf = hfc::mufForHopMHz(kFo, kHm, kLay2.hopKm);
    const double kFot = hfc::fotOfMHz(kMuf);
    const double kLuf = hfc::sampleGrid(kluf.data(), JW, JH, kRefLat, kRefLon);
    check_near("K5 MUF = 5.5·M(481.1) ~ 10.04 MHz", kMuf, 10.041177880123028, 1.0e-6);
    check_near("K6 FOT = 0.85·MUF ~ 8.53", kFot, 8.5350011981045739, 1.0e-6);
    check_near("K7 LUF = 3.0 (grid constante, max trivial)", kLuf, 3.0, 0.0);
    check_int("K8 banda 7.0 MHz -> OPEN", hfc::bandState(7.0, kLuf, kFot, kMuf), 2);
    check_int("K9 banda 9.0 MHz -> MARGINAL", hfc::bandState(9.0, kLuf, kFot, kMuf), 1);
    check_int("K10 banda 14.0 MHz (default panel) -> CLOSED", hfc::bandState(14.0, kLuf, kFot, kMuf), 0);
    check_near("K11 aViable del salto a 14 MHz -> 0 (arco rojo)", hfc::hopViable(14.0, kMuf), 0.0, 0.0);
    printf("--- L) LUF del circuito = max sobre saltos ---\n");
    const double lD = hfc::greatCircleKm(20.0, 0.0, -20.0, 67.75);
    const hfc::HopLayout lLay = hfc::hopLayout(lD, 310.0);
    double lMax = -1.0e30;
    for (int k = 0; k < lLay.nHops; k++) {
        double rla, rlo;
        hfc::arcPoint(20.0, 0.0, -20.0, 67.75, hfc::reflectionFraction(k, lLay.nHops), rla, rlo);
        const double lL = hfc::sampleGrid(jlat.data(), JW, JH, rla, rlo);
        if (lL > lMax) lMax = lL;
    }
    check_int("L1 circuito de 3 saltos (D~7533)", lLay.nHops, 3);
    check_near("L2 LUF circuito = max de las reflexiones", lMax, 13.773972399689676, 1.0e-9);
    check_bool("L3 el max manda: > min de las reflexiones", lMax > 0.0);
    printf("--- M) M9: exageracion visual del arco (acople explodeFactor) ---\n");
    // GENERADO por scripts/m9_gen_test.py (implementacion python
    // independiente). hfc::legAltitudeKm y ribbonHalfWidth son
    // PRODUCCION (las usa buildPolyline/render de HFTraceLayer).
    check_near("M1  leg(310,1,1) = 310 (back-compat M8: exag=1 fisica)",
               hfc::legAltitudeKm(310.0, 1.0, 1.0), 310.0, 0.0);
    check_near("M2  leg(310,1,2.2) = 682 (apice demo-paridad, h'=310)",
               hfc::legAltitudeKm(310.0, 1.0, 2.2), 682.0, 1.0e-9);
    check_near("M3  bajada leg(310,0.65,2.2) = 310*0.65*2.2",
               hfc::legAltitudeKm(310.0, 0.65, 2.2), 443.3, 1.0e-9);
    check_near("M4  leg(310,0.5,1) = 155 (formula M8 literal h'*t)",
               hfc::legAltitudeKm(310.0, 0.5, 1.0), 155.0, 0.0);
    // ribbonHalfWidth (re-sellado M9b, comparativa lado a lado con la demo):
    // 0.0075 CONSTANTE a cualquier Explode — la demo usa tubo de radio
    // 0.0075 fijo y el ancho NO escala con la exageracion. tol 1e-6.
    check_near("M5  hw(1.0) = 0.0075 (demo verbatim: constante)",
               (double)hfc::ribbonHalfWidth(1.0f), 0.0075, 1.0e-6);
    check_near("M6  hw(2.2) = 0.0075 (punto de calibracion demo)",
               (double)hfc::ribbonHalfWidth(2.2f), 0.0075, 1.0e-6);
    check_near("M7  hw(5.0) = 0.0075 (sin tope: constante)",
               (double)hfc::ribbonHalfWidth(5.0f), 0.0075, 1.0e-6);
    {
        bool mono = true;
        float prevw = hfc::ribbonHalfWidth(1.0f);
        for (int e10 = 10; e10 <= 50; e10 += 5) {
            const float w = hfc::ribbonHalfWidth((float)e10 / 10.0f);
            if (w < prevw) mono = false;
            prevw = w;
        }
        check_bool("M8  hw monotono no-decreciente en [1,5] (constante: trivial)", mono);
    }
    check_near("M9  hw clamp bajo: hw(0.3) = 0.0075 (constante)",
               (double)hfc::ribbonHalfWidth(0.3f), 0.0075, 1.0e-6);
    check_near("M10 hw clamp alto: hw(9.0) = 0.0075 (constante)",
               (double)hfc::ribbonHalfWidth(9.0f), 0.0075, 1.0e-6);
    printf("--- N) M9fix: apice sigue al slider de capa (offset + floor) ---\n");
    // GENERADO por scripts/m9fix_gen_test.py (implementacion python
    // independiente). hfc::legAltitudeKm 4-arg es PRODUCCION (las 2
    // patas de buildPolyline la usan); el offset = altKm - 300 de App.
    check_near("N1  leg(288,1,2.2,+100) = (288+100)*2.2 (apice @Alt 400)",
               hfc::legAltitudeKm(288.0, 1.0, 2.2, 100.0), 853.6, 1.0e-9);
    check_near("N2  leg(310,1,2.2,0) = 682 (offset 0 explicito = M9)",
               hfc::legAltitudeKm(310.0, 1.0, 2.2, 0.0), 682.0, 1.0e-9);
    check_near("N3  leg(310,0.65,2.2,0) = 443.3 (4-arg = M3)",
               hfc::legAltitudeKm(310.0, 0.65, 2.2, 0.0), 443.3, 1.0e-9);
    check_near("N4  floor: leg(180,0.5,1,-200) = 0 (base -20 clamped)",
               hfc::legAltitudeKm(180.0, 0.5, 1.0, -200.0), 0.0, 0.0);
    check_near("N5  floor apice: leg(180,1,2.2,-200) = 0",
               hfc::legAltitudeKm(180.0, 1.0, 2.2, -200.0), 0.0, 0.0);
    check_near("N6  bajada leg(288,0.35,2.2,+100) = 388*0.35*2.2",
               hfc::legAltitudeKm(288.0, 0.35, 2.2, 100.0), 298.76, 1.0e-9);
    check_near("N7  offset lineal: leg(288,0.5,1,+100) - leg(288,0.5,1,0) = 50",
               hfc::legAltitudeKm(288.0, 0.5, 1.0, 100.0)
               - hfc::legAltitudeKm(288.0, 0.5, 1.0, 0.0), 50.0, 1.0e-9);
    {
        bool monoN = true;
        double prevN = hfc::legAltitudeKm(288.0, 0.0, 2.2, 100.0);
        for (int t8 = 1; t8 <= 8; t8++) {
            const double v = hfc::legAltitudeKm(288.0, (double)t8 / 8.0, 2.2, 100.0);
            if (v < prevN) monoN = false;
            prevN = v;
        }
        check_bool("N8  leg monotona no-decreciente en t (h'=288, off +100, exag 2.2)", monoN);
    }
    check_bool("N9  default-param: leg(310,1,2.2) == leg(310,1,2.2,0.0)",
               hfc::legAltitudeKm(310.0, 1.0, 2.2) == hfc::legAltitudeKm(310.0, 1.0, 2.2, 0.0));
    printf("--- O) M9c: perfil tubo+halo + cabeza del pulso ---\n");
    // GENERADO por scripts/m9c_gen_test.py (implementacion python
    // independiente). hfc::ribbonSpan es PRODUCCION (render() la multiplica
    // en u_halfWidth); traceProfile/pulseHead* son twins C++ EXACTOS de los
    // literales GLSL del frag (paridad por proofs RT: el GLSL no llama hfc::).
    check_near("O1  f(0.0) = 1.35 (nucleo pleno: core 1 + pedestal halo 0.35)",
               (double)hfc::traceProfile(0.0f), 1.35, 1.0e-6);
    check_near("O2  f(0.45) = 1.35 (borde del nucleo)",
               (double)hfc::traceProfile(0.45f), 1.35, 1.0e-6);
    check_near("O3  f(0.5) = 0.85 (hombro: borde M9b +/-0.0075)",
               (double)hfc::traceProfile(0.5f), 0.8500000000000004, 1.0e-6);
    check_near("O4  f(0.55) = 0.35 (pedestal del aura)",
               (double)hfc::traceProfile(0.55f), 0.35, 1.0e-6);
    check_near("O5  f(0.7) = 0.2593 (cola del aura: 0.35*(1-ss))",
               (double)hfc::traceProfile(0.7f), 0.2592592592592593, 1.0e-6);
    check_near("O6  f(1.0) = 0.0 (borde geometrico invisible)",
               (double)hfc::traceProfile(1.0f), 0.0, 1.0e-9);
    {
        bool monoO = true;
        float prevO = hfc::traceProfile(0.0f);
        for (int i8 = 1; i8 <= 100; i8++) {
            const float v = hfc::traceProfile((float)i8 * 0.01f);
            if (v > prevO + 1.0e-6f) monoO = false;
            prevO = v;
        }
        check_bool("O7  f monotona no-creciente en [0,1] paso 0.01", monoO);
    }
    check_near("O8  span = 2.0 (borde = 2x nucleo)", (double)hfc::ribbonSpan(), 2.0, 0.0);
    check_near("O9  borde geometrico mundo = hw*span = 0.015",
               (double)hfc::ribbonHalfWidth(2.2f) * hfc::ribbonSpan(), 0.015, 1.0e-6);
    check_near("O10 nucleo M9b intacto: hw*span*0.5 == hw (s=0.5)",
               (double)hfc::ribbonHalfWidth(2.2f) * hfc::ribbonSpan() * 0.5,
               (double)hfc::ribbonHalfWidth(2.2f), 1.0e-6);
    check_near("O11 boost(0.0) = 1.0 (cima del pulso)",
               (double)hfc::pulseHeadBoost(0.0f), 1.0, 1.0e-9);
    check_near("O12 boost(0.16) = 0.0 (fin de la cabeza, M9g +30%)",
               (double)hfc::pulseHeadBoost(0.16f), 0.0, 1.0e-9);
    check_near("O13 boost(0.14) = 0.5 (flanco AA, M9h flat-top)",
               (double)hfc::pulseHeadBoost(0.14f), 0.5, 1.0e-6);
    check_near("O14 gate(0.40) = 1.0 (pedestal pleno, M9g)",
               (double)hfc::pulseHeadGate(0.40f), 1.0, 1.0e-9);
    check_near("O15 gate(0.70) = 0.0 (fuera del pedestal, M9g)",
               (double)hfc::pulseHeadGate(0.70f), 0.0, 1.0e-9);
    check_near("O16 gate(0.55) = 0.5 (media puerta, M9g)",
               (double)hfc::pulseHeadGate(0.55f), 0.5000000000000002, 1.0e-6);
    check_near("O17 amplitud pico = 0.55*1*1",
               (double)hfc::pulseHeadAmplitude(1.0f, 1.0f), 0.55, 1.0e-6);
    {
        // cabeza compuesta M9e, banda verde DEMO (0.10,0.95,0.55), pulse
        // 0.9, f(0), add TENIDO por col (el verde sale del propio col):
        const float ampPk = hfc::pulseHeadAmplitude(hfc::pulseHeadBoost(0.0f), hfc::pulseHeadGate(0.0f));
        const float gTot = 0.95f * (0.55f + 0.9f) * hfc::traceProfile(0.0f) + 0.95f * ampPk;
        const float bTot = 0.55f * (0.55f + 0.9f) * hfc::traceProfile(0.0f) + 0.55f * ampPk;
        const float rHalf = 0.10f * (0.55f + 0.9f) * hfc::traceProfile(0.0f)
                          + 0.10f * hfc::pulseHeadAmplitude(hfc::pulseHeadBoost(0.14f), 1.0f);
        check_bool("O18 cabeza pico: G y B saturan (>=1, verde intenso demo)",
                   gTot >= 1.0 && bTot >= 1.0);
        check_bool("O19 cabeza no-blanca global: R diminuto a media cabeza (boost 0.5, <1)",
                   rHalf < 1.0);
    }
    check_bool("O20 fuera de la cabeza add = 0 (amplitude(boost(0.2),1)==0): arco puro multiplicativo",
               hfc::pulseHeadAmplitude(hfc::pulseHeadBoost(0.2f), 1.0f) == 0.0f);
    printf("--- P) M9d: respiracion del halo + derrame neon de la cabeza ---\n");
    // GENERADO por scripts/m9d_gen_test.py (implementacion python
    // independiente). traceProfileBreath/glowSpill/haloPulse* son twins
    // C++ EXACTOS de los literales GLSL del frag M9d (paridad por proofs
    // RT: el GLSL no llama hfc::). Blend (SRC_ALPHA, ONE): dst = rgb*alpha.
    check_near("P1  breath(0.0, 0.0) = 1.21 (reposo: nucleo + aura 0.21)",
               (double)hfc::traceProfileBreath(0.0f, 0.0f), 1.21, 1.0e-6);
    check_near("P2  breath(0.0, 1.0) = 1.49 (cima: nucleo + aura 0.49)",
               (double)hfc::traceProfileBreath(0.0f, 1.0f), 1.49, 1.0e-6);
    check_near("P3  breath(0.55, 0.0) = 0.21 (pedestal del aura en reposo)",
               (double)hfc::traceProfileBreath(0.55f, 0.0f), 0.21, 1.0e-6);
    check_near("P4  breath(0.55, 1.0) = 0.49 (pedestal del aura a la cima)",
               (double)hfc::traceProfileBreath(0.55f, 1.0f), 0.48999999999999994, 1.0e-6);
    check_near("P5  breath(0.7, 1.0) = 0.3630 (falda del aura a la cima)",
               (double)hfc::traceProfileBreath(0.7f, 1.0f), 0.362962962962963, 1.0e-6);
    check_near("P6  breath(0.7, 0.0) = 0.1556 (falda del aura en reposo)",
               (double)hfc::traceProfileBreath(0.7f, 0.0f), 0.15555555555555559, 1.0e-6);
    {
        bool invP = true;
        for (int i9 = 0; i9 <= 100; i9++) {
            const float sP = (float)i9 * 0.01f;
            if (hfc::traceProfileBreath(sP, 0.5f) != hfc::traceProfile(sP)) invP = false;
        }
        check_bool("P7  invariante: breath(s, 0.5) == traceProfile(s) exacto (halo x1.0 = anclas M9c)", invP);
    }
    {
        bool monoPs = true;
        float prevP0 = hfc::traceProfileBreath(0.0f, 0.0f);
        float prevP1 = hfc::traceProfileBreath(0.0f, 1.0f);
        for (int i10 = 1; i10 <= 100; i10++) {
            const float sP = (float)i10 * 0.01f;
            if (hfc::traceProfileBreath(sP, 0.0f) > prevP0 + 1.0e-6f) monoPs = false;
            if (hfc::traceProfileBreath(sP, 1.0f) > prevP1 + 1.0e-6f) monoPs = false;
            prevP0 = hfc::traceProfileBreath(sP, 0.0f);
            prevP1 = hfc::traceProfileBreath(sP, 1.0f);
        }
        check_bool("P8  breath monotona no-creciente en s (env 0 y 1)", monoPs);
    }
    {
        bool monoPe = true;
        float prevPe = hfc::traceProfileBreath(0.7f, 0.0f);
        for (int i11 = 1; i11 <= 100; i11++) {
            const float vP = hfc::traceProfileBreath(0.7f, (float)i11 * 0.01f);
            if (vP < prevPe - 1.0e-6f) monoPe = false;
            prevPe = vP;
        }
        check_bool("P9  breath no-decreciente en env a s=0.7 (el aura solo se enciende, nunca se apaga al paso)", monoPe);
    }
    check_near("P10 spill(0.0, env 1) = 0.45 (M9e: plan-B sellado 0.30->0.45)",
               (double)hfc::glowSpill(0.0f, 1.0f), 0.35, 1.0e-6);
    check_near("P11 spill(1.0, 1.0) = 0.0 (nada en el borde geometrico)",
               (double)hfc::glowSpill(1.0f, 1.0f), 0.0, 1.0e-9);
    check_near("P12 spill(0.7, env 1) = 0.135 (falda del derrame a la cima)",
               (double)hfc::glowSpill(0.7f, 1.0f), 0.105, 1.0e-6);
    {
        bool noSpill = true;
        for (int i12 = 0; i12 <= 100; i12++) {
            if (hfc::glowSpill((float)i12 * 0.01f, 0.0f) != 0.0f) noSpill = false;
        }
        check_bool("P13 en reposo spill = 0 (env 0): arco puro multiplicativo (M9e: el 2o arg es env)", noSpill);
    }
    check_near("P14 alpha compuesta falda-cima (s=0.85): 0.9*breath + spill = 0.1818",
               (double)(0.9f * hfc::traceProfileBreath(0.85f, 1.0f) + hfc::glowSpill(0.85f, 1.0f)),
               0.16683333333333332, 1.0e-6);
    {
        // contraste dst (SRC_ALPHA, ONE): rgb*alpha con rgb = mezcla*breath + spill
        const float bhP = hfc::traceProfileBreath(0.7f, 1.0f);
        const float spP = hfc::glowSpill(0.7f, 1.0f);
        const float rgbHd = (0.55f + 0.9f) * bhP + spP;
        const float aHd = 0.9f * bhP + spP;
        const float brP = hfc::traceProfileBreath(0.7f, 0.0f);
        const float rgbRd = 0.55f * brP;
        const float aRd = 0.9f * brP;
        check_bool("P15 contraste neon dst cabeza/reposo (s=0.7) >= 20x (2.6x en M9c)",
                   (rgbHd * aHd) >= 20.0f * (rgbRd * aRd));
    }
    {
        // falda tenida (M9fix extendido al derrame): G de banda verde < 1 a s=0.7 en la cima
        const float rgbSkirt = (0.55f + 0.9f) * hfc::traceProfileBreath(0.7f, 1.0f)
                             + hfc::glowSpill(0.7f, 1.0f);
        check_bool("P16 falda sin clip a s=0.7 en la cima: G < 1.0 (neon TENIDO, no blanco)",
                   rgbSkirt < 1.0f);
    }
    {
        // nucleo a la cima sigue quemando (O18 con breath + spill + cabeza
        // TENIDA M9e: add = col*amp — el G del pico sale del propio col)
        const float ampPk = hfc::pulseHeadAmplitude(hfc::pulseHeadBoost(0.0f), hfc::pulseHeadGate(0.0f));
        const float gTot = 0.95f * ((0.55f + 0.9f) * hfc::traceProfileBreath(0.0f, 1.0f)
                          + hfc::glowSpill(0.0f, 1.0f)) + 0.95f * ampPk;
        check_bool("P17 nucleo a la cima: G >= 1 (verde intenso de la demo, R diminuto)",
                   gTot >= 1.0f);
    }
    printf("--- Q) M9e: cursor verde demo-verbatim + glow ancho de la envolvente ---\n");
    // GENERADO por scripts/m9e_gen_test.py (implementacion python
    // independiente; evidencia: fuente shaders.ts + demo en vivo).
    // traceColor*/pulseCycleHz/pulseRepeatsPerArc son twins C++ EXACTOS
    // de los literales GLSL/TS (paridad por proofs RT: el GLSL no llama
    // hfc::). Blend (SRC_ALPHA, ONE): dst = rgb*alpha.
    check_near("Q1  color abierto R = 0.10 (demo shaders.ts:460)",
               (double)hfc::traceColorOpen().r, 0.10, 1.0e-7);
    check_near("Q2  color abierto G = 0.95 (demo)",
               (double)hfc::traceColorOpen().g, 0.95, 1.0e-7);
    check_near("Q3  color abierto B = 0.55 (demo)",
               (double)hfc::traceColorOpen().b, 0.55, 1.0e-7);
    check_near("Q4  color marginal R = 0.98 (demo shaders.ts:461)",
               (double)hfc::traceColorMarginal().r, 0.98, 1.0e-7);
    check_near("Q5  color marginal G = 0.72 (demo)",
               (double)hfc::traceColorMarginal().g, 0.72, 1.0e-7);
    check_near("Q6  color marginal B = 0.10 (demo)",
               (double)hfc::traceColorMarginal().b, 0.10, 1.0e-7);
    check_near("Q7  color cerrado R = 0.95 (demo shaders.ts:462)",
               (double)hfc::traceColorClosed().r, 0.95, 1.0e-7);
    check_near("Q8  color cerrado G = 0.16 (demo)",
               (double)hfc::traceColorClosed().g, 0.16, 1.0e-7);
    check_near("Q9  color cerrado B = 0.16 (demo)",
               (double)hfc::traceColorClosed().b, 0.16, 1.0e-7);
    {
        // pico del cursor a la cima (s=0, env=1, boost=1, gate=1):
        // total = (0.55+0.9)*breath(0,1) + spill(0,1) + amp — TODO col.
        const float totQ = (0.55f + 0.9f) * hfc::traceProfileBreath(0.0f, 1.0f)
                         + hfc::glowSpill(0.0f, 1.0f)
                         + hfc::pulseHeadAmplitude(1.0f, 1.0f);
        const float rPk = hfc::traceColorOpen().r * totQ;
        const float gPk = hfc::traceColorOpen().g * totQ;
        const float bPk = hfc::traceColorOpen().b * totQ;
        check_bool("Q10 pico NEVER-WHITE: R < 0.5 con R=0.10 a cualquier ganancia (veredicto DAVID)",
                   rPk < 0.5f);
        check_bool("Q11 pico VERDE intenso: G >= 1 (satura como la demo)",
                   gPk >= 1.0f);
        check_bool("Q12 pico verde-cian: B >= 1 (G/B saturan, R diminuto — la firma del verde demo)",
                   bPk >= 1.0f);
        check_near("Q13 pico R exacto = 0.10*3.0605 = 0.30605",
                   (double)rPk, 0.30605000000000005, 1.0e-6);
    }
    check_near("Q14 spill(0.5, env 0.24) = 0.042 (glow vivo a |p-0.5|=0.2: tramo ANCHO)",
               (double)hfc::glowSpill(0.5f, 0.24f), 0.042, 1.0e-6);
    {
        // tramo visible en la falda (s=0.7) a |p-0.5|=0.25 (env 0.105):
        // M9e supera 0.02 de dst (el glow llega lejos); M9d (boost=0 ahi,
        // sin spill) se quedaba por debajo — la longitude percibida.
        const float envQ = expf(-36.0f * 0.25f * 0.25f);
        const float rgbQ = (0.55f + 0.9f * envQ) * hfc::traceProfileBreath(0.7f, envQ)
                         + hfc::glowSpill(0.7f, envQ);
        const float aQ = 0.9f * hfc::traceProfileBreath(0.7f, envQ) + hfc::glowSpill(0.7f, envQ);
        check_bool("Q15 tramo ancho: dst falda a |p-0.5|=0.25 > 0.02 (M9d sin spill se quedaba en 0.018)",
                   rgbQ * aQ > 0.02f);
    }
    check_near("Q16 velocidad: ciclo de emision = 1/0.22 = 4.5455 s (demo shaders.ts:450)",
               1.0 / (double)hfc::pulseCycleHz(), 4.545454545454546, 1.0e-6);
    check_near("Q17 velocidad: recorrido del paquete = 1/(0.22/2.5) = 11.3636 s",
               1.0 / (hfc::pulseCycleHz() / hfc::pulseRepeatsPerArc()), 11.363636363636363, 1.0e-6);
    check_near("Q18 paquetes en vuelo = 2.5 (repeats demo-verbatim)",
               (double)hfc::pulseRepeatsPerArc(), 2.5, 0.0);
    {
        // reposo puro (invariante M9d conservada): env=0 => spill=0 y
        // boost(0.5)=0 => add=0 — arco multiplicativo col*(0.55)*breath.
        bool noSpillQ = true;
        for (int i13 = 0; i13 <= 100; i13++) {
            if (hfc::glowSpill((float)i13 * 0.01f, 0.0f) != 0.0f) noSpillQ = false;
        }
        check_bool("Q19 reposo puro: spill(s, env 0) = 0 y add = 0 (boost(0.5) = 0)",
                   noSpillQ && hfc::pulseHeadBoost(0.5f) == 0.0f);
    }
    {
        // cabeza TENIDA: add = col*amp => el ratio G/R del add es el del col
        // (9.5x): la cabeza no puede ser mas blanca que el tramo.
        const float ampQ = hfc::pulseHeadAmplitude(hfc::pulseHeadBoost(0.0f), hfc::pulseHeadGate(0.0f));
        const float addR = hfc::traceColorOpen().r * ampQ;
        const float addG = hfc::traceColorOpen().g * ampQ;
        check_bool("Q20 cabeza tenida: add.G/add.R = col.G/col.R (el verde sale del propio col)",
                   addR < addG * 0.11f && ampQ > 0.0f);
    }
    check_near("Q21 bypass V del bloom = -1.0 (M9f: smoothstep(-1,-0.75,l>=0)=1 passthrough)",
               (double)hfc::bloomBypassThreshold(), -1.0, 0.0);
    printf("\n[test_hf_circuit] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
