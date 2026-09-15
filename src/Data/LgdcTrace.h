#pragma once
// =============================================================================
// Ciclo instrumentación-stderr (post-sello): traza timestamped del tráfico
// LGDC a stderr (capturable; stdout es void en este runtime).
//
// Módulo de FORMATO + sumidero. Sin curl, sin GL, sin App, sin URLs, sin
// endpoints. Solo std (string/cstdio/functional/mutex). Testeable en CI
// (tests/test_lgdc_trace.cpp) vía sumidero inyectable.
//
// Línea: `[LGDC <ISO8601-ms-UTC>] <categoria> <detalle>`.
// Categorías: `launch` (catalog/getbest/gambit, instante de REGISTRO en el
// gate), `gate-consult` (allow/deny de gambit + ms desde último), `result`
// (ok/fail de gambit + motivo). CERO cambio de comportamiento: los sitios
// de llamada reutilizan los mismos valores de reloj que el gate.
// =============================================================================
#include <functional>
#include <string>

namespace lgdctrace {

// Nombres declarados para el fold (grep): lgdctrace::trace,
// lgdctrace::formatIsoMs, lgdctrace::setSink, lgdctrace::setEnabled.

// "2026-09-15T01:10:39.123Z" desde ms Unix epoch. Puro.
std::string formatIsoMs(long long msSinceEpoch);

// Emite una línea (stderr salvo sumidero de test). Nunca lanza.
void trace(const std::string& category, const std::string& detail);

// Solo tests: captura líneas en vez de stderr (nullptr = stderr).
// No thread-safe el cambio; el uso sí (mutex interno).
void setSink(std::function<void(const std::string&)> sink);
void setEnabled(bool on);

}  // namespace lgdctrace
