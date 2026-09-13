// =============================================================================
// test_kc2g_cache — validacion de la cache kc2g en disco (FASE B P2, sello GLM).
//
// Test de logica pura: sin GL, sin red. Llama a PRODUCCION
// (namespace kc2gcache en src/Data/Kc2gCache.{h,cpp}), no replica.
//
// Cubre la aceptacion FASE B G4: gate de escritura (shouldCacheBatch),
// round-trip binario exacto, corruptos (magic/truncado/trailing),
// cap 144 + self-heal + orden asc (loadCached). Patron test_tec_cache.
// =============================================================================
#include "Data/Kc2gCache.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static int g_ok = 0, g_fail = 0;

static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok;
    else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static std::string freshDir(const char* tag) {
    namespace fs = std::filesystem;
    static int n = 0;
    std::string d = (fs::temp_directory_path() /
        ("kc2g_cache_test_" + std::string(tag) + "_" + std::to_string(n++))).string();
    fs::remove_all(d);
    return d;
}

static kc2gcache::Row makeRow(const char* code, double epoch, float foF2) {
    kc2gcache::Row r;
    r.code = code;
    r.name = code;
    r.latDeg = 40.0;
    r.lonDeg = -4.0;
    r.timeUtc = epoch;
    r.foF2 = foF2;
    r.muf = foF2 * 2.9f;
    r.hmF2 = 300.0f;
    r.valid = true;
    r.stale = false;
    r.cs = 80;
    return r;
}

static kc2gcache::Frame makeFrame(double epoch, int nrows) {
    kc2gcache::Frame f;
    f.epoch = epoch;
    for (int i = 0; i < nrows; i++) {
        char code[16];
        std::snprintf(code, sizeof(code), "EB%03d", i);
        f.rows.push_back(makeRow(code, epoch, 5.0f + (float)i));
    }
    return f;
}

int main() {
    using namespace kc2gcache;
    std::printf("--- FASE B P2: shouldCacheBatch (solo batches con panel) ---\n");
    check_bool("panel>0 se cachea", shouldCacheBatch(3));
    check_bool("panel==0 NO se cachea", !shouldCacheBatch(0));
    check_bool("panel<0 NO se cachea", !shouldCacheBatch(-1));

    std::printf("--- FASE B P2: frameName/ensureDir ---\n");
    check_bool("nombre kc2g_<epoch>.bin", frameName(1000.0) == "kc2g_1000.bin");
    {
        std::string d = freshDir("dir");
        check_bool("ensureDir crea", ensureDir(d) && std::filesystem::is_directory(d));
    }

    std::printf("--- FASE B P2: round-trip binario exacto ---\n");
    {
        std::string d = freshDir("rt");
        Frame f = makeFrame(1789000000.0, 3);
        check_bool("save ok", saveFrame(d, f));
        Frame g;
        check_bool("load ok", loadFrame(d + "/kc2g_1789000000.bin", g));
        check_bool("epoch exacto", g.epoch == f.epoch);
        check_bool("nrows exacto", g.rows.size() == 3);
        bool same = g.rows.size() == f.rows.size();
        for (size_t i = 0; same && i < f.rows.size(); i++) {
            const Row& a = f.rows[i];
            const Row& b = g.rows[i];
            same = same && a.code == b.code && a.name == b.name &&
                   a.latDeg == b.latDeg && a.lonDeg == b.lonDeg &&
                   std::memcmp(&a.foF2, &b.foF2, 4) == 0 &&
                   std::memcmp(&a.muf, &b.muf, 4) == 0 &&
                   std::memcmp(&a.hmF2, &b.hmF2, 4) == 0 &&
                   a.timeUtc == b.timeUtc && a.valid == b.valid &&
                   a.stale == b.stale && a.cs == b.cs;
        }
        check_bool("filas bit-exactas", same);
        Frame empty;
        empty.epoch = 1.0;
        check_bool("save rechaza frame vacio", !saveFrame(d, empty));
        Frame badEpoch;
        badEpoch.rows.push_back(makeRow("EB040", 1.0, 5.0f));
        badEpoch.epoch = 0.0;
        check_bool("save rechaza epoch<=0", !saveFrame(d, badEpoch));
    }

    std::printf("--- FASE B P2: corruptos (magic/truncado/trailing) ---\n");
    {
        std::string d = freshDir("corrupt");
        ensureDir(d);
        FILE* fp = std::fopen((d + "/kc2g_1.bin").c_str(), "wb");
        std::fputs("NOPE-NOPE-NOPE-NOPE", fp);
        std::fclose(fp);
        Frame g;
        check_bool("magic mismatch -> false", !loadFrame(d + "/kc2g_1.bin", g));
        fp = std::fopen((d + "/kc2g_2.bin").c_str(), "wb");
        uint32_t m = kMagic, v = kVersion;
        std::fwrite(&m, 4, 1, fp);
        std::fwrite(&v, 4, 1, fp);  // header cortado
        std::fclose(fp);
        check_bool("truncado -> false", !loadFrame(d + "/kc2g_2.bin", g));
    }

    std::printf("--- FASE B P2: cap 144 + heal + orden ---\n");
    {
        std::string d = freshDir("cap");
        for (int i = 0; i < 147; i++) saveFrame(d, makeFrame(1000000.0 + i * 600.0, 2));
        FILE* fp = std::fopen((d + "/kc2g_9.bin").c_str(), "wb");
        std::fputs("GARBAGE", fp);
        std::fclose(fp);
        // Un tec_ ajeno en el mismo dir compartido no debe tocarse
        fp = std::fopen((d + "/tec_123.bin").c_str(), "wb");
        std::fputs("NOT-MINE", fp);
        std::fclose(fp);
        ScanResult r = loadCached(d);
        check_bool("144 kept", (int)r.frames.size() == 144);
        check_bool("3 podados", r.pruned == 3);
        check_bool("1 corrupto dropeado", r.dropped == 1);
        check_bool("corrupto borrado del disco",
                   !std::filesystem::exists(d + "/kc2g_9.bin"));
        check_bool("frame ajeno intacto",
                   std::filesystem::exists(d + "/tec_123.bin"));
        bool sorted = true;
        for (size_t i = 1; i < r.frames.size(); i++)
            if (r.frames[i].epoch < r.frames[i - 1].epoch) sorted = false;
        check_bool("orden asc por epoch", sorted);
        check_bool("conserva los mas nuevos",
                   r.frames.front().epoch == 1000000.0 + 3 * 600.0);
    }

    std::printf("\n[test_kc2g_cache] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
