// =============================================================================
// test_rbe_parse — adaptador de eventos RBE de DONKI (lote 5, modulo C).
//
// Puro: sin red, sin GL, sin disco. Llama a PRODUCCION (RbeAdapter::parseRbe),
// no replica el parser. Cubre: 3 eventos reales desordenados -> orden por
// eventTime descendente, instrumento y link, nota; array vacio (valido, NO
// error); sin `instruments` -> instrumento vacio pero evento valido;
// `eventTime` ausente -> descartado; JSON truncado/basura -> invalido con
// motivo; 12 eventos -> recorte a 8; epoch del primero calculado a mano.
// =============================================================================
#include "Data/RbeAdapter.h"

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

static std::string isoFromEpoch(double t) {
    time_t tt = (time_t)t;
    struct tm g;
#ifdef _WIN32
    gmtime_s(&g, &tt);
#else
    gmtime_r(&tt, &g);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02dZ", g.tm_year + 1900,
                  g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min);
    return buf;
}

// Forma real (campos recortados del JSON vivo del 2026-10-27). A proposito
// desordenado: DONKI publica del mas viejo al mas nuevo.
static const char* kThree = R"JSON([
 {"rbeID":"2026-10-10T14:35:00-RBE-001","eventTime":"2026-10-10T14:35Z",
  "instruments":[{"displayName":"GOES-P: SEISS >2MeV"}],
  "link":"https://ccmc.gsfc.nasa.gov/DONKI/view/RBE/49055/-1",
  "linkedEvents":[{"activityID":"2026-10-06T12:38:00-CME-001"}],
  "sentNotifications":null,"submissionTime":"2026-10-10T14:41Z","versionId":1},
 {"rbeID":"2026-10-22T20:20:00-RBE-001","eventTime":"2026-10-22T20:20Z",
  "instruments":[{"displayName":"GOES-P: SEISS >2MeV"}],
  "link":"https://ccmc.gsfc.nasa.gov/DONKI/view/RBE/49205/-1",
  "linkedEvents":[{"activityID":"2026-10-16T12:38:00-CME-001"}],
  "sentNotifications":null,"submissionTime":"2026-10-22T20:26Z","versionId":1},
 {"rbeID":"2026-10-16T13:20:00-RBE-001","eventTime":"2026-10-16T13:20Z",
  "instruments":[{"displayName":"GOES-P: SEISS >2MeV"},{"displayName":"OTHER"}],
  "link":"https://ccmc.gsfc.nasa.gov/DONKI/view/RBE/49129/-1",
  "linkedEvents":[],"sentNotifications":null,"submissionTime":"2026-10-16T13:30Z","versionId":1}
])JSON";

