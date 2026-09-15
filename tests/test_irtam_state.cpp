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
        check_bool("48h -> DeadGap",
                   zoneForAge(172800.0) == ReplayZone::DeadGap);
        check_bool("24h+1s -> DeadGap",
                   zoneForAge(86401.0) == ReplayZone::DeadGap);
        check_bool("24h -> TecOnly",
                   zoneForAge(86400.0) == ReplayZone::TecOnly);
        check_bool("1h -> TecOnly", zoneForAge(3600.0) == ReplayZone::TecOnly);
        check_bool("futuro -> TecOnly",
                   zoneForAge(-100.0) == ReplayZone::TecOnly);
        check_bool("nombres no vacios",
                   std::string(zoneName(ReplayZone::IrtamOnly)).size() > 0 &&
                       std::string(zoneName(ReplayZone::DeadGap)).size() > 0 &&
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

    std::printf("\n[test_irtam_state] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
