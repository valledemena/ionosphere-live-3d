#pragma once
// =============================================================================
// M-IRTAM-F2 (P3): gate lgdc compartido (C1) — choke point unico de pacing.
//
// Modulo PURO: solo std (atomic/cstdint). Sin curl, sin GL, sin App, sin URLs,
// sin hosts, sin endpoints. Testeable en CI (tests/test_irtamc_gate.cpp) con
// reloj simulado: toda la API recibe nowMs explicito (ms desde Unix epoch,
// misma base que el msNow de GiroAdapter::pollRound).
//
// Invariante C1: gambit NUNCA a <15 s de CUALQUIER request lgdc de la app
// (catalogo incluido). El pacing M4 de getbest queda intacto y grandfathered:
// GiroAdapter solo REGISTRA sus lanzamientos (catalogo + punto CAS de getbest),
// cero cambio de comportamiento. El fetcher gambit (P4) CONSULTA + REGISTRA.
//
// Semantica forward-only: recordLaunch solo avanza el reloj (CAS-max). Dos
// workers getbest registrando fuera de orden no pueden retrasar el reloj y
// abrir una ventana gambit prematura.
// =============================================================================
#include <atomic>
#include <cstdint>

namespace lgdcpacing {

// Nombres declarados para el fold (grep): lgdcpacing::recordLaunch,
// lgdcpacing::canLaunchGambit, lgdcpacing::lastLaunchMs,
// lgdcpacing::msSinceLastLaunch, lgdcpacing::kGambitGapMs.

inline constexpr long long kGambitGapMs = 15000;  // 1 req/15 s POR CLIENTE (E11)

// Registra un lanzamiento lgdc (catalogo, getbest o gambit) en nowMs.
// Solo avanza: un registro tardio con timestamp viejo no mueve el reloj.
void recordLaunch(long long nowMs);

// Ultimo lanzamiento registrado (0 = ninguno: arranque frio).
long long lastLaunchMs();

// nowMs - lastLaunchMs() (puede ser negativo si el reloj simulado retrocede).
long long msSinceLastLaunch(long long nowMs);

// true si gambit puede lanzarse en nowMs: >= kGambitGapMs desde CUALQUIER
// lanzamiento lgdc previo. Arranque frio (sin registros) = true.
bool canLaunchGambit(long long nowMs);

// Solo tests: vuelve a arranque frio. Patron FASE B (launchGapSec publica
// para tests): el hook de test vive en la cabecera de produccion.
void resetForTest();

}  // namespace lgdcpacing
