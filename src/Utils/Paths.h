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
//
// Windows: LOCALAPPDATA llega en la codificacion ANSI del sistema (CP_ACP).
// Se convierte a UTF-8 antes de tocar std::filesystem para que un usuario
// con nombre no ASCII ("Jose Perez" con tilde, "Munoz") no acabe en "." o en
// una carpeta distinta.
// =============================================================================
#include <string>

namespace paths {

// Directorio de datos de usuario, creado si falta, en UTF-8:
//   Windows  %LOCALAPPDATA%\IonosphereLive3D
//   macOS    $HOME/Library/Caches/ionosphere-live-3d
//   Linux    $XDG_CACHE_HOME/ionosphere-live-3d (o $HOME/.cache/...)
// Si no se puede resolver o crear, devuelve "." (nunca lanza).
std::string userDataDir();

// true si userDataDir() cayo al directorio de trabajo (sin persistencia real
// entre carpetas). La UI debe declararlo, no ocultarlo.
bool userDataDirFallback();

// Une directorio y nombre con el separador nativo.
std::string join(const std::string& dir, const std::string& name);

}  // namespace paths
