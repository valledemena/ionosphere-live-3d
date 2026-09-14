// M-IRTAM (F1): cache IRTAM en disco (implementacion; ver IrtamCache.h).
#include "Data/IrtamCache.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace irtamcache {
namespace {

bool readFile(const std::string& path, std::vector<uint8_t>& out,
              std::size_t maxBytes) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    bool ok = true;
    static const std::size_t kChunk = 65536;
    std::vector<uint8_t> buf;
    for (;;) {
        if (buf.size() >= maxBytes) {
            ok = false;
            break;
        }
        uint8_t tmp[kChunk];
        std::size_t n = std::fread(tmp, 1, kChunk, fp);
        buf.insert(buf.end(), tmp, tmp + n);
        if (n < kChunk) {
            if (std::ferror(fp)) ok = false;
            break;
        }
    }
    std::fclose(fp);
    if (!ok || buf.empty()) return false;
    out = std::move(buf);
    return true;
}

bool writeFileAtomic(const std::string& dst, const uint8_t* data,
                     std::size_t size) {
    const std::string tmp = dst + ".tmp";
    FILE* fp = std::fopen(tmp.c_str(), "wb");
    if (!fp) return false;
    bool ok = size == 0 || std::fwrite(data, 1, size, fp) == size;
    if (std::fclose(fp) != 0) ok = false;
    std::error_code ec;
    if (!ok) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    std::filesystem::rename(tmp, dst, ec);
    return !ec;
}

// Salta sub-bloques (len-byte ... 0). false si se sale del buffer.
bool skipSubBlocks(const uint8_t* d, std::size_t n, std::size_t& p) {
    for (;;) {
        if (p >= n) return false;
        uint8_t len = d[p++];
        if (len == 0) return true;
        if (p + len > n || p + len < p) return false;
        p += len;
    }
}

std::string sanitizeProduct(const std::string& product) {
    std::string s;
    for (char c : product) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) s += c;
        else if (c >= 'A' && c <= 'Z') s += (char)(c - 'A' + 'a');
        if (s.size() >= 12) break;
    }
    return s.empty() ? std::string("map") : s;
}

void removeWithMeta(const std::string& gifPath) {
    std::error_code ec;
    std::filesystem::remove(gifPath, ec);
    ec.clear();
    std::filesystem::remove(gifPath + ".meta", ec);
    ec.clear();
}

}  // namespace

bool hasGifMagic(const uint8_t* data, std::size_t size) {
    if (!data || size < 6) return false;
    return (data[0] == 'G' && data[1] == 'I' && data[2] == 'F' &&
            data[3] == '8' && (data[4] == '7' || data[4] == '9') &&
            data[5] == 'a');
}

bool scanGif(const uint8_t* d, std::size_t n, GifInfo& out) {
    out = GifInfo{};
    if (!hasGifMagic(d, n) || n < 13) return false;
    std::size_t p = 6;
    int w = (int)d[p] | ((int)d[p + 1] << 8);
    int h = (int)d[p + 2] | ((int)d[p + 3] << 8);
    uint8_t packed = d[p + 4];
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
    p += 7;
    if (packed & 0x80) {  // Global Color Table
        std::size_t gct = (std::size_t)3 << ((packed & 0x07) + 1);
        if (p + gct > n || p + gct < p) return false;
        p += gct;
    }
    int frames = 0;
    for (;;) {
        if (p >= n) return false;  // EOF sin trailer = truncado
        uint8_t sep = d[p++];
        if (sep == 0x3B) break;  // trailer
        if (sep == 0x21) {       // extension
            if (p >= n) return false;
            ++p;  // label (cualquiera: F9 grafica, FF app, FE comment...)
            if (!skipSubBlocks(d, n, p)) return false;
            continue;
        }
        if (sep != 0x2C) return false;  // byte desconocido = corrupto
        if (p + 9 > n || p + 9 < p) return false;
        uint8_t ip = d[p + 8];
        p += 9;
        if (ip & 0x80) {  // Local Color Table
            std::size_t lct = (std::size_t)3 << ((ip & 0x07) + 1);
            if (p + lct > n || p + lct < p) return false;
            p += lct;
        }
        if (p >= n) return false;  // falta LZW min code
        ++p;
        if (!skipSubBlocks(d, n, p)) return false;
        ++frames;
        if (frames > 100000) return false;  // fusible anti-bucle
    }
    if (p != n) return false;  // trailing = corrupto
    if (frames == 0) return false;
    out.w = w;
    out.h = h;
    out.nframes = frames;
    return true;
}

bool shouldCacheGif(const uint8_t* data, std::size_t size) {
    return size >= kMinGifBytes && size <= kMaxGifBytes &&
           hasGifMagic(data, size);
}

