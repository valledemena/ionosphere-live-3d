// C2a: rutas de datos de usuario (implementacion; ver Paths.h).
#include "Utils/Paths.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

namespace paths {
namespace {

namespace fs = std::filesystem;

bool g_fallback = false;

#if defined(_WIN32)
// _wgetenv devuelve el valor en wchar_t: el nombre del perfil puede tener
// caracteres fuera de ASCII y no pasa por ninguna conversion de codepage.
fs::path envDir(const wchar_t* name) {
    const wchar_t* v = _wgetenv(name);
    if (v == nullptr || v[0] == L'\0') return fs::path();
    return fs::path(v);
}
#else
fs::path envDir(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0') return fs::path();
    return fs::path(v);
}
#endif

fs::path resolveBase() {
    fs::path base;
#if defined(_WIN32)
    base = envDir(L"LOCALAPPDATA");
    if (base.empty()) base = envDir(L"APPDATA");
    if (base.empty()) base = envDir(L"TEMP");
    if (!base.empty()) base /= L"IonosphereLive3D";
#elif defined(__APPLE__)
    base = envDir("HOME");
    if (!base.empty()) base = base / "Library" / "Caches" / "ionosphere-live-3d";
#else
    base = envDir("XDG_CACHE_HOME");
    if (!base.empty()) {
        base /= "ionosphere-live-3d";
    } else {
        const fs::path home = envDir("HOME");
        if (!home.empty()) base = home / ".cache" / "ionosphere-live-3d";
    }
#endif
    return base;
}

}  // namespace

bool userDataDirFallback() { return g_fallback; }

// Raiz del cache relativo: por defecto el "cache" del CWD de toda la vida.
// App la completa desde settings.cfg (cacheDir <ruta>) ANTES de crear hilos;
// ver Paths.h.
//
// Acceso protegido: loadSettings re-aplica la clave mientras los workers ya
// corren. La asignacion y la lectura replican el path bajo mutex, y
// setCacheRoot no escribe si el valor no cambia (evita un peaje por llamada
// repetida con el mismo valor).
std::mutex g_cacheMutex;
fs::path g_cacheRoot = "cache";

void setCacheRoot(const fs::path& root) {
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    if (root == g_cacheRoot) return;   // idempotente, sin escritura inutil
    g_cacheRoot = root;
}

fs::path cacheRoot() {
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    return g_cacheRoot;
}

std::string cacheRootString() {
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    return g_cacheRoot.string();
}

fs::path userDataDir() {
    static const fs::path cached = []() -> fs::path {
        const fs::path base = resolveBase();
        if (base.empty()) {
            g_fallback = true;
            std::fprintf(stderr,
                         "[Paths] sin directorio de datos de usuario: usando el CWD\n");
            return fs::path(".");
        }
        std::error_code ec;
        fs::create_directories(base, ec);
        if (ec || !fs::is_directory(base, ec) || ec) {
            g_fallback = true;
            std::fprintf(stderr,
                         "[Paths] no se pudo crear el directorio de datos: usando el CWD "
                         "(sin persistencia entre carpetas)\n");
            return fs::path(".");
        }
        return base;
    }();
    return cached;
}

}  // namespace paths
