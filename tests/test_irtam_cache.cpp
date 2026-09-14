// =============================================================================
// test_irtam_cache — validacion de la cache IRTAM en disco (M-IRTAM F1).
//
// Test de logica pura: sin GL, sin red. Llama a PRODUCCION
// (namespace irtamcache en src/Data/IrtamCache.{h,cpp}), no replica.
//
// Cubre M-IRTAM G1/G3/G4/G5: gate de escritura (shouldCacheGif), magic,
// walker estructural scanGif (w/h/nframes, truncado/trailing/sin-trailer),
// round-trip gif+sidecar .meta, cap 8 + self-heal + orden asc (loadCached),
// coexistencia con tec_/kc2g_. Patron test_kc2g_cache.
//
// Fixtures: GIFs minimos construidos a mano (1x1, 43 B + extension de
// comentario para pasar el gate de 1 KB). El .gif real de 3.5 MB NO entra
// al repo (G3).
// =============================================================================
#include "Data/IrtamCache.h"

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
        ("irtam_cache_test_" + std::string(tag) + "_" + std::to_string(n++))).string();
    fs::remove_all(d);
    return d;
}

// 1x1 GIF89a clasico (43 B) + extension de comentario con `pad` bytes para
// pasar el gate kMinGifBytes sin romper la estructura.
static std::vector<uint8_t> makeGif(int frames, int pad) {
    static const uint8_t kBase[] = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61,  // GIF89a
        0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,  // LSD 1x1 + GCT flag
        0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,  // GCT: blanco, negro
    };
    static const uint8_t kFrame[] = {
        0x21, 0xF9, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00,  // GCE
        0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
        0x00,  // descriptor 1x1 sin LCT
        0x02, 0x02, 0x44, 0x01, 0x00,  // LZW min 2 + 1 sub-bloque + fin
    };
    std::vector<uint8_t> v(kBase, kBase + sizeof(kBase));
    if (pad > 0) {  // extension de comentario 0x21 0xFE (inerte)
        v.push_back(0x21);
        v.push_back(0xFE);
        int left = pad;
        while (left > 0) {
            int take = left > 255 ? 255 : left;
            v.push_back((uint8_t)take);
            v.insert(v.end(), (size_t)take, 0x41);
            left -= take;
        }
        v.push_back(0x00);
    }
    for (int i = 0; i < frames; i++)
        v.insert(v.end(), kFrame, kFrame + sizeof(kFrame));
    v.push_back(0x3B);  // trailer
    return v;
}

