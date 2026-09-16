#pragma once
// =============================================================================
// M-IRTAM-F2 (P5): estados honestos + replay frozen-declarado (C9/G9).
//
// Modulo PURO: solo std (string/cmath). Sin curl, sin GL, sin App.
// Testeable en CI (tests/test_irtam_state.cpp).
//
// C9 (aritmetica G9, comentario exigido — ver update() en App.cpp):
//   ventana de replay ~= 24 h (tecHist cap 72, tecCacheMaxAgeH 24.0) y el dato
//   IRTAM disponible empieza en T-72 h (lag 3 dias, kGambitLagSec): CERO
//   solape. IRTAM NO participa del muestreo temporal del replay: durante
//   replay se muestra el bucket mas reciente con nota honesta
//   "(retrospectivo, fuera de ventana de replay)" — congelado DECLARADO,
//   no silencioso.
//
// Reglas P5: sin dato valido (429/504/parse-fail/ventana/TOV-no-comun) la
// capa NO se dibuja (valid=false, precedente cartoon-out) + estado textual
// honesto (jamas silencio, jamas defaults como dato); fail-soft conserva el
// cache con backoff pasivo al siguiente ciclo (el gate C1 lo impone por
// construccion — cero primitivas de pacing nuevas aqui).
// =============================================================================
#include <string>
#include <vector>

namespace irtamstate {

// Nombres declarados para el fold (grep): irtamstate::badgeLine,
// irtamstate::kReplayWindowSec, irtamstate::kGambitLagSec.

inline constexpr double kReplayWindowSec = 86400.0;   // ~24 h (E4)
inline constexpr double kGambitLagSec = 259200.0;     // 3 d (E11)

// Linea de estado para el badge. ageMin < 0 = sin dato. inReplay anade la
// nota frozen-declarada. Nunca devuelve vacio (P5: jamas silencio).
std::string badgeLine(const std::string& param, bool valid, double dataEpoch,
                      const std::string& error, double ageMin, bool inReplay);

// M-irtam-replay-A (ruling R1, CICLO A): ventana IRTAM 96 h sobre el cache
// cap-96 intacto ([72,96] h de edad, probado en vivo). kReplayWindowSec
// (86400, TEC) queda intacta.
inline constexpr double kIrtamReplayWindowSec = 345600.0;  // 96 h

// Zonas de la ventana de union (ruling §5 CORRECCION 2): solo-IRTAM
// [T-96,T-72] · MUERTO estructural (T-72,T-24) · solo-TEC [T-24,T].
enum class ReplayZone { IrtamOnly, DeadGap, TecOnly };

// Por edad del epoch (ageSec = now - epoch): [72h,96h] IrtamOnly;
// (24h,72h) DeadGap; resto (incl. futuro) TecOnly. Puro.
ReplayZone zoneForAge(double ageSec);
const char* zoneName(ReplayZone z);

// Bracket de TOVs ordenados asc para el epoch: vecinos que lo enmarcan +
// factor lerp; clamp al borde con flag; empty si no hay TOVs. Puro.
struct TovBracket {
    std::size_t i0 = 0, i1 = 0;
    double f = 0.0;
    bool clamped = false;
    bool empty = true;
};
TovBracket bracketTovs(const std::vector<double>& sortedTovs, double epoch);

// Lerp celda a celda (mallas mismo tamano; si difieren, false). Puro.
bool lerpValues(const std::vector<float>& a, const std::vector<float>& b,
                double f, std::vector<float>& out);

// "09-11 12:00 UTC" desde epoch. Puro.
std::string formatTovShort(double tovEpoch);

// Badge en replay al TOV MUESTREADO (ruling Q5.3): nombra SIEMPRE el TOV
// mostrado + DATA-age a ese TOV; clamped anade "(borde de ventana)".
// shownTov <= 0 = honesto sin-dato. Nunca devuelve vacio. Puro.
std::string badgeSampled(const std::string& param, double shownTov,
                         double nowUtc, bool clamped);

// M-irtam-replay-025 (ruling R2 RELOJ): hora-dato por capa con rangos
// REALES leidos en runtime (sin hardcodear 24 h/12 h). El llamador aporta
// los rangos (TEC de tecHist bajo lock, GIRO de historyStats/histHours);
// IRTAM usa las zonas R1. Puro, testeable en CI.
struct LayerRanges {
    double tecOldest = -1.0, tecNewest = -1.0;    // <0 = historial vacio
    double giroOldest = -1.0, giroNewest = -1.0;  // <0 = historial vacio
};
struct LayerDataTime {
    double dataEpoch = -1.0;  // hora-dato de la capa (<0 = sin dato)
    bool stale = false;       // clampeada o worker ocupado (ver nota)
    bool clamped = false;
};
// samplerBusy = worker IRTAM con peticion en curso (O2 del veredicto 024:
// el flag solo-escritura ahora se lee): en IRTAM, ocupado => stale (la
// muestra visible va por detras del cursor). En TEC/GIRO se ignora.
LayerDataTime layerDataTime(int mapVariable, double replayEpoch, double nowUtc,
                            const LayerRanges& r, bool samplerBusy);

}  // namespace irtamstate
