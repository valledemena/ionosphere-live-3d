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

namespace irtamstate {

// Nombres declarados para el fold (grep): irtamstate::badgeLine,
// irtamstate::kReplayWindowSec, irtamstate::kGambitLagSec.

inline constexpr double kReplayWindowSec = 86400.0;   // ~24 h (E4)
inline constexpr double kGambitLagSec = 259200.0;     // 3 d (E11)

// Linea de estado para el badge. ageMin < 0 = sin dato. inReplay anade la
// nota frozen-declarada. Nunca devuelve vacio (P5: jamas silencio).
std::string badgeLine(const std::string& param, bool valid, double dataEpoch,
                      const std::string& error, double ageMin, bool inReplay);

}  // namespace irtamstate
