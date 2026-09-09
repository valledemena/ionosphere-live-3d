// =============================================================================
// test_kc2g_parse — validacion del parser y time-filter de kc2g (M4R, FASE A
// sellada: failover-only) sobre fixture REAL capturado en vivo:
//   GET https://prop.kc2g.com/api/stations.json — 2026-09-09T16:51 UTC,
//   101 filas (giro 49 / noaa 46 / ingv 2 / aus-sws 2 /
//   giro_fastchar 2), con nulls frecuentes y filas NOAA congeladas
//   2023/2024 incluidas.
//
// Test de logica pura: sin GL-contexto, sin GLFW, sin red. Llama a
// PRODUCCION: Kc2gAdapter::parseStations / rowHasData / gridUsable /
// panelUsable / toSample (src/Data/Kc2gAdapter.cpp), no replica.
//
// SEGUNDA IMPLEMENTACION: TODAS las esperanzas numericas de las partes A-F
// son GENERADAS por scripts/m4a_gen_test.py (Python sobre el MISMO fixture
// — nada transcrito a mano; patron m4t/m8). El conteo de filas / md-null /
// source se re-verifica ademas con un escaneo independiente IN-TEST (conteo
// de ocurrencias de subcadenas — tecnica distinta).
//
// Reglas selladas verificadas (FASE A, sello de MUSE 2026-09-10):
//   - lon 0-360 -> -180..180 EN PARSE (nuestra convencion);
//   - md COMO CADENA ("" si null; numero -> "" sin coercion);
//   - time-filter: grids <= 6 h; panel <= 24 h (banda 6-24 h = stale);
//   - EG931 excluida de grids POR REGLA EXPLICITA (EG931-2024: feed NOAA
//     congelado desde 2024-08-22);
//   - sin B0/B1/fmin/fxI en kc2g -> 0 + sanitizeB0/B1 -> 100/3 (produccion
//     de LayerProfile.h, llamada aqui mismo);
//   - historia de 1 punto (sampleHistoryAt clampa al ultimo valor = replay
//     congelado pero honesto, marcado stale).
// =============================================================================
#include "Data/Kc2gAdapter.h"
#include "Ionosphere/LayerProfile.h"   // sanitizeB0/B1: cierre del sello FASE A

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

static void expectNear(const char* what, double got, double want, double tol) {
    const bool ok = std::fabs(got - want) <= tol;
    if (!ok) ++g_failures;
    std::printf("  %-52s got %14.5f  want %14.5f  tol %7.1e  [%s]\n",
                what, got, want, tol, ok ? "OK" : "FAIL");
}

static void expectTrue(const char* what, bool cond) {
    if (!cond) ++g_failures;
    std::printf("  %-52s [%s]\n", what, cond ? "OK" : "FAIL");
}

static void expectStr(const char* what, const std::string& got, const std::string& want) {
    const bool ok = got == want;
    if (!ok) ++g_failures;
    std::printf("  %-52s got %-14s want %-14s [%s]\n",
                what, got.c_str(), want.c_str(), ok ? "OK" : "FAIL");
}

static const Kc2gRow* findByCode(const std::vector<Kc2gRow>& rows, const char* code) {
    for (const auto& r : rows) if (r.code == code) return &r;
    return nullptr;
}

// 3a via independiente IN-TEST: conteo de ocurrencias de subcadenas.
static size_t countOccur(const std::string& hay, const char* needle) {
    size_t n = 0, pos = 0;
    const std::string nd(needle);
    while ((pos = hay.find(nd, pos)) != std::string::npos) { n++; pos += nd.size(); }
    return n;
}

