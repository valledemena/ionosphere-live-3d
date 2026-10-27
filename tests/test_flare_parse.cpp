// =============================================================================
// test_flare_parse — adaptador de fulguraciones DONKI FLR (lote 4, modulo C).
//
// Puro: sin red, sin GL, sin disco. Llama a PRODUCCION (FlareAdapter), no
// replica el parser. Cubre: 3 eventos reales con orden por peakTime, ranks
// C/M/X, flrRank validos e invalidos, activeRegionNum ausente/null, peakTime
// ausente -> beginTime, array vacio (valido, NO error), JSON basura/truncado
// (invalido), recorte a 50 y reseteo del snapshot de salida.
// =============================================================================
#include "Data/FlareAdapter.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* what, bool cond) {
    if (cond)
        ++g_ok;
    else
        ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static std::string isoFromEpoch(double t) {
    time_t tt = (time_t)t;
    struct tm g;
#ifdef _WIN32
    gmtime_s(&g, &tt);
#else
    gmtime_r(&tt, &g);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", g.tm_year + 1900,
                  g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec);
    return buf;
}

// Forma real de la respuesta (campos recortados del JSON vivo del 2026-10-27).
static const char* kThree = R"JSON([
 {"flrID":"2026-10-27T05:00:00-FLR-001","catalog":"M2M_CATALOG",
  "instruments":[{"displayName":"GOES-P: EXIS 1.0-8.0"}],
  "beginTime":"2026-10-27T05:00Z","peakTime":"2026-10-27T05:09Z",
  "endTime":"2026-10-27T05:20Z","classType":"X2.0","sourceLocation":"N20W30",
  "activeRegionNum":14570,"note":"","link":"https://ccmc.gsfc.nasa.gov/DONKI/view/FLR/1/-1",
  "linkedEvents":[{"activityID":"2026-10-27T05:30:00-CME-001"}],
  "sentNotifications":null,"submissionTime":"2026-10-27T06:10Z","versionId":1},
 {"flrID":"2026-10-26T18:17:00-FLR-001","catalog":"M2M_CATALOG",
  "beginTime":"2026-10-26T18:17Z","peakTime":"2026-10-26T18:40Z",
  "endTime":"2026-10-26T19:02Z","classType":"M1.2","sourceLocation":"S12W77",
  "activeRegionNum":14561,"note":"","linkedEvents":null,"sentNotifications":null},
 {"flrID":"2026-10-25T08:21:00-FLR-001","catalog":"M2M_CATALOG",
  "beginTime":"2026-10-25T08:21Z","endTime":"2026-10-25T08:51Z",
  "classType":"C5.4","sourceLocation":"","activeRegionNum":null,"note":""}
])JSON";

