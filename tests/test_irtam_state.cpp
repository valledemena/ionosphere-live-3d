// =============================================================================
// test_irtam_state — estados honestos P5 + replay frozen-declarado (C9/G9).
//
// Puro, sin red, sin GL. Llama a PRODUCCION (irtamstate), no replica.
// Cubre: sin-dato con/sin motivo, badge con edad, sufijo replay, nunca vacio.
// =============================================================================
#include "Data/IrtamState.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

int main() {
    using namespace irtamstate;

    std::printf("--- P5: estados honestos ---\n");
    {
        const std::string s = badgeLine("foF2", false, 0.0, "", -1.0, false);
        check_bool("sin dato: menciona backfill",
                   s.find("backfill running") != std::string::npos);
        check_bool("sin dato: nunca vacio", !s.empty());
    }
    {
        // 429/504/parse-fail viajan en la linea, no solo en tooltip (P5.1).
        const std::string s =
            badgeLine("hmF2", false, 0.0, "http 429", -1.0, false);
        check_bool("error 429 visible en linea",
                   s.find("429") != std::string::npos);
        check_bool("error: reintento pasivo (sin re-burst)",
                   s.find("passive retry") != std::string::npos);
    }
    {
        const std::string s =
            badgeLine("foF2", true, 1000.0, "", 4320.0, false);
        check_bool("con dato: edad andante",
                   s.find("4320 min") != std::string::npos);
        check_bool("con dato: sin nota replay en live",
                   s.find("fuera de ventana") == std::string::npos);
    }
    std::printf("--- P5: replay frozen-declarado (C9) ---\n");
    {
        // Mismo bundle en live y replay: solo cambia el sufijo (C9: IRTAM
        // NO participa del muestreo temporal; cero solape 24h vs 72h).
        const std::string live =
            badgeLine("foF2", true, 1000.0, "", 4320.0, false);
        const std::string rep =
            badgeLine("foF2", true, 1000.0, "", 4320.0, true);
        check_bool("replay: nota frozen-declarada",
                   rep.find("fuera de ventana de replay") !=
                       std::string::npos);
        check_bool("replay: conserva edad del dato",
                   rep.find("4320 min") != std::string::npos);
        check_bool("replay: determinista (mismo input, mismo output)",
                   badgeLine("foF2", true, 1000.0, "", 4320.0, true) == rep);
        check_bool("live != replay solo por el sufijo",
                   rep.size() > live.size() &&
                       rep.find(live) != std::string::npos);
    }
    {
        // Sin dato en replay: sigue honesto, sin nota de congelado.
        const std::string s = badgeLine("foF2", false, 0.0, "", -1.0, true);
        check_bool("replay sin dato: honesto",
                   s.find("no data") != std::string::npos);
    }

    std::printf("--- M-irtam-replay-A: zonas de la ventana union ---\n");
    {
        check_bool("72h borde -> IrtamOnly",
                   zoneForAge(259200.0) == ReplayZone::IrtamOnly);
        check_bool("80h -> IrtamOnly",
                   zoneForAge(288000.0) == ReplayZone::IrtamOnly);
        check_bool("168h borde -> IrtamOnly",
                   zoneForAge(604800.0) == ReplayZone::IrtamOnly);
        check_bool("168h+1s borde -> TecOnly",
                   zoneForAge(604801.0) == ReplayZone::TecOnly);
        check_bool("96h interior -> IrtamOnly (banda extendida)",
                   zoneForAge(345600.0) == ReplayZone::IrtamOnly);
        check_bool("48h -> TecOnly (hueco muerto TEC-ext 034)",
                   zoneForAge(172800.0) == ReplayZone::TecOnly);
        check_bool("24h+1s -> TecOnly",
                   zoneForAge(86401.0) == ReplayZone::TecOnly);
        check_bool("24h -> TecOnly",
                   zoneForAge(86400.0) == ReplayZone::TecOnly);
        check_bool("1h -> TecOnly", zoneForAge(3600.0) == ReplayZone::TecOnly);
        check_bool("futuro -> TecOnly",
                   zoneForAge(-100.0) == ReplayZone::TecOnly);
        check_bool("nombres no vacios",
                   std::string(zoneName(ReplayZone::IrtamOnly)).size() > 0 &&
                       std::string(zoneName(ReplayZone::TecOnly)).size() > 0);
    }

    std::printf("--- M-irtam-replay-A: bracket+lerp con reloj simulado ---\n");
    {
        const std::vector<double> tovs = {1000.0, 1900.0, 2800.0};
        TovBracket b = bracketTovs(tovs, 1900.0);
        check_bool("exacto interior: f=1.0 en el tramo",
                   !b.empty && !b.clamped && b.i0 == 0 && b.i1 == 1 &&
                       std::fabs(b.f - 1.0) < 1e-12);
        TovBracket m = bracketTovs(tovs, 1450.0);
        check_bool("medio: f=0.5", !m.empty && !m.clamped && m.i0 == 0 &&
                                       m.i1 == 1 && std::fabs(m.f - 0.5) < 1e-12);
        TovBracket lo = bracketTovs(tovs, 100.0);
        check_bool("bajo rango: clamp al borde",
                   !lo.empty && lo.clamped && lo.i0 == 0 && lo.i1 == 0);
        TovBracket hi = bracketTovs(tovs, 9999.0);
        check_bool("sobre rango: clamp al borde",
                   !hi.empty && hi.clamped && hi.i0 == 2 && hi.i1 == 2);
        std::vector<double> e;
        check_bool("vacio: empty", bracketTovs(e, 1500.0).empty);
        std::vector<double> one = {500.0};
        TovBracket o = bracketTovs(one, 600.0);
        check_bool("singleton: clamp", !o.empty && o.clamped);
    }
    {
        const std::vector<float> a = {1.0f, 2.0f, 4.0f};
        const std::vector<float> b = {3.0f, 6.0f, 8.0f};
        std::vector<float> o;
        check_bool("lerp f=0.25",
                   lerpValues(a, b, 0.25, o) && std::fabs(o[0] - 1.5f) < 1e-6f &&
                       std::fabs(o[2] - 5.0f) < 1e-6f);
        std::vector<float> c = {1.0f};
        check_bool("lerp tamanos distintos = false",
                   !lerpValues(a, c, 0.5, o));
        // Par-fallido al epoch: bracket vacio -> el llamador publica ausente
        // (mismo contrato que evalPairGrids con TOV desigual).
        std::vector<double> noTovs;
        check_bool("sin TOVs: nada que muestrear",
                   bracketTovs(noTovs, 1500.0).empty);
    }

    std::printf("--- M-irtam-replay-A: badge al TOV muestreado ---\n");
    {
        // TOV 2016-05-23T12:00Z = 1464004800.
        const std::string s =
            badgeSampled("foF2", 1464004800.0, 1464004800.0 + 4320.0 * 60.0, false);
        check_bool("nombra el TOV mostrado",
                   s.find("05-23 12:00") != std::string::npos);
        check_bool("edad al TOV muestreado",
                   s.find("4320 min") != std::string::npos);
        check_bool("sin coletilla frozen vieja",
                   s.find("fuera de ventana de replay") == std::string::npos);
        const std::string c =
            badgeSampled("B0", 1464004800.0, 1464004800.0 + 4320.0 * 60.0, true);
        check_bool("clamp con nota de borde",
                   c.find("window edge") != std::string::npos);
        const std::string n = badgeSampled("hmF2", 0.0, 9999.0, false);
        check_bool("sin TOV: honesto", n.find("no data") != std::string::npos);
    }

    std::printf("--- M-irtam-replay-025: hora-dato por capa (rangos runtime) ---\n");
    {
        LayerRanges r;
        r.tecOldest = 1000.0;
        r.tecNewest = 2000.0;
        r.giroOldest = 1500.0;
        r.giroNewest = 2500.0;
        const double now = 100000.0;
        LayerDataTime t = layerDataTime(0, 1500.0, now, r, false);
        check_bool("TEC dentro: hora-dato = epoch, fresco",
                   t.dataEpoch == 1500.0 && !t.stale && !t.clamped);
        LayerDataTime tc = layerDataTime(0, 500.0, now, r, false);
        check_bool("TEC bajo rango: clamp al borde + stale",
                   tc.dataEpoch == 1000.0 && tc.stale && tc.clamped);
        LayerDataTime tc2 = layerDataTime(0, 9999.0, now, r, false);
        check_bool("TEC sobre rango: clamp al borde + stale",
                   tc2.dataEpoch == 2000.0 && tc2.stale && tc2.clamped);
        LayerDataTime g = layerDataTime(3, 2000.0, now, r, false);
        check_bool("GIRO dentro: usa rango GIRO, no TEC",
                   g.dataEpoch == 2000.0 && !g.stale);
        LayerDataTime g2 = layerDataTime(6, 100.0, now, r, false);
        check_bool("GIRO fuera: clamp GIRO",
                   g2.dataEpoch == 1500.0 && g2.stale);
        LayerRanges empty;
        LayerDataTime e = layerDataTime(0, 1500.0, now, empty, false);
        check_bool("TEC sin historia: sin dato + stale",
                   e.dataEpoch < 0.0 && e.stale);
        // IRTAM: ventana [now-96h, now-72h], sin hardcode en el llamador
        // (now realista para epochs positivos).
        const double nowR = 1789500000.0;
        LayerDataTime i =
            layerDataTime(8, nowR - 80.0 * 3600.0, nowR, r, false);
        check_bool("IRTAM en zona: fresco",
                   !i.stale && !i.clamped &&
                       i.dataEpoch == nowR - 80.0 * 3600.0);
        LayerDataTime ib =
            layerDataTime(10, nowR - 10.0 * 3600.0, nowR, r, false);
        check_bool("IRTAM en zona TEC: clamp al borde fresco + stale",
                   ib.clamped && ib.stale &&
                       ib.dataEpoch == nowR - 72.0 * 3600.0);
        LayerDataTime iw =
            layerDataTime(9, nowR - 80.0 * 3600.0, nowR, r, true);
        check_bool("IRTAM worker ocupado: stale (O2 veredicto 024)",
                   iw.stale);
        LayerDataTime u = layerDataTime(99, 1500.0, now, r, false);
        check_bool("variable desconocida: sin dato",
                   u.dataEpoch < 0.0 && u.stale);
    }

    std::printf("--- M-irtam-replay-038: Zone por capa (perLayerZoneName) ---\n");
    {
        // Caso 037: TEC real [now-86.6h, now], cursor now-80h.
        const double now = 1789500000.0;
        LayerRanges r;
        r.tecOldest = now - 86.6 * 3600.0;
        r.tecNewest = now;
        check_bool("1: TEC+IRTAM en cursor now-80h",
                   std::string(perLayerZoneName(now - 80.0 * 3600.0, now,
                                                r)) == "TEC+IRTAM");
        check_bool("2: mismos rangos, cursor now-30h -> solo-TEC",
                   std::string(perLayerZoneName(now - 30.0 * 3600.0, now,
                                                r)) == "solo-TEC");
        check_bool("3: cursor now-90h -> solo-IRTAM",
                   std::string(perLayerZoneName(now - 90.0 * 3600.0, now,
                                                r)) == "solo-IRTAM");
        check_bool("4: borde T-72h inclusivo -> TEC+IRTAM",
                   std::string(perLayerZoneName(now - 72.0 * 3600.0, now,
                                                r)) == "TEC+IRTAM");
        LayerRanges g;
        g.giroOldest = now - 6.0 * 3600.0;
        g.giroNewest = now;
        check_bool("5: TEC vacio + GIRO -> solo-GIRO",
                   std::string(perLayerZoneName(now - 3.0 * 3600.0, now,
                                                g)) == "solo-GIRO");
        LayerRanges tg;
        tg.tecOldest = now - 24.0 * 3600.0;
        tg.tecNewest = now;
        tg.giroOldest = now - 6.0 * 3600.0;
        tg.giroNewest = now;
        check_bool("6: TEC+GIRO en now-3h",
                   std::string(perLayerZoneName(now - 3.0 * 3600.0, now,
                                                tg)) == "TEC+GIRO");
        LayerRanges e;
        check_bool("7: rangos vacios -> sin datos",
                   std::string(perLayerZoneName(now - 3.0 * 3600.0, now,
                                                e)) == "no data");
        LayerRanges all;
        all.tecOldest = now - 86.6 * 3600.0;
        all.tecNewest = now;
        all.giroOldest = now - 86.6 * 3600.0;
        all.giroNewest = now;
        check_bool("8: tres fuentes -> TEC+GIRO+IRTAM (orden canonico)",
                   std::string(perLayerZoneName(now - 80.0 * 3600.0, now,
                                                all)) == "TEC+GIRO+IRTAM");
    }

    std::printf("--- M-irtam-replay-028: bracket de par (testeable) ---\n");
    {
        // TOVs comunes foF2xhmF2 en {1000, 1900, 2800}; B0 huerfano en 1500.
        const std::vector<TovEntry> v = {
            {"fof2", 1000.0}, {"hmf2", 1000.0}, {"fof2", 1900.0},
            {"hmf2", 1900.0}, {"fof2", 2800.0}, {"hmf2", 2800.0},
            {"b0", 1500.0},
        };
        PairBracket b = pairBracketTovs(v, "fof2", "hmf2", 1450.0);
        check_bool("bracket medio f=0.5",
                   !b.empty && !b.clamped && b.t0 == 1000.0 &&
                       b.t1 == 1900.0 && std::fabs(b.f - 0.5) < 1e-12);
        PairBracket c = pairBracketTovs(v, "fof2", "hmf2", 50.0);
        check_bool("bajo rango: clamp t0",
                   !c.empty && c.clamped && c.t0 == 1000.0 && c.t1 == 1000.0);
        PairBracket h = pairBracketTovs(v, "fof2", "hmf2", 9999.0);
        check_bool("sobre rango: clamp t1",
                   !h.empty && h.clamped && h.t0 == 2800.0 && h.t1 == 2800.0);
        // Par B0xB1 sin interseccion (b1 ausente): vacio -> ausente honesto.
        PairBracket e = pairBracketTovs(v, "b0", "b1", 1500.0);
        check_bool("sin par comun: empty", e.empty);
        // Huerfanos no contaminan (b0@1500 sin mate no entra).
        PairBracket m = pairBracketTovs(v, "fof2", "hmf2", 1500.0);
        check_bool("huerfano ignorado: tramo [1000,1900]",
                   !m.empty && m.t0 == 1000.0 && m.t1 == 1900.0);
        std::vector<TovEntry> nov;
        check_bool("vacio: empty",
                   pairBracketTovs(nov, "fof2", "hmf2", 1500.0).empty);
    }

    std::printf("--- M-irtam-replay-031: rango del bucle por capa ---\n");
    {
        LayerRanges r;
        r.tecOldest = 1000.0;
        r.tecNewest = 2000.0;
        r.giroOldest = 1500.0;
        r.giroNewest = 2500.0;
        const double now = 100000.0;
        LayerLoopRange t = layerLoopRange(0, now, r);
        check_bool("TEC valida",
                   t.valid && t.oldest == 1000.0 && t.newest == 2000.0);
        LayerRanges e;
        check_bool("TEC vacia: invalid",
                   !layerLoopRange(0, now, e).valid);
        LayerRanges one;
        one.tecOldest = 1000.0;
        one.tecNewest = 1000.0;
        check_bool("TEC un frame (oldest==newest): invalid",
                   !layerLoopRange(0, now, one).valid);
        LayerLoopRange g = layerLoopRange(3, now, r);
        check_bool("GIRO valida",
                   g.valid && g.oldest == 1500.0 && g.newest == 2500.0);
        LayerRanges g0;
        g0.tecOldest = 1000.0;
        g0.tecNewest = 2000.0;
        check_bool("GIRO histHours=0: invalid",
                   !layerLoopRange(3, now, g0).valid);
        LayerLoopRange i = layerLoopRange(8, 1789500000.0, r);
        check_bool("IRTAM estructural exacto [now-604800,now-259200]",
                   i.valid && i.oldest == 1789500000.0 - 604800.0 &&
                       i.newest == 1789500000.0 - 259200.0);
        check_bool("var desconocida: invalid",
                   !layerLoopRange(99, now, r).valid);
    }

    std::printf("--- M-O3-053: magnitud del retraso (staleSuffix) ---\n");
    {
        LayerDataTime fresh;
        fresh.dataEpoch = 1789500000.0;
        check_bool("1: fresco -> vacio",
                   staleSuffix(fresh, false).empty());
        LayerDataTime s0;
        s0.dataEpoch = 1789500000.0;
        s0.stale = true;
        check_bool("2: stale sin gap -> (stale)",
                   staleSuffix(s0, false) == "(stale)");
        LayerDataTime s8;
        s8.dataEpoch = 1789500000.0 - 8.0 * 3600.0;
        s8.stale = true;
        s8.gapSec = 8.0 * 3600.0;
        check_bool("3: stale +8h",
                   staleSuffix(s8, false) == "(stale, +8.0 h)");
        LayerDataTime s90;
        s90.dataEpoch = 1789500000.0 - 90.0 * 60.0;
        s90.stale = true;
        s90.gapSec = 90.0 * 60.0;
        check_bool("4: stale 90 min -> +1.5 h (escala >=3600)",
                   staleSuffix(s90, false) == "(stale, +1.5 h)");
        LayerDataTime s45;
        s45.dataEpoch = 1789500000.0 - 45.0 * 60.0;
        s45.stale = true;
        s45.gapSec = 45.0 * 60.0;
        check_bool("4b: stale 45 min -> +45 min",
                   staleSuffix(s45, false) == "(stale, +45 min)");
        check_bool("5: stale + worker sin gap",
                   staleSuffix(s0, true) == "(stale, worker)");
        check_bool("6: stale 90 min + worker -> +1.5 h",
                   staleSuffix(s90, true) == "(stale, +1.5 h, worker)");
        check_bool("7: borde 3599 s -> +59 min",
                   staleGapText(3599.0) == "+59 min");
        check_bool("8: borde 3600 s -> +1.0 h",
                   staleGapText(3600.0) == "+1.0 h");
        check_bool("9: gap 0 -> vacio", staleGapText(0.0).empty());
        LayerRanges rg;
        rg.tecOldest = 1000.0;
        rg.tecNewest = 2000.0;
        LayerDataTime e2e = layerDataTime(0, 2000.0 + 8.0 * 3600.0, 99999.0,
                                          rg, false);
        check_bool("10: end-to-end TEC mas alla del borde -> gap 8h",
                   e2e.stale && e2e.gapSec == 8.0 * 3600.0 &&
                       staleSuffix(e2e, false) == "(stale, +8.0 h)");
    }

    std::printf("--- DW.14: replay salta huecos sin datos ---\n");
    {
        // 75 frames de septiembre con el hueco real de 2010 s (17:45-18:18:30).
        std::vector<double> sep75;
        for (int i = 0; i < 72; i++) sep75.push_back(1000.0 + 600.0 * i);
        sep75.push_back(sep75.back() + 2010.0);
        sep75.push_back(sep75.back() + 600.0);
        sep75.push_back(sep75.back() + 600.0);
        check_bool("d14-1: hueco 2010 s con 1800 -> 2 clusters",
                   loopClusters(sep75, 1800.0).size() == 2);
        check_bool("d14-2: hueco 2010 s con 3600 -> 1 cluster",
                   loopClusters(sep75, 3600.0).size() == 1);

        // Cache real modelada: 75 frames Sep + salto 6160905 s + 245 Nov.
        std::vector<double> two;
        const double x0 = 1000.0;
        for (int i = 0; i < 75; i++) two.push_back(x0 + 600.0 * i);
        const double sepLast = two.back();
        const double novFirst = sepLast + 6160905.0;
        for (int i = 0; i < 245; i++) two.push_back(novFirst + 600.0 * i);
        const std::vector<LoopCluster> cl2 = loopClusters(two, kLoopGapJoinSec);
        check_bool("d14-3: 320 epochs -> 2 clusters", cl2.size() == 2);
        check_bool("d14-4: bordes 75/245 correctos",
                   cl2[0].first == x0 && cl2[0].last == sepLast &&
                       cl2[1].first == novFirst);

        double skipped = -1.0;
        const double jumped =
            advanceLoopEpoch(sepLast - 300.0, 600.0, cl2, x0, cl2[1].last,
                             kLoopGapJoinSec, &skipped);
        check_bool("d14-5: salto exacto al inicio del siguiente cluster",
                   jumped == novFirst);
        check_bool("d14-6: skippedSec = hueco real 6160905",
                   skipped == 6160905.0);

        const std::vector<LoopCluster> one{{0.0, 2000.0}};
        double s2 = -1.0;
        check_bool("d14-7: single cluster avance lineal",
                   advanceLoopEpoch(1000.0, 600.0, one, 0.0, 2000.0, 3600.0,
                                    &s2) == 1600.0 &&
                       s2 == 0.0);
        check_bool("d14-8: wrap a lo",
                   advanceLoopEpoch(1900.0, 600.0, one, 0.0, 2000.0, 3600.0,
                                    &s2) == 0.0);

        auto gapSkip = [&](double gap) {
            const std::vector<LoopCluster> cs{
                {0.0, 1000.0}, {1000.0 + gap, 2000.0 + gap}};
            double sk = -1.0;
            const double r = advanceLoopEpoch(900.0, 200.0, cs, 0.0,
                                              2000.0 + gap, kLoopGapJoinSec,
                                              &sk);
            return r * 1e6 + sk;  // empaqueta para comparar sin std::pair
        };
        const double b3599 = gapSkip(3599.0);
        const double b3600 = gapSkip(3600.0);
        const double b3601 = gapSkip(3601.0);
        check_bool("d14-9: gap 3599 no salta",
                   b3599 == 1100.0 * 1e6 + 0.0);
        check_bool("d14-10: gap 3600 no salta (<= join)",
                   b3600 == 1100.0 * 1e6 + 0.0);
        check_bool("d14-11: gap 3601 salta",
                   b3601 == 4601.0 * 1e6 + 3601.0);

        double cur = x0;
        bool ok = true;
        bool wrapped = false;
        int jumps = 0;
        for (int i = 0; i < 5000; i++) {
            double sk = 0.0;
            const double nx = advanceLoopEpoch(cur, 1200.0, cl2, x0,
                                              cl2[1].last, kLoopGapJoinSec, &sk);
            if (nx == x0 && cur != x0) {
                wrapped = true;  // cierre de un loop completo
                break;
            }
            if (!insideLoopData(nx, cl2, 0.0)) {
                ok = false;
                break;
            }
            if (sk > 0.0) ++jumps;
            cur = nx;
        }
        check_bool("d14-12: ningun paso cae en hueco > join", ok);
        check_bool("d14-13: un loop completo, un unico salto",
                   wrapped && jumps == 1);

        check_bool("d14-14: cursor en cluster -> con dato",
                   insideLoopData(sepLast, cl2, 0.0));
        check_bool("d14-15: cursor en el hueco -> sin dato",
                   !insideLoopData(sepLast + 3000.0, cl2, 0.0));
        check_bool("d14-16: margen join cubre 3000 s",
                   insideLoopData(sepLast + 3000.0, cl2, kLoopGapJoinSec));
    }

    std::printf("--- DW.15: circuito HF con dato GIRO fresco ---\n");
    {
        LayerRanges g;
        g.giroOldest = 1000.0;
        g.giroNewest = 96400.0;   // 26.5 h
        check_bool("d15-1: dentro del rango -> fresco",
                   circuitHasFreshData(50000.0, 100000.0, g, false));
        check_bool("d15-2: borde oldest exacto -> fresco",
                   circuitHasFreshData(1000.0, 100000.0, g, false));
        check_bool("d15-3: borde newest exacto -> fresco",
                   circuitHasFreshData(96400.0, 100000.0, g, false));
        check_bool("d15-4: 1 s antes de oldest -> sin dato",
                   !circuitHasFreshData(999.0, 100000.0, g, false));
        check_bool("d15-5: 1 s despues de newest -> sin dato",
                   !circuitHasFreshData(96401.0, 100000.0, g, false));
        check_bool("d15-6: worker ocupado -> sin dato (aunque ldt.stale=false)",
                   !circuitHasFreshData(50000.0, 100000.0, g, true));
        LayerRanges empty;
        check_bool("d15-7: historial vacio -> sin dato",
                   !circuitHasFreshData(50000.0, 100000.0, empty, false));
        const double gap = g.giroOldest - 999.0;
        check_bool("d15-8: gapSec=1 -> +1 min", staleGapText(gap) == "+1 min");
        check_bool("d15-9: gapSec=5400 -> +1.5 h",
                   staleGapText(5400.0) == "+1.5 h");
        const LayerDataTime before = layerDataTime(1, 999.0, 100000.0, g, false);
        check_bool("d15-10: antes del borde stale=true y gapSec=0",
                   before.stale && before.gapSec == 0.0);
    }

    std::printf("--- DW.15 v3: rango de la fuente activa ---\n");
    {
        // Rango kc2g sintetico (historyStats t0=min, t1=max).
        const double t0 = 1000.0, t1 = 96400.0;
        check_bool("d15-11: kc2g interior -> fresco",
                   circuitHasFreshDataInRange(50000.0, t0, t1, false));
        check_bool("d15-12: kc2g borde t0 exacto -> fresco",
                   circuitHasFreshDataInRange(t0, t0, t1, false));
        check_bool("d15-13: kc2g borde t1 exacto -> fresco",
                   circuitHasFreshDataInRange(t1, t0, t1, false));
        check_bool("d15-14: 1 s antes de t0 -> sin dato",
                   !circuitHasFreshDataInRange(t0 - 1.0, t0, t1, false));
        check_bool("d15-15: 1 s despues de t1 -> sin dato",
                   !circuitHasFreshDataInRange(t1 + 1.0, t0, t1, false));
        check_bool("d15-16: worker ocupado -> sin dato",
                   !circuitHasFreshDataInRange(50000.0, t0, t1, true));
        check_bool("d15-17: rango invalido -> sin dato",
                   !circuitHasFreshDataInRange(50000.0, 0.0, 0.0, false));
        LayerRanges kg;
        kg.giroOldest = t0; kg.giroNewest = t1;
        check_bool("d15-18: wrapper v2 conserva semantica GIRO",
                   circuitHasFreshData(50000.0, 100000.0, kg, false) &&
                       !circuitHasFreshData(999.0, 100000.0, kg, false));
    }

    std::printf("\n[test_irtam_state] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