// Fixture real completo (ver cabecera). El "now" del analisis es el tiempo
// MAXIMO de fila del propio fixture: determinista, sin reloj.
const char* kFixture = R"json([{"he": 90.0, "foes": 3.6, "yf1": null, "time": "2026-03-19T22:10:05", "fof1": null, "yf2": 63.001, "mufd": 28.827, "station": {"code": "AU930", "longitude": "262.3", "name": "Austin, TX, USA", "latitude": "30.4", "id": 1}, "hmf2": 241.8, "fbes": null, "hf2": null, "foe": 3.32, "id": 22008538, "hmf1": null, "cs": 100.0, "hme": 106.1, "md": "3.352", "fof2": 8.6, "scalef2": 38.14, "tec": 15.837, "source": "giro"}, {"he": 92.855, "foes": 2.75, "yf1": 40.256, "time": "2026-09-09T16:45:01", "fof1": 4.3, "yf2": 109.91, "mufd": 26.48, "station": {"code": "EA036", "longitude": "353.3", "name": "El Arenosillo, Spain", "latitude": "37.1", "id": 2}, "hmf2": 278.921, "fbes": null, "hf2": 262.5, "foe": 2.8, "id": 23126207, "hmf1": 170.144, "cs": 95.0, "hme": 97.44, "md": "3.171", "fof2": 8.35, "scalef2": 62.371, "tec": null, "source": "giro"}, {"he": 91.345, "foes": 2.9, "yf1": 28.813, "time": "2026-09-09T16:45:01", "fof1": 3.85, "yf2": 72.858, "mufd": 25.48, "station": {"code": "EB040", "longitude": "0.5", "name": "Roquetes, Spain", "latitude": "40.8", "id": 3}, "hmf2": 254.585, "fbes": null, "hf2": 245.0, "foe": 2.5, "id": 23126206, "hmf1": 174.875, "cs": 60.0, "hme": 105.957, "md": "3.288", "fof2": 7.75, "scalef2": 41.884, "tec": null, "source": "giro"}, {"he": null, "foes": 2.1, "yf1": null, "time": "2026-09-09T16:45:00", "fof1": null, "yf2": 52.331, "mufd": 23.733, "station": {"code": "AT138", "longitude": "23.5", "name": "Athens, Greece", "latitude": "38", "id": 4}, "hmf2": 247.685, "fbes": null, "hf2": 220.0, "foe": null, "id": 23126209, "hmf1": null, "cs": 65.0, "hme": 110.0, "md": "3.477", "fof2": 6.825, "scalef2": 28.047, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2021-08-22T23:00:01", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "BP440", "longitude": "116.2", "name": "Beijing, China", "latitude": "40.3", "id": 5}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6792904, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 5.0, "scalef2": null, "tec": null, "source": "noaa"}, {"he": 100.478, "foes": 3.375, "yf1": 62.517, "time": "2026-09-09T16:37:30", "fof1": 4.825, "yf2": 59.528, "mufd": 21.18, "station": {"code": "MHJ45", "longitude": "288.5", "name": "Millstone Hill, MA, USA", "latitude": "42.6", "id": 6}, "hmf2": 228.381, "fbes": null, "hf2": 305.0, "foe": 3.45, "id": 23126197, "hmf1": 166.332, "cs": 70.0, "hme": 102.181, "md": "3.284", "fof2": 6.45, "scalef2": 50.095, "tec": null, "source": "giro"}, {"he": 122.931, "foes": 1.95, "yf1": null, "time": "2026-09-09T16:45:00", "fof1": null, "yf2": 66.776, "mufd": 22.223, "station": {"code": "PQ052", "longitude": "14.6", "name": "Pruhonice, Czech Republic", "latitude": "50", "id": 7}, "hmf2": 261.874, "fbes": null, "hf2": 242.5, "foe": 1.85, "id": 23126208, "hmf1": null, "cs": 100.0, "hme": 101.776, "md": "3.244", "fof2": 6.85, "scalef2": 36.719, "tec": null, "source": "giro"}, {"he": 110.3, "foes": 2.05, "yf1": null, "time": "2026-09-09T16:43:16", "fof1": null, "yf2": 67.454, "mufd": 22.405, "station": {"code": "JR055", "longitude": "13.4", "name": "Juliusruh, Germany", "latitude": "54.6", "id": 10}, "hmf2": 245.278, "fbes": null, "hf2": 230.0, "foe": 1.96, "id": 23126211, "hmf1": null, "cs": 100.0, "hme": 101.508, "md": "3.369", "fof2": 6.65, "scalef2": 37.548, "tec": null, "source": "giro"}, {"he": 105.0, "foes": 3.3, "yf1": null, "time": "2024-04-16T16:15:05", "fof1": null, "yf2": 83.558, "mufd": 17.415, "station": {"code": "BC840", "longitude": "254.7", "name": "Boulder, CO, USA", "latitude": "40", "id": 11}, "hmf2": 181.2, "fbes": null, "hf2": null, "foe": 3.31, "id": 17073395, "hmf1": null, "cs": 100.0, "hme": 99.2, "md": "3.591", "fof2": 4.85, "scalef2": 46.785, "tec": 5.829, "source": "giro"}, {"he": 102.232, "foes": 2.55, "yf1": null, "time": "2026-09-09T16:45:01", "fof1": null, "yf2": 94.914, "mufd": 20.975, "station": {"code": "DB049", "longitude": "4.6", "name": "Dourbes, Belgium", "latitude": "50.1", "id": 12}, "hmf2": 273.66, "fbes": null, "hf2": 232.5, "foe": 2.3, "id": 23126205, "hmf1": null, "cs": 95.0, "hme": 101.764, "md": "3.085", "fof2": 6.8, "scalef2": 52.984, "tec": null, "source": "giro"}, {"he": 107.894, "foes": null, "yf1": 56.915, "time": "2026-09-09T15:50:00", "fof1": 4.723, "yf2": 73.13, "mufd": 28.258, "station": {"code": "BVJ03", "longitude": "299.3", "name": "Boa Vista, Brazil", "latitude": "2.8", "id": 13}, "hmf2": 292.706, "fbes": null, "hf2": 268.0, "foe": 3.71, "id": 23126055, "hmf1": 143.269, "cs": 75.0, "hme": 102.073, "md": "2.944", "fof2": 9.625, "scalef2": 51.594, "tec": 26.908, "source": "giro"}, {"he": 93.553, "foes": 3.4, "yf1": null, "time": "2026-09-09T16:40:00", "fof1": null, "yf2": 49.118, "mufd": 31.739, "station": {"code": "CAJ2M", "longitude": "315", "name": "Cachoeira Paulista, Brazil", "latitude": "-22.7", "id": 14}, "hmf2": 243.679, "fbes": null, "hf2": 198.0, "foe": 3.46, "id": 23126212, "hmf1": null, "cs": 85.0, "hme": 106.614, "md": "3.431", "fof2": 9.25, "scalef2": 32.45, "tec": null, "source": "giro"}, {"he": null, "foes": 3.95, "yf1": null, "time": "2025-04-10T02:40:00", "fof1": null, "yf2": 48.0, "mufd": 48.177, "station": {"code": "FZA0M", "longitude": "321.6", "name": "Fortaleza, Brazil", "latitude": "-3.9", "id": 15}, "hmf2": 280.745, "fbes": null, "hf2": 231.0, "foe": null, "id": 19665191, "hmf1": null, "cs": 85.0, "hme": 110.0, "md": "3.357", "fof2": 14.35, "scalef2": 24.769, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2026-05-21T14:37:30", "fof1": null, "yf2": 77.3, "mufd": 18.591, "station": {"code": "IC437", "longitude": "127.54", "name": "I-Cheon, South Korea", "latitude": "37.14", "id": 16}, "hmf2": 350.343, "fbes": null, "hf2": 280.0, "foe": null, "id": 22389182, "hmf1": null, "cs": 75.0, "hme": 110.0, "md": "2.817", "fof2": 6.6, "scalef2": 40.224, "tec": null, "source": "giro"}, {"he": null, "foes": 2.325, "yf1": null, "time": "2026-05-21T14:45:00", "fof1": null, "yf2": 67.528, "mufd": 19.488, "station": {"code": "JJ433", "longitude": "126.3", "name": "Jeju Island, Korea", "latitude": "33.43", "id": 17}, "hmf2": 348.033, "fbes": null, "hf2": 285.0, "foe": null, "id": 22389203, "hmf1": null, "cs": 85.0, "hme": 110.0, "md": "2.845", "fof2": 6.85, "scalef2": 34.614, "tec": null, "source": "giro"}, {"he": null, "foes": 2.1, "yf1": null, "time": "2026-09-09T16:40:00", "fof1": null, "yf2": 42.347, "mufd": 26.75, "station": {"code": "NI135", "longitude": "33.16", "name": "Nicosia, Cyprus", "latitude": "35.03", "id": 18}, "hmf2": 263.766, "fbes": null, "hf2": 222.5, "foe": null, "id": 23126215, "hmf1": null, "cs": 85.0, "hme": 110.0, "md": "3.386", "fof2": 7.9, "scalef2": 21.734, "tec": null, "source": "giro"}, {"he": 102.374, "foes": null, "yf1": 40.705, "time": "2022-03-26T09:01:00", "fof1": 4.53, "yf2": 69.394, "mufd": 24.81, "station": {"code": "MO155", "longitude": "37.3", "name": "Moscow, Russia", "latitude": "55.47", "id": 19}, "hmf2": 240.976, "fbes": null, "hf2": 260.0, "foe": 2.947, "id": 8743385, "hmf1": 168.641, "cs": 75.0, "hme": 100.455, "md": "3.328", "fof2": 7.472, "scalef2": 43.982, "tec": 13.334, "source": "noaa"}, {"he": 98.139, "foes": 3.725, "yf1": null, "time": "2024-08-22T19:07:30", "fof1": null, "yf2": 102.632, "mufd": 22.181, "station": {"code": "AL945", "longitude": "276.44", "name": "Alpena MI, USA", "latitude": "45.07", "id": 20}, "hmf2": 261.105, "fbes": null, "hf2": 204.0, "foe": 3.625, "id": 18087059, "hmf1": null, "cs": 75.0, "hme": 95.902, "md": "2.853", "fof2": 7.775, "scalef2": 62.078, "tec": 20.264, "source": "noaa"}, {"he": 114.371, "foes": null, "yf1": null, "time": "2024-08-22T19:00:00", "fof1": null, "yf2": null, "mufd": 21.54, "station": {"code": "RL052", "longitude": "359.4", "name": "Chilton, United Kingdom", "latitude": "51.5", "id": 21}, "hmf2": null, "fbes": null, "hf2": 255.0, "foe": 1.7, "id": 18087047, "hmf1": null, "cs": 0.0, "hme": null, "md": "3.013", "fof2": 7.15, "scalef2": null, "tec": null, "source": "noaa"}, {"he": 96.432, "foes": null, "yf1": 51.379, "time": "2024-08-22T19:07:30", "fof1": 5.275, "yf2": 85.055, "mufd": 29.613, "station": {"code": "EG931", "longitude": "273.5", "name": "Eglin AFB, FL, USA", "latitude": "30.5", "id": 22}, "hmf2": 289.799, "fbes": null, "hf2": 283.071, "foe": 3.375, "id": 18087063, "hmf1": 154.585, "cs": 100.0, "hme": 93.699, "md": "2.841", "fof2": 10.438, "scalef2": 55.744, "tec": 33.384, "source": "noaa"}, {"he": 102.711, "foes": 3.475, "yf1": 102.571, "time": "2024-08-22T19:07:30", "fof1": 5.05, "yf2": 115.149, "mufd": 17.476, "station": {"code": "EI764", "longitude": "212.93", "name": "Eielson AFB AK, USA", "latitude": "64.66", "id": 23}, "hmf2": 298.974, "fbes": null, "hf2": 495.0, "foe": 3.175, "id": 18087060, "hmf1": 204.712, "cs": 75.0, "hme": 98.182, "md": "2.937", "fof2": 5.963, "scalef2": 100.315, "tec": 18.497, "source": "noaa"}, {"he": 96.166, "foes": 3.6, "yf1": 29.633, "time": "2026-09-09T12:37:30", "fof1": 4.25, "yf2": 61.699, "mufd": 24.235, "station": {"code": "FF051", "longitude": "358.5", "name": "Fairford, England", "latitude": "51.7", "id": 24}, "hmf2": 218.199, "fbes": null, "hf2": 270.0, "foe": 3.275, "id": 23126202, "hmf1": 139.685, "cs": 95.0, "hme": 99.505, "md": "3.438", "fof2": 7.05, "scalef2": 42.878, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2026-09-09T16:40:00", "fof1": null, "yf2": 60.403, "mufd": 18.327, "station": {"code": "GR13L", "longitude": "26.5", "name": "Grahamstown, South Africa", "latitude": "-33.3", "id": 25}, "hmf2": 242.572, "fbes": null, "hf2": 192.5, "foe": null, "id": 23126213, "hmf1": null, "cs": 95.0, "hme": 110.0, "md": "3.458", "fof2": 5.3, "scalef2": 31.7, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2026-09-09T16:40:00", "fof1": null, "yf2": 67.298, "mufd": 22.116, "station": {"code": "HE13N", "longitude": "19.22", "name": "Hermanus, South Africa", "latitude": "-34.42", "id": 26}, "hmf2": 251.124, "fbes": null, "hf2": 202.5, "foe": null, "id": 23126193, "hmf1": null, "cs": 70.0, "hme": 110.0, "md": "3.510", "fof2": 6.3, "scalef2": 36.98, "tec": null, "source": "giro"}, {"he": 97.0, "foes": null, "yf1": 61.886, "time": "2026-09-09T16:30:00", "fof1": 4.41, "yf2": 60.534, "mufd": 21.292, "station": {"code": "IF843", "longitude": "247.32", "name": "Idaho Natl Lab, ID, USA", "latitude": "43.81", "id": 27}, "hmf2": 226.025, "fbes": null, "hf2": 270.0, "foe": 3.155, "id": 23126162, "hmf1": 170.199, "cs": 70.0, "hme": 103.655, "md": "3.490", "fof2": 6.113, "scalef2": 44.878, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2024-06-07T12:15:00", "fof1": null, "yf2": 29.632, "mufd": 14.285, "station": {"code": "LM42B", "longitude": "114.1", "name": "Learmonth, Australia", "latitude": "-21.8", "id": 28}, "hmf2": 285.576, "fbes": null, "hf2": 281.5, "foe": null, "id": 17837971, "hmf1": null, "cs": 75.0, "hme": null, "md": "3.401", "fof2": 4.2, "scalef2": 14.673, "tec": 1.445, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2026-01-22T18:25:00", "fof1": null, "yf2": 26.25, "mufd": 19.719, "station": {"code": "LV12P", "longitude": "21.2", "name": "Louisvale, South Africa", "latitude": "-28.5", "id": 29}, "hmf2": 248.268, "fbes": null, "hf2": 267.5, "foe": null, "id": 21629679, "hmf1": null, "cs": 70.0, "hme": 110.0, "md": "3.703", "fof2": 5.325, "scalef2": 13.246, "tec": null, "source": "giro"}, {"he": 97.903, "foes": 4.2, "yf1": 75.194, "time": "2024-08-22T19:07:30", "fof1": 6.35, "yf2": 107.427, "mufd": 27.963, "station": {"code": "PA836", "longitude": "239.5", "name": "Pt Arguello, CA, USA", "latitude": "34.8", "id": 31}, "hmf2": 312.088, "fbes": null, "hf2": 353.0, "foe": 3.6, "id": 18087064, "hmf1": 199.362, "cs": 100.0, "hme": 95.287, "md": "2.769", "fof2": 10.113, "scalef2": 70.311, "tec": 38.787, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2026-09-09T16:15:00", "fof1": null, "yf2": null, "mufd": 23.43, "station": {"code": "RM041", "longitude": "12.5", "name": "Rome, Italy", "latitude": "41.8", "id": 32}, "hmf2": 258.0, "fbes": null, "hf2": null, "foe": 2.5, "id": 23126149, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.3", "fof2": 7.1, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2024-08-22T19:07:30", "fof1": null, "yf2": null, "mufd": 23.643, "station": {"code": "VT139", "longitude": "17.8", "name": "San Vito, Italy", "latitude": "40.6", "id": 33}, "hmf2": null, "fbes": null, "hf2": 241.0, "foe": null, "id": 18087062, "hmf1": null, "cs": 0.0, "hme": null, "md": "2.857", "fof2": 8.275, "scalef2": null, "tec": null, "source": "noaa"}, {"he": 93.549, "foes": 1.95, "yf1": null, "time": "2026-09-09T16:37:30", "fof1": null, "yf2": 42.241, "mufd": 40.947, "station": {"code": "TR169", "longitude": "19.2", "name": "Tromso, Norway", "latitude": "69.6", "id": 35}, "hmf2": 274.872, "fbes": null, "hf2": 315.0, "foe": 1.86, "id": 23126198, "hmf1": null, "cs": 45.0, "hme": 93.555, "md": "3.500", "fof2": 11.7, "scalef2": 21.643, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2026-09-09T16:30:00", "fof1": 3.2, "yf2": null, "mufd": 21.9, "station": {"code": "MZ152", "longitude": "21.1", "name": "Warsaw, Poland", "latitude": "52.2", "id": 36}, "hmf2": null, "fbes": null, "hf2": null, "foe": 2.2, "id": 23126216, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.0", "fof2": 7.3, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2026-09-09T16:30:00", "fof1": null, "yf2": null, "mufd": 12.861, "station": {"code": "NI63_", "longitude": "167.97", "name": "Norfolk, Australia", "latitude": "-29.03", "id": 37}, "hmf2": 321.993, "fbes": null, "hf2": null, "foe": null, "id": 23126171, "hmf1": null, "cs": -1.0, "hme": null, "md": "2.91", "fof2": 4.42, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2026-09-09T16:30:00", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "CB53N", "longitude": "149", "name": "Canberra, Australia", "latitude": "-35.32", "id": 38}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 23126172, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 4.19, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2020-12-30T12:00:00", "fof1": null, "yf2": null, "mufd": 18.4018612158635, "station": {"code": "CN53L", "longitude": "150.67", "name": "Camden, Australia", "latitude": "-34.05", "id": 39}, "hmf2": 374.129, "fbes": null, "hf2": null, "foe": null, "id": 4772340, "hmf1": null, "cs": -1.0, "hme": null, "md": "2.72257156618783", "fof2": 6.759, "scalef2": null, "tec": null, "source": "noaa"}, {"he": 85.0, "foes": 2.0, "yf1": null, "time": "2017-01-21T23:00:00", "fof1": null, "yf2": 99.647, "mufd": 22.361, "station": {"code": "PSJ5J", "longitude": "302.1", "name": "Port Stanley, Falkland Islands", "latitude": "-51.6", "id": 40}, "hmf2": 292.1, "fbes": null, "hf2": null, "foe": 2.01, "id": 9147553, "hmf1": null, "cs": 50.0, "hme": 99.7, "md": "3.022", "fof2": 7.4, "scalef2": 55.469, "tec": 16.311, "source": "noaa"}, {"he": null, "foes": 1.875, "yf1": null, "time": "2026-09-09T16:30:00", "fof1": null, "yf2": null, "mufd": 11.76, "station": {"code": "PE43K", "longitude": "116.13", "name": "Perth, Australia", "latitude": "-32", "id": 41}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 23126166, "hmf1": null, "cs": -1.0, "hme": null, "md": "2.977", "fof2": 3.95, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": 2.7, "yf1": null, "time": "2026-09-09T16:25:00", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "DW41K", "longitude": "130.95", "name": "Darwin, Australia", "latitude": "-12.45", "id": 42}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 23126175, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": null, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2026-04-20T06:45:00", "fof1": 5.615, "yf2": null, "mufd": 40.724, "station": {"code": "CS31K", "longitude": "96.83", "name": "Cocos Island", "latitude": "-12.18", "id": 43}, "hmf2": 293.541, "fbes": null, "hf2": 273.2, "foe": null, "id": 22181973, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.169", "fof2": 12.852, "scalef2": null, "tec": null, "source": "giro"}, {"he": 89.6, "foes": 4.105, "yf1": null, "time": "2026-07-20T23:20:00", "fof1": null, "yf2": null, "mufd": 26.1697455750832, "station": {"code": "BR52P", "longitude": "153.06", "name": "Brisbane, Australia", "latitude": "-27.06", "id": 44}, "hmf2": 242.429, "fbes": 3.815, "hf2": null, "foe": 3.412, "id": 22787468, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.41463277336681", "fof2": 7.664, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2026-09-09T16:20:00", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "HO54K", "longitude": "147.32", "name": "Hobart, Australia", "latitude": "-42.92", "id": 45}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 23126181, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 2.9, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": 2.2, "yf1": null, "time": "2026-09-09T16:25:00", "fof1": null, "yf2": null, "mufd": 14.7584148747925, "station": {"code": "TV51R", "longitude": "146.85", "name": "Townsville, Australia", "latitude": "-19.63", "id": 46}, "hmf2": 303.873, "fbes": null, "hf2": null, "foe": null, "id": 23126173, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.04548387841364", "fof2": 4.846, "scalef2": null, "tec": null, "source": "giro"}, {"he": 80.077, "foes": null, "yf1": null, "time": "2026-05-28T18:20:00", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "SAA0K", "longitude": "315.8", "name": "Sao Luis, Brazil", "latitude": "-2.6", "id": 47}, "hmf2": null, "fbes": null, "hf2": null, "foe": 1.36, "id": 22434557, "hmf1": null, "cs": 75.0, "hme": 110.0, "md": null, "fof2": null, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2016-06-01T23:45:00", "fof1": null, "yf2": 86.236, "mufd": 8.486, "station": {"code": "MU12K", "longitude": "30.88", "name": "Madimbo, South Africa", "latitude": "-22.39", "id": 48}, "hmf2": 317.014, "fbes": null, "hf2": 260.5, "foe": null, "id": 9133846, "hmf1": null, "cs": 100.0, "hme": null, "md": "3.058", "fof2": 2.775, "scalef2": 47.889, "tec": 1.864, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2026-09-09T12:22:30", "fof1": null, "yf2": 12.42, "mufd": 16.678, "station": {"code": "GU513", "longitude": "144.86", "name": "Guam, USA", "latitude": "13.62", "id": 49}, "hmf2": 236.484, "fbes": null, "hf2": 242.5, "foe": null, "id": 23126186, "hmf1": null, "cs": 45.0, "hme": 110.0, "md": "4.043", "fof2": 4.125, "scalef2": 10.583, "tec": null, "source": "giro"}, {"he": 96.897, "foes": null, "yf1": 41.397, "time": "2026-05-25T13:30:00", "fof1": 4.773, "yf2": 61.097, "mufd": 30.919, "station": {"code": "CGK21", "longitude": "305", "name": "Campo Grande, Brazil", "latitude": "-20.5", "id": 50}, "hmf2": 231.558, "fbes": null, "hf2": 243.75, "foe": 3.16, "id": 22414388, "hmf1": 165.363, "cs": 100.0, "hme": 102.268, "md": "3.595", "fof2": 8.613, "scalef2": 37.415, "tec": 15.118, "source": "giro"}, {"he": 89.833, "foes": 3.1, "yf1": null, "time": "2026-09-09T16:40:00", "fof1": null, "yf2": 66.265, "mufd": 25.956, "station": {"code": "GM037", "longitude": "14", "name": "Gibilmanna, Italy", "latitude": "37.9", "id": 51}, "hmf2": 258.397, "fbes": 2.4, "hf2": 240.0, "foe": 1.96, "id": 23126214, "hmf1": null, "cs": 95.0, "hme": 89.694, "md": "3.307", "fof2": 7.85, "scalef2": 36.335, "tec": null, "source": "giro"}, {"he": 115.0, "foes": 3.8, "yf1": null, "time": "2022-06-14T17:40:00", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "JI91J", "longitude": "283.2", "name": "Jicamarca, Peru", "latitude": "-12", "id": 52}, "hmf2": null, "fbes": null, "hf2": null, "foe": 3.52, "id": 9582037, "hmf1": null, "cs": 0.0, "hme": 115.9, "md": null, "fof2": null, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": 2.025, "yf1": null, "time": "2026-09-09T16:25:00", "fof1": null, "yf2": null, "mufd": 8.483, "station": {"code": "ND61R", "longitude": "190.07", "name": "Niue Island", "latitude": "-19.07", "id": 54}, "hmf2": 370.68, "fbes": null, "hf2": null, "foe": null, "id": 23126176, "hmf1": null, "cs": -1.0, "hme": null, "md": "2.768", "fof2": 3.065, "scalef2": null, "tec": null, "source": "giro"}, {"he": 115.0, "foes": 6.05, "yf1": null, "time": "2022-03-22T18:15:05", "fof1": null, "yf2": 61.359, "mufd": 40.892, "station": {"code": "PRJ18", "longitude": "292.9", "name": "Ramey, Puerto Rico", "latitude": "18.5", "id": 56}, "hmf2": 266.6, "fbes": null, "hf2": null, "foe": 3.41, "id": 8671980, "hmf1": null, "cs": 0.0, "hme": 101.0, "md": "3.258", "fof2": 12.55, "scalef2": 37.024, "tec": 33.078, "source": "noaa"}, {"he": null, "foes": 1.425, "yf1": null, "time": "2026-09-09T16:37:40", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "GA762", "longitude": "215", "name": "Gakona, AK, USA", "latitude": "62.38", "id": 57}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 23126196, "hmf1": null, "cs": 75.0, "hme": null, "md": null, "fof2": null, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2023-11-15T13:40:00", "fof1": null, "yf2": 88.71, "mufd": 29.74, "station": {"code": "SMK29", "longitude": "306.29", "name": "Santa Maria, Brazil", "latitude": "-29.73", "id": 58}, "hmf2": 294.931, "fbes": null, "hf2": 342.5, "foe": null, "id": 15445351, "hmf1": null, "cs": 90.0, "hme": 110.0, "md": "2.916", "fof2": 10.2, "scalef2": 54.745, "tec": null, "source": "giro"}, {"he": 94.083, "foes": 3.475, "yf1": null, "time": "2024-08-22T19:07:30", "fof1": null, "yf2": 100.337, "mufd": 26.839, "station": {"code": "LL721", "longitude": "201.85", "name": "Lualualei, Hawaii", "latitude": "21.43", "id": 59}, "hmf2": 261.697, "fbes": null, "hf2": 217.5, "foe": 3.38, "id": 18087054, "hmf1": null, "cs": 100.0, "hme": 96.947, "md": "2.982", "fof2": 9.0, "scalef2": 56.682, "tec": 24.796, "source": "noaa"}, {"he": 115.886, "foes": 3.6, "yf1": null, "time": "2025-12-30T12:40:00", "fof1": null, "yf2": 132.004, "mufd": 29.438, "station": {"code": "BLJ03", "longitude": "311.56", "name": "Belem, Brazil", "latitude": "1.43", "id": 60}, "hmf2": 340.685, "fbes": null, "hf2": 207.5, "foe": 3.505, "id": 21470625, "hmf1": null, "cs": 70.0, "hme": 103.08, "md": "2.634", "fof2": 11.175, "scalef2": 73.552, "tec": 49.625, "source": "giro"}, {"he": 85.0, "foes": 4.15, "yf1": 74.154, "time": "2024-06-24T18:45:08", "fof1": 5.622, "yf2": 118.049, "mufd": 25.858, "station": {"code": "WP937", "longitude": "284.5", "name": "Wallops Island, VA, USA", "latitude": "37.9", "id": 61}, "hmf2": 374.8, "fbes": null, "hf2": 328.0, "foe": 4.01, "id": 17520745, "hmf1": 179.2, "cs": 75.0, "hme": 94.5, "md": "2.372", "fof2": 10.925, "scalef2": 78.314, "tec": 51.811, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2024-08-22T19:07:30", "fof1": null, "yf2": 85.394, "mufd": 36.684, "station": {"code": "AS00Q", "longitude": "345.6", "name": "Ascension Island", "latitude": "-7.95", "id": 62}, "hmf2": 317.805, "fbes": null, "hf2": 241.0, "foe": null, "id": 18087079, "hmf1": null, "cs": 100.0, "hme": null, "md": "2.917", "fof2": 12.575, "scalef2": 43.819, "tec": 36.195, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2024-08-22T19:24:48", "fof1": null, "yf2": 185.642, "mufd": 10.8406097819461, "station": {"code": "JB57N", "longitude": "164.24", "name": "Jang Bogo, Antartica", "latitude": "-74.62", "id": 64}, "hmf2": 467.16, "fbes": null, "hf2": null, "foe": null, "id": 18087179, "hmf1": null, "cs": -1.0, "hme": null, "md": "2.40368287848029", "fof2": 4.51, "scalef2": 92.821, "tec": 9.741, "source": "noaa"}, {"he": 122.7, "foes": 1.9, "yf1": null, "time": "2026-09-09T16:45:00", "fof1": null, "yf2": 69.906, "mufd": 21.523, "station": {"code": "SO148", "longitude": "16.72", "name": "Sopron, Hungary", "latitude": "47.63", "id": 66}, "hmf2": 269.859, "fbes": null, "hf2": 232.5, "foe": 1.8, "id": 23126210, "hmf1": null, "cs": 95.0, "hme": 104.877, "md": "3.189", "fof2": 6.75, "scalef2": 38.64, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2024-08-22T19:26:00", "fof1": null, "yf2": 159.829, "mufd": 17.7060493862877, "station": {"code": "TR170", "longitude": "19.22", "name": "EISCAT Tromso, Norway", "latitude": "69.58", "id": 67}, "hmf2": 326.255, "fbes": null, "hf2": null, "foe": 3.066, "id": 18087177, "hmf1": null, "cs": -1.0, "hme": 141.612, "md": "2.93292187945795", "fof2": 6.037, "scalef2": 79.915, "tec": 15.794, "source": "noaa"}, {"he": 97.118, "foes": 3.45, "yf1": 81.341, "time": "2024-08-22T19:07:30", "fof1": 4.275, "yf2": 117.541, "mufd": 16.269, "station": {"code": "EA653", "longitude": "185.92", "name": "Eareckson Air Station", "latitude": "52.73", "id": 68}, "hmf2": 304.91, "fbes": 3.35, "hf2": 384.0, "foe": 2.95, "id": 18087061, "hmf1": 204.968, "cs": 75.0, "hme": 97.368, "md": "2.793", "fof2": 5.832, "scalef2": 81.517, "tec": 14.81, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2026-09-09T16:30:00", "fof1": null, "yf2": 93.669, "mufd": 26.103, "station": {"code": "RO041", "longitude": "12.5", "name": "Rome, Italy RO041", "latitude": "41.8", "id": 69}, "hmf2": 281.627, "fbes": null, "hf2": 460.0, "foe": null, "id": 23126168, "hmf1": null, "cs": 45.0, "hme": 110.0, "md": "2.807", "fof2": 9.3, "scalef2": 49.784, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2020-07-29T23:04:02", "fof1": null, "yf2": 10.92, "mufd": null, "station": {"code": "SJJ18", "longitude": "293.75", "name": "San Juan, Puerto Rico", "latitude": "18.11", "id": 70}, "hmf2": 276.891, "fbes": null, "hf2": null, "foe": null, "id": 3342971, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 8.021, "scalef2": 5.46, "tec": 3.291, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2021-08-23T15:00:01", "fof1": null, "yf2": null, "mufd": 3.2, "station": {"code": "DI373", "longitude": "80.4", "name": "Dikson", "latitude": "73.5", "id": 71}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6798643, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 4.4, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2026-09-09T16:30:00", "fof1": 5.4, "yf2": null, "mufd": 31.9, "station": {"code": "BBJ3R", "longitude": "297.7", "name": "Bahia Blanca, Argentina", "latitude": "-38.7", "id": 72}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 23126218, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.32", "fof2": 9.6, "scalef2": null, "tec": null, "source": "ingv"}, {"he": null, "foes": null, "yf1": null, "time": "2024-11-18T20:45:00", "fof1": null, "yf2": null, "mufd": 10.4, "station": {"code": "RA041", "longitude": "12.5", "name": "Rome, Italy RA041", "latitude": "41.8", "id": 73}, "hmf2": 323.0, "fbes": null, "hf2": null, "foe": 0.6, "id": 18721366, "hmf1": null, "cs": -1.0, "hme": 110.0, "md": "2.89", "fof2": 3.6, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2021-12-06T22:00:01", "fof1": null, "yf2": 70.782, "mufd": 15.5452144697038, "station": {"code": "WI937", "longitude": "284.422", "name": "Wallops Island, VA, USA WI937", "latitude": "37.936", "id": 74}, "hmf2": 232.338, "fbes": null, "hf2": null, "foe": null, "id": 7662673, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.48547409634614", "fof2": 4.46, "scalef2": 35.391, "tec": 3.572, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2021-08-22T23:00:01", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "09429", "longitude": "106.4", "name": "Chongqing, China", "latitude": "29.5", "id": 75}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6792861, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 4.2, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": 3.861, "yf1": null, "time": "2023-05-20T11:55:00", "fof1": null, "yf2": null, "mufd": 10.632816507963, "station": {"code": "YG431", "longitude": "130.6", "name": "Yamagawa, Japan", "latitude": "31.2", "id": 78}, "hmf2": 213.262, "fbes": null, "hf2": 189.5, "foe": null, "id": 13671295, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.62894761363926", "fof2": 2.93, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": 5.2, "yf1": null, "time": "2022-06-16T08:15:00", "fof1": null, "yf2": null, "mufd": 17.5, "station": {"code": "MA155", "longitude": "37.3", "name": "Moscow, Russia MA155", "latitude": "55.5", "id": 80}, "hmf2": 275.0, "fbes": null, "hf2": null, "foe": 3.3, "id": 9598970, "hmf1": null, "cs": -1.0, "hme": 110.0, "md": "2.97", "fof2": 5.9, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": 3.539, "yf1": null, "time": "2023-05-20T13:40:00", "fof1": null, "yf2": null, "mufd": 18.363039170792, "station": {"code": "TO536", "longitude": "139.5", "name": "Kokubunji, Japan", "latitude": "35.7", "id": 82}, "hmf2": 300.458, "fbes": null, "hf2": 263.5, "foe": null, "id": 13672059, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.06357009856389", "fof2": 5.994, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2026-02-03T23:35:00", "fof1": null, "yf2": null, "mufd": 11.219, "station": {"code": "MW26P", "longitude": "62.88", "name": "Mawson Station, Antarctica", "latitude": "-67.6", "id": 87}, "hmf2": 352.362, "fbes": null, "hf2": 335.84, "foe": null, "id": 21713342, "hmf1": null, "cs": -1.0, "hme": null, "md": "2.873", "fof2": 3.905, "scalef2": null, "tec": null, "source": "aus-sws"}, {"he": null, "foes": null, "yf1": null, "time": "2023-08-25T09:00:00", "fof1": null, "yf2": null, "mufd": 18.6897041129771, "station": {"code": "CW46O", "longitude": "110.53", "name": "Casey, Antarctica", "latitude": "-66.28", "id": 88}, "hmf2": 399.984, "fbes": null, "hf2": null, "foe": null, "id": 14666841, "hmf1": null, "cs": -1.0, "hme": null, "md": "2.62348457509504", "fof2": 7.124, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2022-02-12T22:00:00", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "SQ67Q", "longitude": "166.78", "name": "Scott Base, Antarctica", "latitude": "-77.85", "id": 91}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 8271099, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 4.044, "scalef2": null, "tec": null, "source": "noaa"}, {"he": 95.316, "foes": null, "yf1": null, "time": "2023-05-20T12:30:00", "fof1": 4.2, "yf2": null, "mufd": 10.8843485571408, "station": {"code": "SO166", "longitude": "26.6", "name": "Sodankyla, Finland", "latitude": "67.4", "id": 92}, "hmf2": 470.943, "fbes": null, "hf2": 410.934, "foe": 3.45, "id": 13671548, "hmf1": 210.508, "cs": -1.0, "hme": 109.235, "md": "2.3926903840714", "fof2": 4.549, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": 6.527, "yf1": null, "time": "2023-05-20T13:40:00", "fof1": null, "yf2": null, "mufd": 15.9539263689746, "station": {"code": "OK426", "longitude": "127.8", "name": "Okinawa, Japan", "latitude": "26.33", "id": 93}, "hmf2": 210.297, "fbes": null, "hf2": 183.5, "foe": null, "id": 13672061, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.65245567055279", "fof2": 4.368, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2023-05-20T13:50:00", "fof1": null, "yf2": null, "mufd": 18.8546520271897, "station": {"code": "WK546", "longitude": "141.7", "name": "Wakanai, Japan", "latitude": "45.4", "id": 94}, "hmf2": 354.25, "fbes": null, "hf2": 313.5, "foe": null, "id": 13672148, "hmf1": null, "cs": -1.0, "hme": null, "md": "2.8053343292947", "fof2": 6.721, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": 2.0, "yf1": null, "time": "2021-08-23T09:00:01", "fof1": null, "yf2": null, "mufd": 16.32, "station": {"code": "KB548", "longitude": "135.1", "name": "Khabarovsk, Russia", "latitude": "48.5", "id": 95}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6797592, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.4", "fof2": 4.8, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": 1.6, "yf1": null, "time": "2021-08-23T15:00:01", "fof1": null, "yf2": null, "mufd": 9.3, "station": {"code": "MG560", "longitude": "151", "name": "Magadan, Russia", "latitude": "60", "id": 96}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6798646, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.1", "fof2": 3.0, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2021-08-22T21:00:01", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "ML449", "longitude": "117.5", "name": "Manzhouli, China", "latitude": "49.6", "id": 97}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6792857, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 3.2, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": 2.1, "yf1": null, "time": "2021-08-23T16:00:01", "fof1": null, "yf2": null, "mufd": 14.19, "station": {"code": "MM168", "longitude": "33", "name": "Murmansk, Russia", "latitude": "69", "id": 98}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6798650, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.3", "fof2": 4.3, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2021-08-23T16:00:01", "fof1": null, "yf2": null, "mufd": 12.8, "station": {"code": "SD266", "longitude": "66.5", "name": "Salekhard, Russia", "latitude": "66.5", "id": 99}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6798654, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.2", "fof2": 4.0, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2021-08-23T15:00:01", "fof1": null, "yf2": null, "mufd": 16.66, "station": {"code": "TK356", "longitude": "84.9", "name": "Tomsk, Russia", "latitude": "56.5", "id": 100}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6798657, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.4", "fof2": 4.9, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2021-08-22T23:00:01", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "GU421", "longitude": "113.4", "name": "Guangzhou, China", "latitude": "23.1", "id": 101}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6792883, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 4.8, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2021-08-22T23:00:01", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "HA419", "longitude": "109.3", "name": "Hainan, China", "latitude": "18.3", "id": 102}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6792874, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 4.6, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2026-09-09T16:30:00", "fof1": 5.2, "yf2": null, "mufd": 37.9, "station": {"code": "TUJ2O", "longitude": "294.6", "name": "Tucuman, Argentina TUJ2O", "latitude": "-26.9", "id": 103}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 23126217, "hmf1": null, "cs": -1.0, "hme": null, "md": "3.32", "fof2": 11.4, "scalef2": null, "tec": null, "source": "ingv"}, {"he": 100.0, "foes": 3.975, "yf1": null, "time": "2026-02-03T23:56:00", "fof1": 4.545, "yf2": null, "mufd": 17.854, "station": {"code": "LM42J", "longitude": "114.1", "name": "Learmonth", "latitude": "-22.2", "id": 104}, "hmf2": 306.459, "fbes": 3.95, "hf2": 296.84, "foe": 3.325, "id": 21712648, "hmf1": null, "cs": -1.0, "hme": null, "md": "2.813", "fof2": 6.347, "scalef2": null, "tec": null, "source": "aus-sws"}, {"he": null, "foes": 4.0, "yf1": null, "time": "2021-08-23T12:00:01", "fof1": null, "yf2": null, "mufd": 3.0, "station": {"code": "AS237", "longitude": "58.3", "name": "Ashkhabad", "latitude": "37.9", "id": 105}, "hmf2": null, "fbes": null, "hf2": null, "foe": null, "id": 6797576, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 8.2, "scalef2": null, "tec": null, "source": "noaa"}, {"he": null, "foes": 3.06, "yf1": null, "time": "2022-03-30T06:00:00", "fof1": null, "yf2": null, "mufd": 19.22, "station": {"code": "MO156", "longitude": "38.28", "name": "Elektrougli, Russia", "latitude": "55.76", "id": 107}, "hmf2": 276.0, "fbes": null, "hf2": null, "foe": 2.74, "id": 8752186, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 6.31, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": 3.98, "yf1": null, "time": "2022-03-30T06:00:00", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "KB547", "longitude": "134.77", "name": "Khabarovsk, Russia KB547", "latitude": "47.52", "id": 108}, "hmf2": 324.0, "fbes": null, "hf2": null, "foe": 3.1, "id": 8752181, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 8.73, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": 2.54, "yf1": null, "time": "2022-03-29T19:14:57", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "KL154", "longitude": "20.12", "name": "Kaliningrad, Russia", "latitude": "54.36", "id": 109}, "hmf2": 334.0, "fbes": null, "hf2": null, "foe": null, "id": 8747427, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 7.03, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2022-03-30T06:00:01", "fof1": null, "yf2": null, "mufd": 24.18, "station": {"code": "MA560", "longitude": "150.93", "name": "Magadan, Russia MA560", "latitude": "60.09", "id": 111}, "hmf2": 249.0, "fbes": null, "hf2": null, "foe": 2.84, "id": 8752204, "hmf1": null, "cs": 100.0, "hme": null, "md": null, "fof2": 7.45, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2022-03-30T05:30:00", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "NV355", "longitude": "82.21", "name": "Novosibirsk, Russia", "latitude": "55.02", "id": 112}, "hmf2": 388.0, "fbes": null, "hf2": null, "foe": 3.2, "id": 8751997, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 13.94, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2022-03-30T06:00:00", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "PK553", "longitude": "158.24", "name": "Petropavlovsk, Russia", "latitude": "52.97", "id": 113}, "hmf2": 317.0, "fbes": null, "hf2": null, "foe": 2.57, "id": 8752166, "hmf1": null, "cs": -1.0, "hme": null, "md": null, "fof2": 7.72, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": null, "yf1": null, "time": "2026-07-13T22:07:40", "fof1": 4.15, "yf2": null, "mufd": 17.147, "station": {"code": "PF765", "longitude": "212.55", "name": "Poker Flat, AK, USA", "latitude": "65.13", "id": 114}, "hmf2": null, "fbes": null, "hf2": 420.0, "foe": null, "id": 22741201, "hmf1": null, "cs": 0.0, "hme": null, "md": "3.266", "fof2": 5.25, "scalef2": null, "tec": null, "source": "giro"}, {"he": null, "foes": 2.2, "yf1": null, "time": "2024-04-30T23:30:00", "fof1": null, "yf2": null, "mufd": 21.502, "station": {"code": "THJ76", "longitude": "291.56", "name": "Thule, Greenland", "latitude": "76.54", "id": 115}, "hmf2": 311.7, "fbes": null, "hf2": null, "foe": 2.4, "id": 17172396, "hmf1": null, "cs": 40.0, "hme": null, "md": null, "fof2": 7.563, "scalef2": null, "tec": 16.2, "source": "giro_fastchar"}, {"he": null, "foes": null, "yf1": null, "time": "2024-04-29T18:37:30", "fof1": null, "yf2": null, "mufd": 28.684, "station": {"code": "WA619", "longitude": "166.647", "name": "Wake Island", "latitude": "19.294", "id": 116}, "hmf2": 281.2, "fbes": null, "hf2": null, "foe": null, "id": 17163384, "hmf1": null, "cs": 75.0, "hme": null, "md": null, "fof2": 8.55, "scalef2": null, "tec": 17.9, "source": "giro_fastchar"}, {"he": null, "foes": null, "yf1": null, "time": "2018-04-23T21:24:07", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "IS141", "longitude": "29.029", "name": "IS141", "latitude": "41.1", "id": 117}, "hmf2": null, "fbes": null, "hf2": null, "foe": 16.131, "id": 11297609, "hmf1": null, "cs": -1.0, "hme": 149.374, "md": null, "fof2": null, "scalef2": null, "tec": 6.962, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2015-06-10T06:55:00", "fof1": null, "yf2": null, "mufd": 14.6877075117143, "station": {"code": "LO168", "longitude": "35.02", "name": "LO168", "latitude": "68", "id": 118}, "hmf2": 450.114, "fbes": null, "hf2": 315.8, "foe": null, "id": 9129573, "hmf1": null, "cs": -1.0, "hme": null, "md": "2.45490682127935", "fof2": 5.983, "scalef2": null, "tec": null, "source": "noaa"}, {"he": 101.585, "foes": 3.351, "yf1": null, "time": "2015-07-20T09:40:00", "fof1": null, "yf2": null, "mufd": null, "station": {"code": "PB671", "longitude": "170.92", "name": "PB671", "latitude": "70.83", "id": 119}, "hmf2": null, "fbes": null, "hf2": null, "foe": 1.838, "id": 9140273, "hmf1": null, "cs": -1.0, "hme": 117.75, "md": null, "fof2": null, "scalef2": null, "tec": null, "source": "noaa"}, {"he": 105.0, "foes": 2.9, "yf1": null, "time": "2022-09-16T22:00:05", "fof1": null, "yf2": 83.697, "mufd": 28.562, "station": {"code": "HAJ43", "longitude": "288.8", "name": "HAJ43", "latitude": "42.45", "id": 364}, "hmf2": 292.4, "fbes": null, "hf2": null, "foe": 2.42, "id": 10668375, "hmf1": null, "cs": 50.0, "hme": 110.0, "md": "3.038", "fof2": 9.4, "scalef2": 46.286, "tec": 22.129, "source": "noaa"}, {"he": null, "foes": null, "yf1": null, "time": "2023-08-18T19:15:00", "fof1": null, "yf2": 97.511, "mufd": 20.366, "station": {"code": "DH224", "longitude": "54.58", "name": "Al Dhafra AFB", "latitude": "24.24", "id": 365}, "hmf2": 401.59, "fbes": null, "hf2": 315.0, "foe": null, "id": 14607104, "hmf1": null, "cs": 95.0, "hme": 110.0, "md": "2.628", "fof2": 7.75, "scalef2": 51.012, "tec": null, "source": "giro"}])json";

