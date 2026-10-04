// M10: cache TEC en disco (implementacion; ver TecCache.h).
#include "Data/TecCache.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace teccache {
namespace {

bool writeAll(const std::string& path, const Frame& f) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    bool ok = true;
    ok = ok && std::fwrite(&kMagic, 4, 1, fp) == 1;
    uint32_t ver = kVersion;
    ok = ok && std::fwrite(&ver, 4, 1, fp) == 1;
    ok = ok && std::fwrite(&f.epoch, 8, 1, fp) == 1;
    int32_t w = f.w, h = f.h;
    ok = ok && std::fwrite(&w, 4, 1, fp) == 1;
    ok = ok && std::fwrite(&h, 4, 1, fp) == 1;
    ok = ok && std::fwrite(f.v.data(), 4, f.v.size(), fp) == f.v.size();
    if (std::fclose(fp) != 0) ok = false;
    return ok;
}

bool readAll(const std::string& path, Frame& out) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    uint32_t magic = 0, ver = 0;
    double epoch = 0;
    int32_t w = 0, h = 0;
    bool ok = true;
    ok = ok && std::fread(&magic, 4, 1, fp) == 1;
    ok = ok && std::fread(&ver, 4, 1, fp) == 1;
    ok = ok && std::fread(&epoch, 8, 1, fp) == 1;
    ok = ok && std::fread(&w, 4, 1, fp) == 1;
    ok = ok && std::fread(&h, 4, 1, fp) == 1;
    ok = ok && magic == kMagic && ver == kVersion;
    ok = ok && w > 0 && h > 0 && w <= 4096 && h <= 4096;
    Frame f;
    if (ok) {
        f.epoch = epoch;
        f.w = w;
        f.h = h;
        f.v.resize((size_t)w * (size_t)h);
        ok = std::fread(f.v.data(), 4, f.v.size(), fp) == f.v.size();
        // Trailing bytes => archivo ajeno/relleno: corrupto.
        if (ok && std::fgetc(fp) != EOF) ok = false;
    }
    std::fclose(fp);
    if (!ok) return false;
    out = std::move(f);
    return true;
}

}  // namespace

bool shouldCacheFrame(bool valid, bool simulated, bool enabled) {
    return valid && !simulated && enabled;
}

bool isFresh(double epoch, double nowUtc, double maxAgeH) {
    return (nowUtc - epoch) <= maxAgeH * 3600.0;
}

std::string frameName(double epoch) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "tec_%lld.bin", (long long)epoch);
    return std::string(buf);
}

bool ensureDir(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

bool saveFrame(const std::string& dir, const Frame& f) {
    if (f.w <= 0 || f.h <= 0 || f.epoch <= 0.0) return false;
    if (f.v.size() != (size_t)f.w * (size_t)f.h) return false;
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

bool removeFrame(const std::string& dir, double epoch) {
    std::error_code ec;
    const bool gone =
        std::filesystem::remove(dir + "/" + frameName(epoch), ec);
    return gone && !ec;
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
        // Solo nuestros frames (el dir se comparte con kc2gcache: FASE B).
        if (name.compare(0, 4, "tec_") != 0 ||
            name.size() < 8 || name.compare(name.size() - 4, 4, ".bin") != 0)
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
    size_t keep = items.size() > (size_t)kCapFrames ? items.size() - kCapFrames : 0;
    for (size_t i = 0; i < keep; i++) {
        std::filesystem::remove(items[i].path, ec);
        ec.clear();
        ++r.pruned;
    }
    r.frames.reserve(items.size() - keep);
    for (size_t i = keep; i < items.size(); i++) r.frames.push_back(std::move(items[i].frame));
    return r;
}

std::vector<double> missingSince(const std::vector<double>& cachedEpochs,
                                 const std::vector<double>& indexEpochs) {
    double cutoff = -1.0;
    for (double e : cachedEpochs)
        if (e > cutoff) cutoff = e;
    std::vector<double> out;
    for (double e : indexEpochs)
        if (e > cutoff) out.push_back(e);
    return out;
}

std::vector<std::size_t> missingInWindow(
    const std::vector<double>& cachedEpochs,
    const std::vector<double>& indexEpochs, double nowUtc, std::size_t cap) {
    const double oldest = nowUtc - kHistWindowSec;
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < indexEpochs.size(); i++) {
        const double e = indexEpochs[i];
        if (e < oldest) continue;  // fuera de ventana: ni se pide
        bool found = false;
        for (double c : cachedEpochs) {
            if (c == e) {
                found = true;
                break;
            }
        }
        if (!found) out.push_back(i);
    }
    if (out.size() > cap) out.erase(out.begin(), out.end() - cap);
    return out;
}

}  // namespace teccache