int main() {
    const double now = 1793000000.0;   // epoch fijo: determinismo

    std::printf("--- rbe: 3 eventos reales, orden por eventTime ---\n");
    {
        RbeSnapshot s;
        const bool ok = RbeAdapter::parseRbe(kThree, now, s);
        check_bool("1: parse ok", ok);
        check_bool("2: valid", s.valid);
        check_bool("3: 3 eventos", s.events.size() == 3);
        check_bool("4: timeUtc = now", s.timeUtc == now);
        check_bool("5: fromCache false", !s.fromCache);
        check_bool("6: note '3 RBE events'", s.note == "3 RBE events");
        check_bool("7: primero el mas reciente (2026-10-22)",
                   s.events.size() == 3 &&
                       s.events[0].id == "2026-10-22T20:20:00-RBE-001");
        check_bool("8: segundo 2026-10-16",
                   s.events.size() == 3 &&
                       s.events[1].id == "2026-10-16T13:20:00-RBE-001");
        check_bool("9: tercero 2026-10-10",
                   s.events.size() == 3 &&
                       s.events[2].id == "2026-10-10T14:35:00-RBE-001");
        bool desc = true;
        for (size_t i = 1; i < s.events.size(); i++)
            if (!(s.events[i].timeUtc < s.events[i - 1].timeUtc)) desc = false;
        check_bool("10: orden descendente estricto", desc);
        check_bool("11: instrumento del primero",
                   s.events.size() == 3 && s.events[0].instrument == "GOES-P: SEISS >2MeV");
        check_bool("12: instrumento solo el primero del array",
                   s.events.size() == 3 && s.events[1].instrument == "GOES-P: SEISS >2MeV");
        check_bool("13: links correctos",
                   s.events.size() == 3 &&
                       s.events[0].link == "https://ccmc.gsfc.nasa.gov/DONKI/view/RBE/49205/-1" &&
                       s.events[1].link == "https://ccmc.gsfc.nasa.gov/DONKI/view/RBE/49129/-1" &&
                       s.events[2].link == "https://ccmc.gsfc.nasa.gov/DONKI/view/RBE/49055/-1");
        check_bool("14: eventTimeIso crudo",
                   s.events.size() == 3 && s.events[0].eventTimeIso == "2026-10-22T20:20Z");
        // 2026-10-22T20:20Z = 1792700400 (calculado a mano: dias desde la epoch
        // x 86400 + 20*3600 + 20*60).
        check_bool("15: timeUtc del primero = 1792700400",
                   s.events.size() == 3 && s.events[0].timeUtc == 1792700400.0);
        check_bool("16: timeUtc del tercero = 1791642900",
                   s.events.size() == 3 && s.events[2].timeUtc == 1791642900.0);
    }

    std::printf("--- rbe: array vacio es valido ---\n");
    {
        RbeSnapshot s;
        const bool ok = RbeAdapter::parseRbe("[]", now, s);
        check_bool("17: '[]' parse ok", ok);
        check_bool("18: '[]' valid", s.valid);
        check_bool("19: '[]' 0 eventos", s.events.empty());
        check_bool("20: '[]' note 'no RBE events in 58 days'",
                   s.note == "no RBE events in 58 days");
        RbeSnapshot w;
        const bool ok2 = RbeAdapter::parseRbe("  [ ]  ", now, w);
        check_bool("21: '[ ]' con espacios ok", ok2 && w.valid && w.events.empty());
    }

    std::printf("--- rbe: campos ausentes ---\n");
    {
        // Sin instruments ni link, con segundos en eventTime.
        const std::string j =
            "[{\"rbeID\":\"2026-10-24T01:02:00-RBE-001\","
            "\"eventTime\":\"2026-10-24T01:02:30Z\"}]";
        RbeSnapshot s;
        check_bool("22: parse de evento minimo", RbeAdapter::parseRbe(j, now, s));
        check_bool("23: 1 evento", s.events.size() == 1);
        check_bool("24: instrumento vacio pero valido",
                   s.events.size() == 1 && s.events[0].instrument.empty() &&
                       s.events[0].timeUtc > 0.0);
        check_bool("25: link vacio", s.events.size() == 1 && s.events[0].link.empty());
        check_bool("26: segundos respetados (= 1792803750)",
                   s.events.size() == 1 && s.events[0].timeUtc == 1792803750.0);
    }

    std::printf("--- rbe: eventTime ausente -> descartado ---\n");
    {
        const std::string j =
            "[{\"rbeID\":\"A\",\"eventTime\":\"2026-10-20T10:00Z\"},"
            "{\"rbeID\":\"B\",\"instruments\":[{\"displayName\":\"X\"}]},"
            "{\"rbeID\":\"C\",\"eventTime\":null},"
            "{\"rbeID\":\"D\",\"eventTime\":\"not a date\"}]";
        RbeSnapshot s;
        const bool ok = RbeAdapter::parseRbe(j, now, s);
        check_bool("27: parse ok", ok && s.valid);
        check_bool("28: solo 1 de 4 sobrevive", s.events.size() == 1);
        check_bool("29: el superviviente es A", s.events.size() == 1 && s.events[0].id == "A");
        check_bool("30: note '1 RBE events'", s.note == "1 RBE events");
    }

    std::printf("--- rbe: JSON basura/truncado es invalido ---\n");
    {
        const char* bad[] = { "no json", "", "{ }", "[{\"rbeID\":\"x\"",
                              "[{\"rbeID\":\"x\",\"eventTime\":\"2026-10-20T10:00Z\"},",
                              "<html>502</html>", "[nonsense]" };
        bool allBad = true;
        for (const char* b : bad) {
            RbeSnapshot s;
            if (RbeAdapter::parseRbe(b, now, s) || s.valid || s.note.empty()) allBad = false;
        }
        check_bool("31: basura/truncado -> false, invalido y con motivo", allBad);
        RbeSnapshot t;
        RbeAdapter::parseRbe("[{\"rbeID\":\"x\"", now, t);
        check_bool("32: motivo 'JSON truncado'", t.note == "JSON truncado");
        // Un snapshot valido previo no debe sobrevivir a un parseo fallido.
        RbeSnapshot s;
        s.valid = true;
        s.events.resize(7);
        s.note = "3 RBE events";
        const bool ok = RbeAdapter::parseRbe("garbage", now, s);
        check_bool("33: resetea el snapshot de salida",
                   !ok && !s.valid && s.events.empty() && s.note != "3 RBE events");
    }

    std::printf("--- rbe: recorte a 8 ---\n");
    {
        const double base = 1792900000.0;   // 2026-10-25T00:00Z aprox.
        std::string j = "[";
        for (int i = 0; i < 12; i++) {
            const std::string t = isoFromEpoch(base + (double)i * 60.0);
            char id[64];
            std::snprintf(id, sizeof(id), "GEN-%02d", i);
            if (i > 0) j += ",";
            j += "{\"rbeID\":\"" + std::string(id) + "\",\"eventTime\":\"" + t +
                 "\",\"instruments\":[{\"displayName\":\"GOES-P: SEISS >2MeV\"}],"
                 "\"link\":\"https://ccmc.gsfc.nasa.gov/DONKI/view/RBE/1/-1\"}";
        }
        j += "]";

        RbeSnapshot s;
        const bool ok = RbeAdapter::parseRbe(j, now, s);
        check_bool("34: JSON de 12 parsea", ok && s.valid);
        check_bool("35: recorte a 8", s.events.size() == 8);
        check_bool("36: el mas reciente primero", s.events.size() == 8 &&
                                                   s.events[0].id == "GEN-11");
        check_bool("37: el ultimo publicado es GEN-04",
                   s.events.size() == 8 && s.events[7].id == "GEN-04");
        bool desc = true;
        for (size_t i = 1; i < s.events.size(); i++)
            if (s.events[i].timeUtc > s.events[i - 1].timeUtc) desc = false;
        check_bool("38: orden descendente", desc);
        check_bool("39: note '8 RBE events'", s.note == "8 RBE events");
    }

    std::printf("[test_rbe_parse] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