int main() {
    const std::string fx(kFixture);
    const double now1 = (double)1788972301;   // batch fresco del fixture
    std::vector<Kc2gRow> rows;
    expectTrue("parseStations(fixture real de 101 filas)",
               Kc2gAdapter::parseStations(fx, rows));
    expectNear("rows.size() == 101", (double)rows.size(), 101, 0);
    expectNear("3a via: '\"station\": {' == 101",
               (double)countOccur(fx, "\"station\": {"), 101, 0);
    expectNear("3a via: '\"md\": null' == 25",
               (double)countOccur(fx, "\"md\": null"), 25, 0);

    std::printf("[test_kc2g_parse] A: estructura y fuentes\n");
    {
        int nG = 0, nN = 0, nI = 0, nA = 0, nF = 0;
        for (const auto& r : rows) {
            if (r.source == "giro") nG++;
            else if (r.source == "noaa") nN++;
            else if (r.source == "ingv") nI++;
            else if (r.source == "aus-sws") nA++;
            else if (r.source == "giro_fastchar") nF++;
        }
        expectNear("fuente giro == 49", nG, 49, 0);
        expectNear("fuente noaa == 46", nN, 46, 0);
        expectNear("fuente ingv == 2", nI, 2, 0);
        expectNear("fuente aus-sws == 2", nA, 2, 0);
        expectNear("fuente giro_fastchar == 2", nF, 2, 0);
        expectNear("3a via: '\"source\": \"giro\"' == 49",
                   (double)countOccur(fx, "\"source\": \"giro\""), 49, 0);
        expectTrue("orden: fila 0 == AU930 (orden del archivo)", rows[0].code == "AU930");
        expectTrue("orden: fila 1 == EA036", rows[1].code == "EA036");
        bool dup = false;
        for (size_t i = 0; i < rows.size() && !dup; i++)
            for (size_t j = i + 1; j < rows.size(); j++)
                if (rows[i].code == rows[j].code) { dup = true; break; }
        expectTrue("sin codigos duplicados", !dup);
    }

    std::printf("[test_kc2g_parse] B: normalizacion lon 0-360 -> -180..180\n");
    {
        int neg = 0;
        double mn = 1e9, mx = -1e9, mnLat = 1e9, mxLat = -1e9;
        for (const auto& r : rows) {
            if (r.lonDeg < 0.0) neg++;
            if (r.lonDeg < mn) mn = r.lonDeg;
            if (r.lonDeg > mx) mx = r.lonDeg;
            if (r.latDeg < mnLat) mnLat = r.latDeg;
            if (r.latDeg > mxLat) mxLat = r.latDeg;
        }
        expectNear("filas con lon negativa (eran 0-360 > 180)", neg, 34, 0);
        expectNear("lon min (>= -180)", mn, -174.08000000000001, 1e-6);
        expectNear("lon max (<= 180)", mx, 170.91999999999999, 1e-6);
        expectNear("lat min (rango global)", mnLat, -77.849999999999994, 1e-4);
        expectNear("lat max (rango global)", mxLat, 76.540000000000006, 1e-4);
        expectNear("AU930 lon '262.3' -> -97.7", findByCode(rows, "AU930")->lonDeg, -97.7, 1e-3);
        expectNear("EA036 lon '353.3' -> -6.7", findByCode(rows, "EA036")->lonDeg, -6.7, 1e-3);
        expectNear("EB040 lon '0.5' -> 0.5 (sin cambio)", findByCode(rows, "EB040")->lonDeg, 0.5, 1e-6);
    }

    std::printf("[test_kc2g_parse] C: md COMO CADENA (sello FASE A)\n");
    {
        int empty = 0, nonempty = 0;
        for (const auto& r : rows) (r.md.empty() ? empty : nonempty)++;
        expectNear("md vacio (null en JSON) == 25", empty, 25, 0);
        expectNear("md con contenido == 76", nonempty, 76, 0);
        expectStr("EB040 md == \"3.288\" (cadena)", findByCode(rows, "EB040")->md, "3.288");
        expectStr("DB049 md == \"3.085\" (cadena)", findByCode(rows, "DB049")->md, "3.085");
        expectStr("AU930 md == \"3.352\" (cadena)", findByCode(rows, "AU930")->md, "3.352");
    }

    std::printf("[test_kc2g_parse] D: anclas de fila (esperanzas Python)\n");
    {
        const Kc2gRow* eb = findByCode(rows, "EB040");
        const Kc2gRow* db = findByCode(rows, "DB049");
        const Kc2gRow* au = findByCode(rows, "AU930");
        const Kc2gRow* eg = findByCode(rows, "EG931");
        expectNear("EB040 lat", eb->latDeg, 40.799999999999997, 1e-4);
        expectNear("EB040 lon", eb->lonDeg, 0.5, 1e-4);
        expectNear("EB040 fof2", eb->fof2, 7.75, 1e-5);
        expectNear("EB040 mufd", eb->mufd, 25.479999542236328, 1e-5);
        expectNear("EB040 hmf2", eb->hmf2, 254.58500671386719, 1e-5);
        expectNear("EB040 fof1", eb->fof1, 3.8499999046325684, 1e-5);
        expectNear("EB040 foe", eb->foe, 2.5, 1e-5);
        expectNear("EB040 foes", eb->foes, 2.9000000953674316, 1e-5);
        expectNear("EB040 hf2 (h'F2)", eb->hf2, 245, 1e-5);
        expectNear("EB040 he (h'E)", eb->he, 91.345001220703125, 1e-5);
        expectNear("EB040 cs", eb->cs, 60, 1e-5);
        expectStr("EB040 source", eb->source, "giro");
        expectNear("EB040 timeUtc", eb->timeUtc, 1788972301, 0.5);
        expectNear("DB049 fof2", db->fof2, 6.8000001907348633, 1e-5);
        expectNear("DB049 mufd", db->mufd, 20.975000381469727, 1e-5);
        expectNear("DB049 hmf2", db->hmf2, 273.66000366210938, 1e-5);
        expectNear("DB049 fof1 (null -> 0)", db->fof1, 0, 1e-9);
        expectNear("DB049 foe", db->foe, 2.2999999523162842, 1e-5);
        expectNear("DB049 hf2", db->hf2, 232.5, 1e-5);
        expectNear("DB049 he", db->he, 102.23200225830078, 1e-5);
        expectNear("DB049 timeUtc", db->timeUtc, 1788972301, 0.5);
        expectNear("AU930 fof2", au->fof2, 8.6000003814697266, 1e-5);
        expectNear("AU930 mufd", au->mufd, 28.826999664306641, 1e-5);
        expectNear("AU930 hmf2", au->hmf2, 241.80000305175781, 1e-5);
        expectNear("AU930 foe", au->foe, 3.3199999332427979, 1e-5);
        expectNear("AU930 cs", au->cs, 100, 1e-5);
        expectNear("AU930 timeUtc (fila de 2026-03-19)", au->timeUtc, 1773958205, 0.5);
        expectNear("EG931 fof2 (feed congelado)", eg->fof2, 10.437999725341797, 1e-5);
        expectNear("EG931 mufd", eg->mufd, 29.613000869750977, 1e-5);
        expectStr("EG931 source == noaa", eg->source, "noaa");
        expectNear("EG931 timeUtc (2024-08-22)", eg->timeUtc, 1724353650, 0.5);
    }

    std::printf("[test_kc2g_parse] E: agregados (cobertura de TODAS las filas)\n");
    {
        double sf = 0, sm = 0, sh = 0, st = 0, sla = 0, slo = 0;
        for (const auto& r : rows) {
            sf += r.fof2; sm += r.mufd; sh += r.hmf2; st += r.timeUtc;
            sla += r.latDeg; slo += r.lonDeg;
        }
        expectNear("suma fof2 (101 filas)", sf, 655.57600069046021, 1e-3);
        expectNear("suma mufd", sm, 1760.1640827655792, 1e-3);
        expectNear("suma hmf2", sh, 21489.009002685547, 1e-3);
        expectNear("suma timeUtc (ISO->epoch)", st, 172856913015, 1e-6);
        expectNear("suma latDeg", sla, 2214.7399999999998, 1e-6);
        expectNear("suma lonDeg (normalizada)", slo, 2902.2179999999998, 1e-6);
    }

    std::printf("[test_kc2g_parse] F: time-filter (grids <= 6 h, panel <= 24 h)\n");
    {
        int g = 0, p = 0;
        for (const auto& r : rows) {
            if (Kc2gAdapter::gridUsable(r, now1)) g++;
            if (Kc2gAdapter::panelUsable(r, now1)) p++;
        }
        expectNear("gridUsable @ batch (<= 6 h)", g, 29, 0);
        expectNear("panelUsable @ batch (<= 24 h)", p, 29, 0);
        g = 0; p = 0;
        for (const auto& r : rows) {
            if (Kc2gAdapter::gridUsable(r, now1 + 7.0 * 3600.0)) g++;
            if (Kc2gAdapter::panelUsable(r, now1 + 7.0 * 3600.0)) p++;
        }
        expectNear("gridUsable @ +7 h -> 0 (banda stale)", g, 0, 0);
        expectNear("panelUsable @ +7 h (banda panel-stale)", p, 29, 0);
        g = 0; p = 0;
        for (const auto& r : rows) {
            if (Kc2gAdapter::gridUsable(r, now1 + 25.0 * 3600.0)) g++;
            if (Kc2gAdapter::panelUsable(r, now1 + 25.0 * 3600.0)) p++;
        }
        expectNear("gridUsable @ +25 h -> 0", g, 0, 0);
        expectNear("panelUsable @ +25 h -> 0 (kc2g muerto -> modelo)", p, 0, 0);
        // Bordes EXACTOS del sello (fila sintetica con dato, no bloqueada).
        Kc2gRow br;
        br.code = "XX000"; br.latDeg = 10.0; br.lonDeg = 20.0;
        br.fof2 = 5.0f; br.mufd = 15.0f; br.hmf2 = 300.0f;
        br.timeUtc = now1 - 21600.0;
        expectTrue("borde grid: age == 6 h exacto -> SI", Kc2gAdapter::gridUsable(br, now1));
        br.timeUtc = now1 - 21601.0;
        expectTrue("borde grid: 6 h + 1 s -> NO", !Kc2gAdapter::gridUsable(br, now1));
        expectTrue("borde panel: 6 h + 1 s -> SI (stale)", Kc2gAdapter::panelUsable(br, now1));
        br.timeUtc = now1 - 86400.0;
        expectTrue("borde panel: age == 24 h exacto -> SI", Kc2gAdapter::panelUsable(br, now1));
        expectTrue("borde grid: 24 h -> NO", !Kc2gAdapter::gridUsable(br, now1));
        br.timeUtc = now1 - 86401.0;
        expectTrue("borde panel: 24 h + 1 s -> NO", !Kc2gAdapter::panelUsable(br, now1));
        br.timeUtc = 0.0;
        expectTrue("timeUtc == 0 (ISO mal) -> nada",
                   !Kc2gAdapter::gridUsable(br, now1) && !Kc2gAdapter::panelUsable(br, now1));
        br.timeUtc = now1; br.fof2 = 0.0f; br.mufd = 0.0f; br.hmf2 = 0.0f;
        expectTrue("fila sin dato (nulls) -> nada",
                   !Kc2gAdapter::gridUsable(br, now1) && !Kc2gAdapter::panelUsable(br, now1));
    }

    std::printf("[test_kc2g_parse] G: EG931-2024 fuera de grids POR REGLA\n");
    {
        const Kc2gRow* eg = findByCode(rows, "EG931");
        expectTrue("EG931 del fixture (~2 anios) -> grid NO", !Kc2gAdapter::gridUsable(*eg, now1));
        expectTrue("EG931 del fixture (~2 anios) -> panel NO", !Kc2gAdapter::panelUsable(*eg, now1));
        expectTrue("isGridBlockedCode(\"EG931\") == true (regla explicita)",
                   Kc2gAdapter::isGridBlockedCode("EG931"));
        expectTrue("isGridBlockedCode(\"EB040\") == false",
                   !Kc2gAdapter::isGridBlockedCode("EB040"));
        Kc2gRow fr;
        fr.code = "EG931"; fr.latDeg = 51.7; fr.lonDeg = -1.8;
        fr.fof2 = 6.5f; fr.mufd = 19.0f; fr.hmf2 = 300.0f; fr.timeUtc = now1;
        expectTrue("EG931 FRESCA: grid NO por regla explicita (no por edad)",
                   !Kc2gAdapter::gridUsable(fr, now1));
        expectTrue("EG931 fresca: panel SI (solo grids excluidos)",
                   Kc2gAdapter::panelUsable(fr, now1));
        StationSample seg = Kc2gAdapter::toSample(fr, now1);
        expectTrue("EG931 fresca -> sample valid + stale (panel con marca)",
                   seg.valid && seg.stale);
    }

    std::printf("[test_kc2g_parse] H: toSample (B0/B1 + historia de 1 punto)\n");
    {
        const Kc2gRow* eb = findByCode(rows, "EB040");
        StationSample s = Kc2gAdapter::toSample(*eb, now1);
        expectStr("EB040 sample code", s.code, "EB040");
        expectNear("EB040 sample foF2", s.foF2, 7.75, 1e-5);
        expectNear("EB040 sample muf (mufd)", s.muf, 25.479999542236328, 1e-5);
        expectNear("EB040 sample hmF2", s.hmF2, 254.58500671386719, 1e-5);
        expectNear("EB040 sample foF1", s.foF1, 3.8499999046325684, 1e-5);
        expectNear("EB040 sample foE", s.foE, 2.5, 1e-5);
        expectNear("EB040 sample foEs", s.foEs, 2.9000000953674316, 1e-5);
        expectNear("EB040 sample hF2 (h'F2 kc2g)", s.hF2, 245, 1e-5);
        expectNear("EB040 sample hE", s.hE, 91.345001220703125, 1e-5);
        expectNear("EB040 sample cs (int)", s.cs, 60, 0);
        expectNear("EB040 nmF2 = 1.24e10*foF2^2", s.nmF2, 744775024640, 1e4);
        expectNear("EB040 sample timeUtc", s.timeUtc, 1788972301, 0.5);
        expectTrue("EB040 sample valid (fresca)", s.valid);
        expectTrue("EB040 sample !stale (grid-usable)", !s.stale);
        expectNear("EB040 B0 == 0 (kc2g no lo trae)", s.B0, 0.0, 0);
        expectNear("EB040 B1 == 0 (kc2g no lo trae)", s.B1, 0.0, 0);
        expectNear("EB040 fmin == 0 (ausente)", s.fmin, 0.0, 0);
        expectNear("EB040 fxI == 0 (ausente)", s.fxI, 0.0, 0);
        // Cierre del sello: sanitizeB0/B1 de PRODUCCION sanean el 0 -> 100/3
        // (medOf vacio -> 0 -> saneado: "funciona sin codigo extra").
        expectNear("sanitizeB0(0) == 100 (IRI default)", sanitizeB0(0.0f), 100.0, 0);
        expectNear("sanitizeB1(0) == 3 (IRI default)", sanitizeB1(0.0f), 3.0, 0);
        // Historia de 1 punto: sampleHistoryAt clampa al ultimo valor para
        // cualquier epoch (GiroAdapter.cpp, rama h.size()==1) = replay
        // congelado pero honesto (sello FASE A).
        expectNear("EB040 history.size() == 1", (double)s.history.size(), 1, 0);
        expectNear("EB040 history[0].foF2 == fila", s.history[0].foF2, 7.75, 1e-5);
        expectNear("EB040 history[0].muf == fila", s.history[0].muf, 25.479999542236328, 1e-5);
        expectNear("EB040 history[0].timeUtc == fila", s.history[0].timeUtc, 1788972301, 0.5);
        // Banda panel-stale: fila fresca con now corrido +7 h -> valid + stale.
        StationSample s7 = Kc2gAdapter::toSample(*eb, now1 + 7.0 * 3600.0);
        expectTrue("EB040 @ +7 h: valid (panel) + stale (no grids)",
                   s7.valid && s7.stale);
        expectNear("EB040 @ +7 h: history 1 punto", (double)s7.history.size(), 1, 0);
        // Mas alla del panel window: ni panel, ni historia.
        StationSample s25 = Kc2gAdapter::toSample(*eb, now1 + 25.0 * 3600.0);
        expectTrue("EB040 @ +25 h: !valid + sin historia", !s25.valid && s25.history.empty());
    }

    std::printf("[test_kc2g_parse] I: parser robusto (sinteticos)\n");
    {
        std::vector<Kc2gRow> rs;
        expectTrue("array vacio [] -> true", Kc2gAdapter::parseStations("[]", rs));
        expectNear("array vacio: 0 filas", (double)rs.size(), 0, 0);

        // Pretty-printed (whitespace y saltos) — mismo contrato.
        const char* kPretty =
            "[\n  {\n    \"fof2\": 5.5,\n    \"station\": { \"code\": \"PP000\","
            " \"name\": \"Pretty\", \"latitude\": \"1.5\", \"longitude\": \"2.5\" },\n"
            "    \"time\": \"2026-09-09T10:00:00\",\n    \"source\": \"giro\",\n"
            "    \"md\": \"3.0\"\n  }\n]\n";
        expectTrue("pretty-printed con \\n e indent -> true", Kc2gAdapter::parseStations(kPretty, rs));
        expectNear("pretty: 1 fila", (double)rs.size(), 1, 0);
        expectNear("pretty: fof2", rs[0].fof2, 5.5, 1e-6);
        expectNear("pretty: timeUtc", rs[0].timeUtc, 1788948000, 0.5);

        // Escapes: \\u00e9 -> e-acentuada (UTF-8), \\\" -> comilla, lon 281 -> -79.
        const char* kEsc =
            "[{\"station\": {\"code\": \"QQ000\", \"name\": \"Qu\\u00e9bec \\\"X\\\"\","
            " \"latitude\": \"45.5\", \"longitude\": \"281.0\"},"
            " \"time\": \"2026-09-09T11:00:00\", \"md\": \"2.5\"}]";
        expectTrue("escapes (\\uXXXX y comillas) -> true", Kc2gAdapter::parseStations(kEsc, rs));
        expectStr("escape: name unescaped", rs[0].name, "Qu\xc3\xa9" "bec \"X\"");
        expectNear("escape: lon 281 -> -79", rs[0].lonDeg, -79.0, 1e-6);
        expectStr("escape: md", rs[0].md, "2.5");

        // Claves desconocidas (objetos/arrays anidados, true/false/null): ignoradas.
        const char* kExtra =
            "[{\"zzz\": {\"a\": [1, 2, {\"b\": null}]}, \"fof2\": 6.0,"
            " \"station\": {\"code\": \"RR000\", \"name\": \"Extra\","
            " \"latitude\": \"10\", \"longitude\": \"20\"},"
            " \"time\": \"2026-09-09T12:00:00\", \"wibble\": true, \"tec\": 12.5}]";
        expectTrue("claves desconocidas (anidadas) -> true", Kc2gAdapter::parseStations(kExtra, rs));
        expectNear("extras: 1 fila", (double)rs.size(), 1, 0);
        expectNear("extras: fof2", rs[0].fof2, 6.0, 1e-6);

        // Fila sin station: descartada (documentado en Kc2gAdapter.h).
        const char* kNoSt =
            "[{\"fof2\": 5.0, \"time\": \"2026-09-09T12:00:00\"},"
            " {\"fof2\": 6.0, \"station\": {\"code\": \"SS000\", \"name\": \"S\","
            " \"latitude\": \"1\", \"longitude\": \"2\"},"
            " \"time\": \"2026-09-09T12:00:00\"}]";
        expectTrue("fila sin station -> true (se descarta)", Kc2gAdapter::parseStations(kNoSt, rs));
        expectNear("sin station: queda 1 (SS000)", (double)rs.size(), 1, 0);
        expectTrue("sin station: la que queda es SS000", rs[0].code == "SS000");

        // md como NUMERO (si manana cambia el schema): "" sin coercion.
        const char* kMdNum =
            "[{\"station\": {\"code\": \"TT000\", \"name\": \"T\","
            " \"latitude\": \"1\", \"longitude\": \"2\"},"
            " \"time\": \"2026-09-09T12:00:00\", \"md\": 3.5, \"fof2\": 5.0}]";
        expectTrue("md numerico -> true", Kc2gAdapter::parseStations(kMdNum, rs));
        expectStr("md numerico -> cadena VACIA (sin coercion)", rs[0].md, "");

        // fof2 como string (no numero): se ignora -> 0.
        const char* kStrNum =
            "[{\"station\": {\"code\": \"UU000\", \"name\": \"U\","
            " \"latitude\": \"1\", \"longitude\": \"2\"},"
            " \"time\": \"2026-09-09T12:00:00\", \"fof2\": \"8.6\"}]";
        expectTrue("fof2 string -> true", Kc2gAdapter::parseStations(kStrNum, rs));
        expectNear("fof2 string -> 0 (solo numeros)", rs[0].fof2, 0.0, 0);

        // Rechazos.
        expectTrue("cadena vacia -> false", !Kc2gAdapter::parseStations("", rs));
        expectTrue("objeto al tope (no array) -> false",
                   !Kc2gAdapter::parseStations("{\"a\": 1}", rs));
        expectTrue("garbage -> false", !Kc2gAdapter::parseStations("garbage", rs));
        expectTrue("fixture TRUNCADO a 20000 chars -> false",
                   !Kc2gAdapter::parseStations(fx.substr(0, 20000), rs));
    }

    if (g_failures == 0) {
        std::printf("[test_kc2g_parse] TODOS los checks OK\n");
        return 0;
    }
    std::printf("[test_kc2g_parse] %d check(s) FALLARON\n", g_failures);
    return 1;
}