int main() {
    using namespace irtamcache;

    std::printf("--- M-IRTAM F1: magic + gate ---\n");
    {
        std::vector<uint8_t> g1 = makeGif(1, 2000);
        check_bool("fixture >= 1 KB", g1.size() >= 1024);
        check_bool("magic GIF89a", hasGifMagic(g1.data(), g1.size()));
        check_bool("gate acepta mapa plausible",
                   shouldCacheGif(g1.data(), g1.size()));
        std::vector<uint8_t> tiny = makeGif(1, 0);
        check_bool("gate rechaza < 1 KB",
                   !shouldCacheGif(tiny.data(), tiny.size()));
        std::vector<uint8_t> bad = g1;
        bad[0] = 'X';
        check_bool("gate rechaza magic roto",
                   !shouldCacheGif(bad.data(), bad.size()));
        check_bool("gate rechaza nulo",
                   !shouldCacheGif(nullptr, 0));
        std::vector<uint8_t> huge(17u * 1024u * 1024u, 0);
        std::memcpy(huge.data(), "GIF89a", 6);
        check_bool("gate rechaza > 16 MB",
                   !shouldCacheGif(huge.data(), huge.size()));
    }

    std::printf("--- M-IRTAM F1: scanGif walker ---\n");
    {
        GifInfo info;
        std::vector<uint8_t> g1 = makeGif(1, 2000);
        check_bool("1 frame valido", scanGif(g1.data(), g1.size(), info));
        check_bool("w/h 1x1", info.w == 1 && info.h == 1);
        check_bool("nframes 1", info.nframes == 1);
        std::vector<uint8_t> g2 = makeGif(2, 2000);
        check_bool("2 frames validos", scanGif(g2.data(), g2.size(), info));
        check_bool("nframes 2", info.nframes == 2);
        std::vector<uint8_t> trunc = g1;
        trunc.resize(trunc.size() - 5);
        check_bool("truncado -> false",
                   !scanGif(trunc.data(), trunc.size(), info));
        std::vector<uint8_t> notrail = g1;
        notrail.pop_back();
        check_bool("sin trailer -> false",
                   !scanGif(notrail.data(), notrail.size(), info));
        std::vector<uint8_t> trail = g1;
        trail.push_back(0x00);
        check_bool("trailing -> false",
                   !scanGif(trail.data(), trail.size(), info));
        std::vector<uint8_t> noframes = makeGif(0, 2000);
        check_bool("0 frames -> false",
                   !scanGif(noframes.data(), noframes.size(), info));
    }

    std::printf("--- M-IRTAM F1: frameName/ensureDir ---\n");
    check_bool("nombre irtam_<prod>_<epoch>.gif",
               frameName("foF2", 1000.0) == "irtam_fof2_1000.gif");
    {
        std::string d = freshDir("dir");
        check_bool("ensureDir crea", ensureDir(d) && std::filesystem::is_directory(d));
    }

    std::printf("--- M-IRTAM F1: round-trip gif + sidecar ---\n");
    {
        std::string d = freshDir("rt");
        std::vector<uint8_t> g = makeGif(2, 2000);
        const std::string lm("Mon, 10 Aug 2026 16:08:59 GMT");
        check_bool("save ok", saveGif(d, "foF2", 1789000000.0, g.data(),
                                      g.size(), lm));
        std::vector<uint8_t> bytes;
        std::string lastmod;
        check_bool("load ok",
                   loadGif(d + "/irtam_fof2_1789000000.gif", bytes, lastmod));
        check_bool("bytes bit-exactos", bytes == g);
        check_bool("lastmod del sidecar", lastmod == lm);
        // Sin sidecar: carga OK, lastmod vacio (fallback honesto al fetch).
        std::filesystem::remove(d + "/irtam_fof2_1789000000.gif.meta");
        check_bool("load sin meta ok",
                   loadGif(d + "/irtam_fof2_1789000000.gif", bytes, lastmod));
        check_bool("lastmod vacio sin meta", lastmod.empty());
        std::vector<uint8_t> tiny = makeGif(1, 0);
        check_bool("save rechaza < 1 KB",
                   !saveGif(d, "foF2", 1789000001.0, tiny.data(), tiny.size(), lm));
        check_bool("save rechaza epoch<=0",
                   !saveGif(d, "foF2", 0.0, g.data(), g.size(), lm));
        std::string big(200, 'x');
        check_bool("save rechaza lastmod>128",
                   !saveGif(d, "foF2", 1789000002.0, g.data(), g.size(), big));
    }

    std::printf("--- M-IRTAM F1: cap 8 + heal + orden + coexistencia ---\n");
    {
        std::string d = freshDir("cap");
        std::vector<uint8_t> g = makeGif(1, 2000);
        for (int i = 0; i < 10; i++)
            saveGif(d, "foF2", 1000000.0 + i * 900.0, g.data(), g.size(),
                    "Mon, 10 Aug 2026 16:08:59 GMT");
        FILE* fp = std::fopen((d + "/irtam_foF2_9.gif").c_str(), "wb");
        std::fputs("GARBAGE", fp);
        std::fclose(fp);
        // Ajenos en el mismo dir compartido no deben tocarse
        fp = std::fopen((d + "/tec_123.bin").c_str(), "wb");
        std::fputs("NOT-MINE", fp);
        std::fclose(fp);
        fp = std::fopen((d + "/kc2g_456.bin").c_str(), "wb");
        std::fputs("NOT-MINE", fp);
        std::fclose(fp);
        ScanResult r = loadCached(d);
        check_bool("8 kept", (int)r.gifs.size() == 8);
        check_bool("2 podados", r.pruned == 2);
        check_bool("1 corrupto dropeado", r.dropped == 1);
        check_bool("corrupto borrado del disco",
                   !std::filesystem::exists(d + "/irtam_foF2_9.gif"));
        check_bool("ajenos intactos",
                   std::filesystem::exists(d + "/tec_123.bin") &&
                   std::filesystem::exists(d + "/kc2g_456.bin"));
        bool sorted = true;
        for (size_t i = 1; i < r.gifs.size(); i++)
            if (r.gifs[i].epoch < r.gifs[i - 1].epoch) sorted = false;
        check_bool("orden asc por epoch", sorted);
        check_bool("conserva los mas nuevos",
                   r.gifs.front().epoch == 1000000.0 + 2 * 900.0);
        check_bool("producto parseado", r.gifs.front().product == "fof2");
        check_bool("lastmod en sweep",
                   r.gifs.front().serverLastMod == "Mon, 10 Aug 2026 16:08:59 GMT");
        check_bool("info del sweep", r.gifs.front().info.nframes == 1);
    }

    std::printf("\n[test_irtam_cache] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
