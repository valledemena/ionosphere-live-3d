#pragma once
// =============================================================================
// C2a: rutas de datos de usuario.
//
// La cache de TLE y el sidecar de estado vivian en rutas relativas al
// directorio de trabajo ("cache/satellites.tle"), asi que arrancar la app
// desde otra carpeta creaba una cache nueva y, peor, perdia el bloqueo de la
// puerta de red. Aqui se resuelve una ruta absoluta por plataforma.
//
// TIPO: std::filesystem::path, no std::string. En Windows el nombre del perfil
// puede tener caracteres no ASCII ("Jose" con tilde) y una ruta guardada como
// std::string UTF-8 se reinterpreta al abrir el fichero, con lo que el .tmp de
// un nombre y el destino de otro no coinciden y el rename falla en silencio.
// Con fs::path construido desde _wgetenv el nombre viaja como wchar_t en todo
// el camino (creacion, ofstream, rename).
//
// No es un modulo puro: toca entorno y disco (crea el directorio). El resto
// de la logica de red sigue siendo testeable con paths explicitos.
// =============================================================================
#include <filesystem>
#include <string>

namespace paths {

// Directorio de datos de usuario, creado si falta:
//   Windows  %LOCALAPPDATA%\IonosphereLive3D
//   macOS    $HOME/Library/Caches/ionosphere-live-3d
//   Linux    $XDG_CACHE_HOME/ionosphere-live-3d (o $HOME/.cache/...)
// Si no se puede resolver o crear, devuelve "." (nunca lanza).
std::filesystem::path userDataDir();

// true si userDataDir() cayo al directorio de trabajo (sin persistencia real
// entre carpetas). La UI debe declararlo, no ocultarlo.
bool userDataDirFallback();

}  // namespace paths
