// C2a: rutas de datos de usuario (implementacion; ver Paths.h).
#include "Utils/Paths.h"

#include <cstdlib>
#include <filesystem>

namespace paths {
namespace {

std::string envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && v[0] != '\0') ? std::string(v) : std::string(fallback);
}

}  // namespace

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    const char last = dir.back();
    if (last == '/' || last == '\\') return dir + name;
    return dir + "/" + name;
}

std::string userDataDir() {
    static const std::string cached = []() {
        std::string base;
#if defined(_WIN32)
        base = envOr("LOCALAPPDATA", "");
        if (base.empty()) base = envOr("APPDATA", "");
        if (base.empty()) base = envOr("TEMP", ".");
        base = join(base, "IonosphereLive3D");
#elif defined(__APPLE__)
        base = envOr("HOME", ".");
        base = join(join(base, "Library"), "Caches");
        base = join(base, "ionosphere-live-3d");
#else
        base = envOr("XDG_CACHE_HOME", "");
        if (base.empty()) base = join(envOr("HOME", "."), ".cache");
        base = join(base, "ionosphere-live-3d");
#endif
        std::error_code ec;
        if (!std::filesystem::create_directories(base, ec) && ec) return std::string(".");
        // Comprobacion barata: si el path no es usable, mejor "." que un
        // directorio inventado.
        if (!std::filesystem::is_directory(base, ec) || ec) return std::string(".");
        return base;
    }();
    return cached;
}

}  // namespace paths
