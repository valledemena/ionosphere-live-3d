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
    // ribbonHalfWidth: 0.004@1 (look M8 exacto) -> 0.0075@2.2 (demo: tubo
    // radio 0.0075) -> tope 0.012@5. Float C++ vs double python, tol 1e-6.
    check_near("M5  hw(1.0) = 0.004 (look M8 en escala fisica)",
               (double)hfc::ribbonHalfWidth(1.0f), 0.004, 1.0e-6);
    check_near("M6  hw(2.2) = 0.0075 (paridad demo)",
               (double)hfc::ribbonHalfWidth(2.2f), 0.007500000000000001, 1.0e-6);
    check_near("M7  hw(5.0) = 0.012 (tope del cap)",
               (double)hfc::ribbonHalfWidth(5.0f), 0.012, 1.0e-6);
    {
        bool mono = true;
        float prevw = hfc::ribbonHalfWidth(1.0f);
        for (int e10 = 10; e10 <= 50; e10 += 5) {
            const float w = hfc::ribbonHalfWidth((float)e10 / 10.0f);
            if (w < prevw) mono = false;
            prevw = w;
        }
        check_bool("M8  hw monotono no-decreciente en [1,5] (paso 0.5)", mono);
    }
    check_near("M9  hw clamp bajo: hw(0.3) = hw(1) = 0.004",
               (double)hfc::ribbonHalfWidth(0.3f), 0.004, 1.0e-6);
    check_near("M10 hw clamp alto: hw(9.0) = hw(5) = 0.012",
               (double)hfc::ribbonHalfWidth(9.0f), 0.012, 1.0e-6);
    printf("\n[test_hf_circuit] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
