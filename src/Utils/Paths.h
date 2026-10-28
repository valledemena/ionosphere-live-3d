#pragma once
// =============================================================================
// C2a: rutas de datos de usuario.
//
// La cache de TLE y el sidecar de estado vivian en rutas relativas al
// directorio de trabajo ("cache/satellites.tle"), asi que arrancar la app
// desde otra carpeta creaba una cache nueva y, peor, perdia el bloqueo de la
// puerta de red. Aqui se resuelve una ruta absoluta por plataforma.
//
// No es un modulo puro: toca entorno y disco (crea el directorio). El resto
// de la logica de red sigue siendo testeable con paths explicitos.
// =============================================================================
#include <string>

namespace paths {

// Directorio de datos de usuario, creado si falta:
//   Windows  %LOCALAPPDATA%\IonosphereLive3D
//   macOS    $HOME/Library/Caches/ionosphere-live-3d
//   Linux    $XDG_CACHE_HOME/ionosphere-live-3d (o $HOME/.cache/...)
// Si no se puede resolver o crear, devuelve "." (nunca lanza).
std::string userDataDir();

// Une directorio y nombre con el separador nativo.
std::string join(const std::string& dir, const std::string& name);

}  // namespace paths
