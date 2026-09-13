// FASE B (P2): cache kc2g en disco (implementacion; ver Kc2gCache.h).
#include "Data/Kc2gCache.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace kc2gcache {
namespace {

bool writeStr(FILE* fp, const std::string& s) {
    uint32_t n = (uint32_t)s.size();
    if (n > 64) return false;
    return std::fwrite(&n, 4, 1, fp) == 1 &&
           (n == 0 || std::fwrite(s.data(), 1, n, fp) == n);
}

bool readStr(FILE* fp, std::string& s) {
    uint32_t n = 0;
    if (std::fread(&n, 4, 1, fp) != 1 || n > 64) return false;
    s.resize(n);
    return n == 0 || std::fread(s.data(), 1, n, fp) == n;
}

bool writeAll(const std::string& path, const Frame& f) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    bool ok = true;
    ok = ok && std::fwrite(&kMagic, 4, 1, fp) == 1;
    uint32_t ver = kVersion;
    ok = ok && std::fwrite(&ver, 4, 1, fp) == 1;
    uint32_t nrows = (uint32_t)f.rows.size();
    ok = ok && std::fwrite(&nrows, 4, 1, fp) == 1;
    ok = ok && std::fwrite(&f.epoch, 8, 1, fp) == 1;
    for (const Row& r : f.rows) {
        ok = ok && writeStr(fp, r.code) && writeStr(fp, r.name);
        ok = ok && std::fwrite(&r.latDeg, 8, 1, fp) == 1;
        ok = ok && std::fwrite(&r.lonDeg, 8, 1, fp) == 1;
        const float fs[] = {r.foF2, r.muf,  r.hmF2, r.foF1, r.foE,  r.foEs,
                            r.hF2,  r.hF,   r.hE,   r.hEs,  r.fmin};
        ok = ok && std::fwrite(fs, 4, 11, fp) == 11;
        ok = ok && std::fwrite(&r.timeUtc, 8, 1, fp) == 1;
        const unsigned char flags =
            (unsigned char)((r.valid ? 1 : 0) | (r.stale ? 2 : 0));
        ok = ok && std::fwrite(&flags, 1, 1, fp) == 1;
        int32_t cs = r.cs;
        ok = ok && std::fwrite(&cs, 4, 1, fp) == 1;
        if (!ok) break;
    }
    if (std::fclose(fp) != 0) ok = false;
    return ok;
}

bool readAll(const std::string& path, Frame& out) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    uint32_t magic = 0, ver = 0, nrows = 0;
    double epoch = 0;
    bool ok = true;
    ok = ok && std::fread(&magic, 4, 1, fp) == 1;
    ok = ok && std::fread(&ver, 4, 1, fp) == 1;
    ok = ok && std::fread(&nrows, 4, 1, fp) == 1;
    ok = ok && std::fread(&epoch, 8, 1, fp) == 1;
    ok = ok && magic == kMagic && ver == kVersion;
    ok = ok && nrows <= 4096 && epoch > 0.0;
    Frame f;
    if (ok) {
        f.epoch = epoch;
        f.rows.reserve(nrows);
        for (uint32_t i = 0; i < nrows && ok; i++) {
            Row r;
            ok = ok && readStr(fp, r.code) && readStr(fp, r.name);
            ok = ok && std::fread(&r.latDeg, 8, 1, fp) == 1;
            ok = ok && std::fread(&r.lonDeg, 8, 1, fp) == 1;
            float fs[11];
            ok = ok && std::fread(fs, 4, 11, fp) == 11;
            r.foF2 = fs[0];
            r.muf = fs[1];
            r.hmF2 = fs[2];
            r.foF1 = fs[3];
            r.foE = fs[4];
            r.foEs = fs[5];
            r.hF2 = fs[6];
            r.hF = fs[7];
            r.hE = fs[8];
            r.hEs = fs[9];
            r.fmin = fs[10];
            ok = ok && std::fread(&r.timeUtc, 8, 1, fp) == 1;
            unsigned char flags = 0;
            ok = ok && std::fread(&flags, 1, 1, fp) == 1;
            r.valid = (flags & 1) != 0;
            r.stale = (flags & 2) != 0;
            int32_t cs = 0;
            ok = ok && std::fread(&cs, 4, 1, fp) == 1;
            r.cs = cs;
            if (ok && r.code.empty()) ok = false;
            if (ok) f.rows.push_back(std::move(r));
        }
        if (ok && std::fgetc(fp) != EOF) ok = false;  // trailing = corrupto
    }
    std::fclose(fp);
    if (!ok) return false;
    out = std::move(f);
    return true;
}

}  // namespace

bool shouldCacheBatch(int panelUsable) {
    return panelUsable > 0;
}

std::string frameName(double epoch) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "kc2g_%lld.bin", (long long)epoch);
    return std::string(buf);
}

bool ensureDir(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

bool saveFrame(const std::string& dir, const Frame& f) {
    if (f.epoch <= 0.0 || f.rows.empty()) return false;
    if (!ensureDir(dir)) return false;
    const std::string dst = dir + "/" + frameName(f.epoch);
    const std::string tmp = dst + ".tmp";
    if (!writeAll(tmp, f)) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, dst, ec);
    return !ec;
}

bool loadFrame(const std::string& path, Frame& out) {
    Frame f;
    if (!readAll(path, f)) return false;
    out = std::move(f);
    return true;
}

ScanResult loadCached(const std::string& dir) {
    ScanResult r;
    if (!ensureDir(dir)) return r;
    std::error_code ec;
    struct Item {
        double epoch;
        std::string path;
        Frame frame;
    };
    std::vector<Item> items;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec) || ec) continue;
        const std::string name = e.path().filename().string();
        // Solo nuestros frames (el dir se comparte con teccache/).
        if (name.compare(0, 5, "kc2g_") != 0 ||
            name.size() < 9 || name.compare(name.size() - 4, 4, ".bin") != 0)
            continue;
        Frame f;
        if (!readAll(e.path().string(), f)) {
            std::filesystem::remove(e.path(), ec);
            ec.clear();
            ++r.dropped;
            continue;
        }
        items.push_back({f.epoch, e.path().string(), std::move(f)});
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.epoch < b.epoch; });
    size_t keep = items.size() > (size_t)kCapBatches ? items.size() - kCapBatches : 0;
    for (size_t i = 0; i < keep; i++) {
        std::filesystem::remove(items[i].path, ec);
        ec.clear();
        ++r.pruned;
    }
    r.frames.reserve(items.size() - keep);
    for (size_t i = keep; i < items.size(); i++) r.frames.push_back(std::move(items[i].frame));
    return r;
}

}  // namespace kc2gcache
