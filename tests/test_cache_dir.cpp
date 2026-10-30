// =============================================================================
// test_cache_dir - cache configurable: raiz "cache" de la app (Paths.h) +
// parser de la clave "cacheDir <ruta>" de settings.cfg (appcfg, extraido
// para poder probarlo SIN App).
//
// Llama a PRODUCCION (paths en src/Utils/Paths.cpp + teccache en
// src/Data/TecCache.cpp), no a replicas.
//
// Cubre:
//   (1) parseCacheDirLine: ruta con espacios intacta, recorte de CRLF/tabs,
//       ruta relativa, "cacheDirX"/otras claves NO son cacheDir, y clave sin
//       valor no se consume (vale el defecto).
//   (2) paths::cacheRoot: default "cache" (comportamiento historico) y
//       override con una ruta con ESPACIOS (caso Windows real), con restore
//       al final.
//   (3) Plomeria end-to-end contra la raiz configurada: teccache::saveFrame,
//       loadCached, cleanupTmpFiles y removeFrame (lo mismo que App le pasa
//       a su familia "cache").
// =============================================================================
#include "Utils/Paths.h"
#include "Utils/AppCfg.h"
#include "Data/TecCache.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static int g_ok = 0, g_fail = 0;

static void check_bool(const char* what, bool cond) {
    (cond ? g_ok : g_fail) += 1;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

// Directorio temporal con ESPACIOS en el nombre (caso Windows real: rutas de
// usuario con espacios). Se recrea limpio y se borra al salir.
static std::string freshRoot() {
    static int n = 0;
    std::error_code ec;
    const std::string d = (fs::temp_directory_path() /
        ("cache dir test " + std::to_string(n++) + " con espacios")).string();
    fs::remove_all(d, ec);
    return d;
}

// Registra y restaura la raiz global: los tests no deben dejar modificado el
// estado global de paths (los demas suites dependen del default).
static void restoreRoot(const std::string& previous) {
    paths::setCacheRoot(previous);
}

int main() {
    std::printf("--- (1) parser de cacheDir (appcfg::parseCacheDirLine) ---\n");
    {
        std::string out;
        // Ruta con espacios: loadio COMPLETO, no solo el primer token.
        check_bool("ruta con espacios intacta",
                   appcfg::parseCacheDirLine("cacheDir C:\\Datos Ionos\\cache\r\n", out) &&
                       out == "C:\\Datos Ionos\\cache");
        check_bool("separador tab y recorte de espacios finales",
                   appcfg::parseCacheDirLine("cacheDir\t/var/ionos \t\n", out) &&
                       out == "/var/ionos");
        check_bool("ruta relativa simple",
                   appcfg::parseCacheDirLine("cacheDir cache2\n", out) && out == "cache2");
        check_bool("clave pegada al valor NO es cacheDir (cacheDirX)",
                   !appcfg::parseCacheDirLine("cacheDirX foo\n", out));
        check_bool("otra clave del cfg NO consumida",
                   !appcfg::parseCacheDirLine("tecOpacity 0.55\n", out));
        check_bool("clave sin valor -> no consumida (vale el defecto)",
                   !appcfg::parseCacheDirLine("cacheDir \n", out));
        check_bool("linea vacia -> no consumida",
                   !appcfg::parseCacheDirLine("\n", out));
    }

    std::printf("--- (2) raiz (paths::cacheRoot): default 'cache' + override ---\n");
    {
        const std::string old = paths::cacheRootString();
        check_bool("default historico es 'cache' (CWD)",
                   paths::cacheRootString() == "cache" &&
                       paths::cacheRoot() == "cache");
        const std::string dir = freshRoot();
        paths::setCacheRoot(dir);
        check_bool("override devuelve la ruta configurada",
                   paths::cacheRootString() == dir &&
                       paths::cacheRoot().string() == dir);
        // Plomeria end-to-end contra la raiz configurada.
        std::error_code ec;
        teccache::Frame f;
        f.epoch = 1793349002.0;
        f.w = 3; f.h = 3;
        f.v = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
        check_bool("saveFrame escribe en la raiz configurada",
                   teccache::saveFrame(paths::cacheRootString(), f));
        check_bool("el .bin existe en la raiz con espacios",
                   fs::is_directory(dir, ec) &&
                       fs::exists(fs::path(dir) / teccache::frameName(f.epoch), ec));
        teccache::ScanResult sc = teccache::loadCached(paths::cacheRootString());
        check_bool("loadCached lee el frame de la raiz configurada",
                   sc.frames.size() == 1 && sc.frames[0].epoch == f.epoch &&
                       std::fabs(sc.frames[0].v[4] - 5.0f) < 1e-6);
        teccache::cleanupTmpFiles(paths::cacheRootString(), 300.0);
        check_bool("removeFrame borra dentro de la raiz configurada",
                   teccache::removeFrame(paths::cacheRootString(), f.epoch) &&
                       !fs::exists(fs::path(dir) / teccache::frameName(f.epoch), ec));
        restoreRoot(old);
        check_bool("restore vuelve al default", paths::cacheRootString() == "cache");
        fs::remove_all(dir, ec);
    }

    std::printf("\n[test_cache_dir] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
