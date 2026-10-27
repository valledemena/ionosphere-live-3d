// =============================================================================
// test_ae_parse — parser del indice AE real de Kyoto WDC (AeAdapter).
//
// Puro, SIN red, sin GL. Llama a PRODUCCION (AeAdapter::parseAeFile / fileName
// / buildUrl / writeCacheFile / readCacheFile), no replica el parseo.
//
// Cubre: 2 horas sinteticas (120 muestras, columna AE, no AL/AO/AU), nombre y
// URL reales, linea de 59 valores descartada, cabecera distinta, fichero vacio,
// recorte a 1440 con 25 horas, epoch exacto del ultimo minuto, formato REAL de
// Kyoto (61 campos = 60 minutos + media horaria, que NO se ingiere) y
// round-trip de cache en %TEMP%.
// =============================================================================
#include "Data/AeAdapter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int g_ok = 0, g_fail = 0;
static void check_bool(const char* what, bool cond) {
    if (cond)
        ++g_ok;
    else
        ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

// Epochs calculados a mano (UTC, sin red):
//   2026-10-01 00:00 = 1790812800
//   2026-10-01 00:59 = 1790816340
//   2026-10-01 01:00 = 1790816400
//   2026-10-01 01:59 = 1790819940
//   2026-10-01 02:00 = 1790820000
//   2026-10-01 02:59 = 1790823540
//   2026-10-02 00:59 = 1790902740   (dia 25 h: 2026-10-01 + 24 h + 59 min)
//   2026-10-11 23:59 = 1791763140
static const double kEpoch20261001T0000 = 1790812800.0;
static const double kEpoch20261001T0059 = 1790816340.0;
static const double kEpoch20261001T0100 = 1790816400.0;
static const double kEpoch20261001T0159 = 1790819940.0;
static const double kEpoch20261001T0200 = 1790820000.0;
static const double kEpoch20261001T0259 = 1790823540.0;
static const double kEpoch20261002T0059 = 1790902740.0;
static const double kEpoch20261011T2359 = 1791763140.0;

// Linea con el layout real: prefijo de 34 chars + campos de 6 a la derecha.
static std::string rawLine(const std::string& day6, int hh, const std::vector<long>& vals) {
    char head[64];
    std::snprintf(head, sizeof(head), "AEALAOAU    %sE%02dAE QUICKLK", day6.c_str(), hh);
    std::string out(head);
    for (long v : vals) {
        char f[16];
        std::snprintf(f, sizeof(f), "%6ld", v);
        out += f;
    }
    return out;
}

// Linea como la publica Kyoto: 60 minutos + media horaria redondeada (61 campos).
static std::string realLine(const std::string& day6, int hh, const std::vector<long>& vals) {
    std::vector<long> v = vals;
    double sum = 0.0;
    for (long x : vals) sum += (double)x;
    v.push_back((long)std::lround(sum / (double)vals.size()));
    return rawLine(day6, hh, v);
}

static std::vector<long> ramp(long first, int n) {
    std::vector<long> v;
    v.reserve((size_t)n);
    for (int i = 0; i < n; i++) v.push_back(first + i);
    return v;
}

int main() {
    std::printf("--- ae-parse: formato de linea y columnas ---\n");
    {
        // (1) Dos horas (E00, E01) con 60 valores cada una.
        const std::vector<long> h0 = ramp(100, 60);
        const std::vector<long> h1 = ramp(200, 60);
        const std::string body = rawLine("261001", 0, h0) + "\n" + rawLine("261001", 1, h1) + "\n";

        AeSnapshot s;
        check_bool("1: 2 horas parsean", AeAdapter::parseAeFile(body, 2026, 10, 1, s));
        check_bool("1b: 120 muestras", s.samples.size() == 120);
        check_bool("1c: primera 00:00 (1790812800)",
                   !s.samples.empty() && s.samples.front().timeUtc == kEpoch20261001T0000);
        check_bool("1d: ultima 01:59 (1790819940); snapshot.timeUtc",
                   s.samples.back().timeUtc == kEpoch20261001T0159 &&
                       s.timeUtc == kEpoch20261001T0159);
        check_bool("1e: columna AE 100..159 (no AL/AO/AU)",
                   s.samples[0].ae == 100.0f && s.samples[59].ae == 159.0f &&
                       s.samples[0].al == 0.0f && s.samples[0].ao == 0.0f &&
                       s.samples[0].au == 0.0f);
        check_bool("1f: hora 1 minutos 0 y 59", s.samples[60].ae == 200.0f &&
                                                    s.samples[119].ae == 259.0f &&
                                                    s.samples[60].timeUtc == kEpoch20261001T0100);
        check_bool("1g: valido + note", s.valid &&
                                           s.note.find("120 muestras") != std::string::npos);
    }

    std::printf("--- ae-parse: nombre y URL ---\n");
    {
        // (2) Nombre de fichero con ano de DOS digitos.
        check_bool("2: fileName(2026,10,1) == ae261001",
                   AeAdapter::fileName(2026, 10, 1) == "ae261001");
        const std::string u = AeAdapter::buildUrl(2026, 10, 1);
        check_bool("2b: buildUrl https + /2026/10/01/ae261001",
                   u.rfind("https://", 0) == 0 &&
                       u.find("/2026/10/01/ae261001") != std::string::npos);
        check_bool("2c: buildUrl host Kyoto",
                   u.find("wdc.kugi.kyoto-u.ac.jp") != std::string::npos);
    }

    std::printf("--- ae-parse: lineas rotas ---\n");
    {
        // (3) Linea con 59 valores -> descartada; el resto sigue valido.
        const std::vector<long> h0 = ramp(100, 60);
        const std::vector<long> h1 = ramp(200, 59);
        const std::vector<long> h2 = ramp(300, 60);
        const std::string body =
            rawLine("261001", 0, h0) + "\n" + rawLine("261001", 1, h1) + "\n" +
            rawLine("261001", 2, h2) + "\n";

        AeSnapshot s;
        check_bool("3: con 1 linea rota sigue valido",
                   AeAdapter::parseAeFile(body, 2026, 10, 1, s) && s.valid);
        check_bool("3b: quedan 120 muestras (2 horas)", s.samples.size() == 120);
        bool gap = true;
        for (const AeSample& x : s.samples)
            if (x.timeUtc >= kEpoch20261001T0100 && x.timeUtc < kEpoch20261001T0200) gap = false;
        check_bool("3c: la hora rota no aporta muestras", gap);
        check_bool("3d: descarte contado en note",
                   s.note.find("1 lineas descartadas") != std::string::npos);
        check_bool("3e: hora 2 intacta (300..359, 02:59)",
                   s.samples.back().ae == 359.0f &&
                       s.samples.back().timeUtc == kEpoch20261001T0259);

        // (4) Cabecera distinta -> false.
        AeSnapshot s4;
        std::string bad = rawLine("261001", 0, h0);
        bad.replace(0, 8, "XXALAOAU");
        check_bool("4: cabecera distinta -> false",
                   !AeAdapter::parseAeFile(bad, 2026, 10, 1, s4) && !s4.valid);

        // (5) Fichero vacio -> false.
        AeSnapshot s5;
        check_bool("5: fichero vacio -> false",
                   !AeAdapter::parseAeFile(std::string(), 2026, 10, 1, s5) && !s5.valid);
        AeSnapshot s5b;
        check_bool("5b: solo saltos de linea -> false",
                   !AeAdapter::parseAeFile("\n\n\r\n", 2026, 10, 1, s5b) && !s5b.valid);
    }

    std::printf("--- ae-parse: recorte a 1440 ---\n");
    {
        // (6) 25 horas (1500 valores) -> 1440; la primera es la hora 1 minuto 0.
        std::string body;
        for (int h = 0; h < 25; h++) body += rawLine("261001", h, ramp(h * 60, 60)) + "\n";

        AeSnapshot s;
        check_bool("6: 25 horas parsean", AeAdapter::parseAeFile(body, 2026, 10, 1, s));
        check_bool("6b: quedan 1440", s.samples.size() == 1440);
        check_bool("6c: primera = hora 1 minuto 0 (1790816400)",
                   s.samples.front().timeUtc == kEpoch20261001T0100);
        check_bool("6d: primera muestra es el valor 60 de la hora 1",
                   s.samples.front().ae == 60.0f);
        check_bool("6e: ultima = 24:59 (1790902740), valor 1499",
                   s.samples.back().timeUtc == kEpoch20261002T0059 &&
                       s.samples.back().ae == 1499.0f);
    }

    std::printf("--- ae-parse: formato real de Kyoto (61 campos) ---\n");
    {
        // (7) Una linea con la media horaria detras: 60 muestras, no 61.
        const std::vector<long> h0 = ramp(100, 60);
        const std::string line = realLine("261001", 0, h0);
        AeSnapshot s;
        check_bool("7: linea real (60 + media) parsea",
                   AeAdapter::parseAeFile(line + "\n", 2026, 10, 1, s) && s.valid);
        check_bool("7b: 60 muestras (la media NO es un minuto)", s.samples.size() == 60);
        check_bool("7c: ultima 00:59, no 01:00",
                   s.samples.back().timeUtc == kEpoch20261001T0059 &&
                       s.samples.back().ae == 159.0f);

        // (7d) Un dia completo real-shaped: 24 lineas x 61 campos -> 1440.
        std::string day;
        for (int h = 0; h < 24; h++) day += realLine("261011", h, ramp(h * 60, 60)) + "\n";
        AeSnapshot sd;
        check_bool("7d: dia real-shaped -> 1440 muestras",
                   AeAdapter::parseAeFile(day, 2026, 10, 11, sd) && sd.samples.size() == 1440);
        check_bool("7e: ultimo minuto 2026-10-11 23:59 (1791763140)",
                   sd.samples.back().timeUtc == kEpoch20261011T2359 &&
                       sd.timeUtc == kEpoch20261011T2359);

        // (7f) Mas de 61 campos no es el formato: linea descartada.
        std::vector<long> over = ramp(100, 62);
        AeSnapshot so;
        check_bool("7f: 62 campos -> descartada (sin muestras)",
                   !AeAdapter::parseAeFile(rawLine("261001", 0, over) + "\n", 2026, 10, 1, so) &&
                       !so.valid);
    }

    std::printf("--- ae-parse: cache en %TEMP%% ---\n");
    {
        const char* tmpEnv = std::getenv("TEMP");
        const std::string dir = std::string(tmpEnv != nullptr ? tmpEnv : ".") + "/opencode";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::string path = dir + "/ae_cache_roundtrip.txt";

        const std::string body =
            rawLine("261001", 0, ramp(100, 60)) + "\n" + rawLine("261001", 1, ramp(200, 60)) + "\n";
        const double ep = 1791234567.5;   // epoch con fraccion: debe volver exacto

        // (8) Escritura + lectura: cabecera "AE1 <epoch> <YYYY-MM-DD>\n".
        const bool wrote = AeAdapter::writeCacheFile(path, ep, "2026-10-01", body);
        std::string rbody, rday;
        double rep = 0.0;
        const bool read = AeAdapter::readCacheFile(path, rbody, rep, rday);
        check_bool("8: cache round-trip", wrote && read);
        check_bool("8b: epoch exacto", rep == ep);
        check_bool("8c: sourceDay del header", rday == "2026-10-01");
        check_bool("8d: cuerpo crudo identico", rbody == body);

        std::ifstream f(path, std::ios::binary);
        const std::string raw((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
        check_bool("8e: header literal 'AE1 1791234567.500 2026-10-01'",
                   raw.rfind("AE1 1791234567.500 2026-10-01\n", 0) == 0);

        AeSnapshot sc;
        check_bool("8f: el cuerpo del cache parsea",
                   AeAdapter::parseAeFile(rbody, 2026, 10, 1, sc) && sc.samples.size() == 120);

        // (8g) Header sin dia: se tolera (sourceDay vacio), epoch se conserva.
        const std::string path2 = dir + "/ae_cache_noday.txt";
        {
            std::ofstream o(path2, std::ios::binary | std::ios::trunc);
            o << "AE1 123.000\n" << body;
        }
        std::string b2, d2;
        double e2 = 0.0;
        check_bool("8g: header sin dia tolerado",
                   AeAdapter::readCacheFile(path2, b2, e2, d2) && d2.empty() && e2 == 123.0 &&
                       b2 == body);

        // (8h) Basura / vacio: readCacheFile dice false.
        const std::string path3 = dir + "/ae_cache_bad.txt";
        {
            std::ofstream o(path3, std::ios::binary | std::ios::trunc);
            o << "XX1 1.0\n" << body;
        }
        std::string b3, d3;
        double e3 = 0.0;
        check_bool("8h: cabecera desconocida -> false",
                   !AeAdapter::readCacheFile(path3, b3, e3, d3));
        check_bool("8i: escribir cuerpo vacio -> false",
                   !AeAdapter::writeCacheFile(dir + "/ae_cache_empty.txt", ep, "2026-10-01",
                                              std::string()));

        std::filesystem::remove(path, ec);
        std::filesystem::remove(path2, ec);
        std::filesystem::remove(path3, ec);
        std::filesystem::remove(dir + "/ae_cache_empty.txt", ec);
    }

    std::printf("[test_ae_parse] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
