// C2a: rutas de datos de usuario (implementacion; ver Paths.h).
#include "Utils/Paths.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace paths {
namespace {

bool g_fallback = false;

std::string envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && v[0] != '\0') ? std::string(v) : std::string(fallback);
}

#if defined(_WIN32)
// LOCALAPPDATA viene en CP_ACP (no UTF-8). std::filesystem de libstdc++ espera
// UTF-8, asi que se convierte; sin esto, "C:\Users\Jose" con tilde se
// corrompe y el directorio acaba en otro sitio.
std::string acpToUtf8(const std::string& in) {
    if (in.empty()) return in;
    const int n = MultiByteToWideChar(CP_ACP, 0, in.c_str(), (int)in.size(), nullptr, 0);
    if (n <= 0) return in;
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, in.c_str(), (int)in.size(), w.data(), n);
    const int m = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), n, nullptr, 0, nullptr, nullptr);
    if (m <= 0) return in;
    std::string u((size_t)m, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), n, u.data(), m, nullptr, nullptr);
    return u;
}
#endif

std::string resolveBase() {
    std::string base;
#if defined(_WIN32)
    base = acpToUtf8(envOr("LOCALAPPDATA", ""));
    if (base.empty()) base = acpToUtf8(envOr("APPDATA", ""));
    if (base.empty()) base = acpToUtf8(envOr("TEMP", ""));
    if (!base.empty()) base = join(base, "IonosphereLive3D");
#elif defined(__APPLE__)
    base = envOr("HOME", "");
    if (!base.empty()) base = join(join(join(base, "Library"), "Caches"), "ionosphere-live-3d");
#else
    base = envOr("XDG_CACHE_HOME", "");
    if (base.empty()) {
        const std::string home = envOr("HOME", "");
        if (!home.empty()) base = join(join(home, ".cache"), "ionosphere-live-3d");
    } else {
        base = join(base, "ionosphere-live-3d");
    }
#endif
    return base;
}

}  // namespace

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    const char last = dir.back();
    if (last == '/' || last == '\\') return dir + name;
    return dir + "/" + name;
}

bool userDataDirFallback() { return g_fallback; }

std::string userDataDir() {
    static const std::string cached = []() {
        const std::string base = resolveBase();
        if (base.empty()) {
            g_fallback = true;
            fprintf(stderr, "[Paths] sin directorio de datos de usuario: usando el CWD\n");
            return std::string(".");
        }
        std::error_code ec;
        const std::filesystem::path p = std::filesystem::u8path(base);
        std::filesystem::create_directories(p, ec);
        if (ec || !std::filesystem::is_directory(p, ec) || ec) {
            g_fallback = true;
            fprintf(stderr,
                    "[Paths] no se pudo crear '%s': usando el CWD (sin persistencia "
                    "entre carpetas)\n",
                    base.c_str());
            return std::string(".");
        }
        return base;
    }();
    return cached;
}

}  // namespace paths