int main() {
    const double now = 1793000000.0;   // epoch fijo: determinismo

    std::printf("--- flare: 3 eventos reales, orden por peakTime ---\n");
    {
        FlareSnapshot s;
        const bool ok = FlareAdapter::parseFlares(kThree, now, s);
        check_bool("1: parse ok", ok);
        check_bool("2: valid", s.valid);
        check_bool("3: 3 eventos", s.events.size() == 3);
        check_bool("4: orden X/M/C",
                   s.events.size() == 3 && s.events[0].classType == "X2.0" &&
                       s.events[1].classType == "M1.2" &&
                       s.events[2].classType == "C5.4");
        check_bool("5: rank X2.0 = 102", s.events.size() == 3 && near(s.events[0].rank, 102.0f));
        check_bool("6: rank M1.2 = 11.2", s.events.size() == 3 && near(s.events[1].rank, 11.2f));
        check_bool("7: rank C5.4 = 5.4", s.events.size() == 3 && near(s.events[2].rank, 5.4f));
        check_bool("8: timeUtc = now", s.timeUtc == now);
        check_bool("9: fromCache false", !s.fromCache);
        check_bool("10: note '3 flares'", s.note == "3 flares");
        check_bool("11: id del primero", s.events.size() == 3 &&
                                             s.events[0].id == "2026-10-27T05:00:00-FLR-001");
        check_bool("12: activeRegion 14570", s.events.size() == 3 && s.events[0].activeRegion == 14570);
        check_bool("13: sourceLocation N20W30",
                   s.events.size() == 3 && s.events[0].sourceLocation == "N20W30");
        check_bool("14: hasPeak + peak > begin",
                   s.events.size() == 3 && s.events[0].hasPeak &&
                       s.events[0].peakUtc > s.events[0].beginUtc);
        check_bool("15: endUtc parseado", s.events.size() == 3 && s.events[0].endUtc > 0.0);
        check_bool("16: activeRegionNum null -> -1",
                   s.events.size() == 3 && s.events[2].activeRegion == -1);
        check_bool("17: sin peakTime -> hasPeak false y peakUtc = beginUtc",
                   s.events.size() == 3 && !s.events[2].hasPeak &&
                       s.events[2].peakUtc == s.events[2].beginUtc &&
                       s.events[2].peakUtc > 0.0);
    }

    std::printf("--- flare: flrRank ---\n");
    {
        float r = 0.0f;
        check_bool("18: X2.0 -> 102",
                   FlareAdapter::flrRank("X2.0", r) && near(r, 102.0f));
        check_bool("19: M1.2 -> 11.2",
                   FlareAdapter::flrRank("M1.2", r) && near(r, 11.2f));
        check_bool("20: C5.4 -> 5.4",
                   FlareAdapter::flrRank("C5.4", r) && near(r, 5.4f));
        check_bool("21: C1.0 -> 1",
                   FlareAdapter::flrRank("C1.0", r) && near(r, 1.0f));
        check_bool("22: M9.9 -> 19.9",
                   FlareAdapter::flrRank("M9.9", r) && near(r, 19.9f));
        check_bool("23: m10 (minuscula) rechazado", !FlareAdapter::flrRank("m10", r));
        check_bool("24: vacio rechazado", !FlareAdapter::flrRank("", r));
        check_bool("25: Z9 rechazado", !FlareAdapter::flrRank("Z9", r));
        check_bool("26: 'M' sin numero rechazado", !FlareAdapter::flrRank("M", r));
        check_bool("27: 'M1.2x' rechazado", !FlareAdapter::flrRank("M1.2x", r));
        check_bool("28: 'C.' rechazado", !FlareAdapter::flrRank("C.", r));
        r = 7.5f;
        const bool bad = FlareAdapter::flrRank("M1.2x", r);
        check_bool("29: rank intacto si falla", !bad && near(r, 7.5f));
    }

    std::printf("--- flare: campos ausentes ---\n");
    {
        // activeRegionNum ausente del todo (no null) y peakTime ausente.
        const std::string j =
            "[{\"flrID\":\"2026-10-24T01:02:00-FLR-001\","
            "\"beginTime\":\"2026-10-24T01:02Z\",\"classType\":\"C1.0\"}]";
        FlareSnapshot s;
        check_bool("30: parse de evento minimo", FlareAdapter::parseFlares(j, now, s));
        check_bool("31: 1 evento", s.events.size() == 1);
        check_bool("32: activeRegionNum ausente -> -1",
                   s.events.size() == 1 && s.events[0].activeRegion == -1);
        check_bool("33: peakTime ausente -> beginUtc",
                   s.events.size() == 1 && s.events[0].hasPeak == false &&
                       s.events[0].peakUtc > 0.0 &&
                       s.events[0].peakUtc == s.events[0].beginUtc);
        check_bool("34: sin endTime -> endUtc 0", s.events.size() == 1 && s.events[0].endUtc == 0.0);
        check_bool("35: rank C1.0 = 1", s.events.size() == 1 && near(s.events[0].rank, 1.0f));
        check_bool("36: sourceLocation ausente -> vacio",
                   s.events.size() == 1 && s.events[0].sourceLocation.empty());
    }

    std::printf("--- flare: array vacio es valido ---\n");
    {
        FlareSnapshot s;
        const bool ok = FlareAdapter::parseFlares("[]", now, s);
        check_bool("37: '[]' parse ok", ok);
        check_bool("38: '[]' valid", s.valid);
        check_bool("39: '[]' 0 eventos", s.events.empty());
        check_bool("40: '[]' note sin fulguraciones", s.note == "sin fulguraciones");
        FlareSnapshot w;
        const bool ok2 = FlareAdapter::parseFlares("  [ ]  ", now, w);
        check_bool("41: '[ ]' con espacios ok", ok2 && w.valid && w.events.empty());
    }

    std::printf("--- flare: JSON basura/truncado es invalido ---\n");
    {
        const char* bad[] = { "no json", "", "{ }", "[{\"flrID\":\"x\"",
                              "<html>502</html>" };
        bool allBad = true;
        for (const char* b : bad) {
            FlareSnapshot s;
            if (FlareAdapter::parseFlares(b, now, s) || s.valid || s.note.empty()) allBad = false;
        }
        check_bool("42: basura/truncado -> false, invalido y con motivo", allBad);
        // Un snapshot valido previo no debe sobrevivir a un parseo fallido.
        FlareSnapshot s;
        s.valid = true;
        s.events.resize(7);
        s.note = "3 flares";
        const bool ok = FlareAdapter::parseFlares("garbage", now, s);
        check_bool("43: resetea el snapshot de salida",
                   !ok && !s.valid && s.events.empty() && s.note != "3 flares");
    }

    std::printf("--- flare: recorte a 50 ---\n");
    {
        const double base = 1792900000.0;
        std::string j = "[";
        for (int i = 0; i < 60; i++) {
            const std::string t = isoFromEpoch(base + (double)i * 60.0);
            char id[64];
            std::snprintf(id, sizeof(id), "GEN-%02d", i);
            if (i > 0) j += ",";
            j += "{\"flrID\":\"" + std::string(id) + "\",\"beginTime\":\"" + t +
                 "\",\"peakTime\":\"" + t + "\",\"endTime\":\"" + t +
                 "\",\"classType\":\"C1.0\",\"activeRegionNum\":14561}";
        }
        j += "]";

        FlareSnapshot s;
        const bool ok = FlareAdapter::parseFlares(j, now, s);
        check_bool("44: JSON de 60 parsea", ok && s.valid);
        check_bool("45: recorte a 50", s.events.size() == 50);
        check_bool("46: el mas reciente primero", !s.events.empty() && s.events[0].id == "GEN-59");
        check_bool("47: el ultimo es GEN-10", s.events.size() == 50 && s.events[49].id == "GEN-10");
        bool desc = true;
        for (size_t i = 1; i < s.events.size(); i++)
            if (s.events[i].peakUtc > s.events[i - 1].peakUtc) desc = false;
        check_bool("48: orden descendente estricto", desc);
        check_bool("49: note '50 flares'", s.note == "50 flares");
    }

    std::printf("[test_flare_parse] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
