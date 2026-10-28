#pragma once
// =============================================================================
// C2a: persistencia del estado de la puerta de red (fetchgate::State).
//
// Por que un fichero aparte y no la cabecera de la cache: la cache solo se
// reescribe cuando algo sale bien, y el bloqueo nace justo de un fallo. Un
// sidecar escrito en cada intento (tmp + rename) sobrevive al reinicio, que es
// el escenario que hay que cubrir: cerrar y abrir la app no debe reiniciar la
// cuenta de errores contra CelesTrak.
//
// Modulo PURO: solo std. Formato de texto, una clave por linea, tolerante a
// lineas desconocidas y a valores basura (se ignoran, nunca lanzan).
// =============================================================================
#include <string>

#include "Data/FetchGate.h"

namespace fetchstate {

// Escribe el estado completo. Crea el directorio padre si falta. Devuelve
// false si no se pudo escribir (no es fatal para el llamante).
bool save(const std::string& path, const fetchgate::State& st);

// Lee el estado. Devuelve false si el fichero no existe o no trae ninguna
// clave conocida. Los campos que falten conservan el default del struct.
bool load(const std::string& path, fetchgate::State& out);

}  // namespace fetchstate