std::string frameName(const std::string& product, double epoch) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "irtam_%s_%lld.gif",
                  sanitizeProduct(product).c_str(), (long long)epoch);
    return std::string(buf);
}

bool ensureDir(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

bool saveGif(const std::string& dir, const std::string& product, double epoch,
             const uint8_t* data, std::size_t size,
             const std::string& serverLastMod) {
    if (epoch <= 0.0 || !data || !shouldCacheGif(data, size)) return false;
    if (serverLastMod.size() > 128) return false;
    if (!ensureDir(dir)) return false;
    const std::string dst = dir + "/" + frameName(product, epoch);
    if (!writeFileAtomic(dst, data, size)) return false;
    char meta[192];
    std::snprintf(meta, sizeof(meta), "lastmod=%s\nfetch=%.0f\n",
                  serverLastMod.c_str(), epoch);
    std::string ms(meta, sizeof(meta));
    ms.resize(std::strlen(meta));
    if (!writeFileAtomic(dst + ".meta", (const uint8_t*)ms.data(),
                         ms.size())) {
        removeWithMeta(dst);
        return false;
    }
    return true;
}

bool loadGif(const std::string& path, std::vector<uint8_t>& bytesOut,
             std::string& serverLastModOut) {
    bytesOut.clear();
    serverLastModOut.clear();
    std::vector<uint8_t> buf;
    if (!readFile(path, buf, kMaxGifBytes + 1)) return false;
    GifInfo info;
    if (!scanGif(buf.data(), buf.size(), info)) return false;
    FILE* fp = std::fopen((path + ".meta").c_str(), "rb");
    if (fp) {
        char line[160] = {0};
        if (std::fgets(line, sizeof(line), fp) &&
            std::strncmp(line, "lastmod=", 8) == 0) {
            std::string lm(line + 8);
            while (!lm.empty() &&
                   (lm.back() == '\n' || lm.back() == '\r')) lm.pop_back();
            if (lm.size() <= 128) serverLastModOut = lm;
        }
        std::fclose(fp);
    }
    bytesOut = std::move(buf);
    return true;
}

ScanResult loadCached(const std::string& dir) {
    ScanResult r;
    if (!ensureDir(dir)) return r;
    std::error_code ec;
    struct Raw {
        double epoch;
        std::string product, path;
    };
    std::vector<Raw> raws;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec) || ec) continue;
        const std::string name = e.path().filename().string();
        // Solo nuestros frames (el dir se comparte con tec_/kc2g_).
        if (name.compare(0, 6, "irtam_") != 0 ||
            name.size() < 12 || name.compare(name.size() - 4, 4, ".gif") != 0)
            continue;
        const std::string mid = name.substr(6, name.size() - 6 - 4);
        const std::size_t us = mid.rfind('_');
        if (us == std::string::npos || us == 0 ||
            us + 1 >= mid.size())
            continue;
        double epoch = 0.0;
        try {
            epoch = std::stod(mid.substr(us + 1));
        } catch (...) {
            continue;
        }
        if (epoch <= 0.0) continue;
        raws.push_back({epoch, mid.substr(0, us), e.path().string()});
    }
    std::sort(raws.begin(), raws.end(),
              [](const Raw& a, const Raw& b) { return a.epoch < b.epoch; });
    struct Good {
        Raw raw;
        std::vector<uint8_t> bytes;
        std::string lastmod;
        GifInfo info;
    };
    std::vector<Good> goods;
    for (const Raw& rw : raws) {
        std::vector<uint8_t> bytes;
        std::string lastmod;
        if (!loadGif(rw.path, bytes, lastmod)) {
            removeWithMeta(rw.path);
            ++r.dropped;
            continue;
        }
        GifInfo info;
        scanGif(bytes.data(), bytes.size(), info);  // ya validado arriba
        goods.push_back({rw, std::move(bytes), lastmod, info});
    }
    std::size_t keep = 0;
    if (goods.size() > (std::size_t)kCapGifs) {
        keep = goods.size() - kCapGifs;
        for (std::size_t i = 0; i < keep; i++) {
            removeWithMeta(goods[i].raw.path);
            ++r.pruned;
        }
    }
    r.gifs.reserve(goods.size() - keep);
    for (std::size_t i = keep; i < goods.size(); i++) {
        Item it;
        it.epoch = goods[i].raw.epoch;
        it.product = goods[i].raw.product;
        it.path = goods[i].raw.path;
        it.serverLastMod = goods[i].lastmod;
        it.info = goods[i].info;
        it.bytes = goods[i].bytes.size();
        r.gifs.push_back(std::move(it));
    }
    return r;
}

}  // namespace irtamcache
