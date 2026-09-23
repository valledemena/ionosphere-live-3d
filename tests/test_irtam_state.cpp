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
                   s.find("backfill en curso") != std::string::npos);
        check_bool("sin dato: nunca vacio", !s.empty());
    }
    {
        // 429/504/parse-fail viajan en la linea, no solo en tooltip (P5.1).
        const std::string s =
            badgeLine("hmF2", false, 0.0, "http 429", -1.0, false);
        check_bool("error 429 visible en linea",
                   s.find("429") != std::string::npos);
        check_bool("error: reintento pasivo (sin re-burst)",
                   s.find("reintento pasivo") != std::string::npos);
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
                   s.find("sin datos") != std::string::npos);
    }

    std::printf("--- M-irtam-replay-A: zonas de la ventana union ---\n");
    {
        check_bool("72h borde -> IrtamOnly",
                   zoneForAge(259200.0) == ReplayZone::IrtamOnly);
        check_bool("80h -> IrtamOnly",
                   zoneForAge(288000.0) == ReplayZone::IrtamOnly);
        check_bool("96h borde -> IrtamOnly",
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
                   c.find("borde de ventana") != std::string::npos);
        const std::string n = badgeSampled("hmF2", 0.0, 9999.0, false);
        check_bool("sin TOV: honesto", n.find("sin datos") != std::string::npos);
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
                                                e)) == "sin datos");
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
        check_bool("IRTAM estructural exacto [now-345600,now-259200]",
                   i.valid && i.oldest == 1789500000.0 - 345600.0 &&
                       i.newest == 1789500000.0 - 259200.0);
        check_bool("var desconocida: invalid",
                   !layerLoopRange(99, now, r).valid);
    }

    std::printf("\n[test_irtam_state] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
