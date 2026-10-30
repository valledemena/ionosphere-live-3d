#pragma once
// =============================================================================
// AppCfg: parseo de claves de settings.cfg EXTRAIDO para test. La app lee
// settings.cfg con fgets + sscanf token a token (valores %63s: id de grupo,
// booleans, doubles) — pero la nueva clave "cacheDir" es una RUTA que puede
// contener espacios ("C:\Datos Ionos\cache"), y sscanf %s corta por espacio.
//
// parseCacheDirLine consume solo las lineas "cacheDir <ruta>" y entrega la
// ruta con espacios intactos; cualquier otra linea devuelve false y no toca
// `out`. Recorta espacios en ambos lados y el CR/LF final (settings.cfg puede
// haberse guardado en CRLF).
//
// Header solo, std: testeable sin App, sin GL, sin red.
// =============================================================================
#include <cstring>
#include <string>

namespace appcfg {

// El nombre de la clave, por si App y el test comparten la literal.
inline constexpr const char* kCacheDirKey = "cacheDir";

inline bool parseCacheDirLine(const char* line, std::string& out) {
    const char* key = kCacheDirKey;
    const size_t klen = std::strlen(key);
    if (std::strncmp(line, key, klen) != 0) return false;
    const char* p = line + klen;
    // Detalle del separador: un espacio o tab como minimo (evita "cacheDirX ...").
    if (*p != ' ' && *p != '\t') return false;
    // Saltar separadores y recortar el final (CR/LF/espacios finales).
    while (*p == ' ' || *p == '\t') ++p;
    size_t end = std::strlen(p);
    while (end > 0 &&
           (p[end - 1] == '\n' || p[end - 1] == '\r' || p[end - 1] == ' ' ||
            p[end - 1] == '\t'))
        --end;
    if (end == 0) return false;   // clave sin valor: se ignora, vale el defecto
    out.assign(p, end);
    return true;
}

}  // namespace appcfg
