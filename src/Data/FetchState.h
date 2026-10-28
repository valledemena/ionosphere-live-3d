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
// CLAVES POR ENDPOINT (revision C2a): el mismo fichero guarda varias puertas,
// una por URL/grupo, con el prefijo delante:
//
//   celestrak.active.last_attempt=...
//   celestrak.active.blocked_until=...
//   celestrak.satnogs.last_attempt=...
//
// Asi C2b (amateur + stations) y el fallback SatNOGS no comparten bloqueo:
// un 403 en un grupo no debe silenciar los demas.
//
// Modulo PURO: solo std. Texto, una clave por linea, tolerante a lineas
// desconocidas y a valores basura (se ignoran, nunca lanzan).
// =============================================================================
#include <string>

#include "Data/FetchGate.h"

namespace fetchstate {

// Escribe el estado de `key`. Crea el directorio padre si falta. Devuelve
// false si no se pudo escribir (no es fatal para el llamante).
bool save(const std::string& path, const std::string& key, const fetchgate::State& st);

// Lee el estado de `key`. `key` vacio lee el formato antiguo sin prefijo
// (migracion de una version anterior). Devuelve false si no hay ninguna clave
// conocida para esa puerta. Los campos ausentes conservan el default.
bool load(const std::string& path, const std::string& key, fetchgate::State& out);

}  // namespace fetchstate
