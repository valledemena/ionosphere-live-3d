// M-IRTAM-F2 (P2): cache numerica gambit-coeffs (ver IrtamCoeffCache.h).
#include "Data/IrtamCoeffCache.h"

#include "Data/IrtamCoeffParse.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>

namespace irtamccache {
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

std::string sanitizeParam(const std::string& param) {
    std::string s;
    for (char c : param) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) s += c;
        else if (c >= 'A' && c <= 'Z') s += (char)(c - 'A' + 'a');
        if (s.size() >= 12) break;
    }
    return s.empty() ? std::string("param") : s;
}

void removeWithMeta(const std::string& txtPath) {
    std::error_code ec;
    std::filesystem::remove(txtPath, ec);
    ec.clear();
    std::filesystem::remove(txtPath + ".meta", ec);
    ec.clear();
}

// Validez estructural del contenido (P1a). Rellena nCoeffs/tov si valida.
bool checkContent(const uint8_t* data, std::size_t size, int& nCoeffs,
                  double& tovEpoch) {
    nCoeffs = 0;
    tovEpoch = 0.0;
    if (!data || size == 0) return false;
    const std::string text((const char*)data, size);
    irtamcoeff::Message m;
    std::string err;
    if (!irtamcoeff::parseMessage(text, m, err)) return false;
    nCoeffs = (int)m.coeffs.size();
    tovEpoch = m.header.tovEpoch;
    return nCoeffs > 0;
}

}  // namespace

bool shouldCacheBucket(std::size_t sizeBytes) {
    return sizeBytes >= kMinBucketBytes && sizeBytes <= kMaxBucketBytes;
}

std::string frameName(const std::string& param, double epoch) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "irtamc_%s_%lld.txt",
                  sanitizeParam(param).c_str(), (long long)epoch);
    return std::string(buf);
}

bool ensureDir(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

bool saveBucket(const std::string& dir, const std::string& param,
                double epoch, const uint8_t* data, std::size_t size,
                const std::string& serverLastMod) {
    if (epoch <= 0.0 || !shouldCacheBucket(size)) return false;
    if (serverLastMod.size() > 128) return false;
    int nCoeffs = 0;
    double tovEpoch = 0.0;
    if (!checkContent(data, size, nCoeffs, tovEpoch)) return false;
    if (!ensureDir(dir)) return false;
    const std::string dst = dir + "/" + frameName(param, epoch);
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

bool loadBucket(const std::string& path, Bucket& out) {
    out = Bucket{};
    std::vector<uint8_t> buf;
    if (!readFile(path, buf, kMaxBucketBytes + 1)) return false;
    int nCoeffs = 0;
    double tovEpoch = 0.0;
    if (!checkContent(buf.data(), buf.size(), nCoeffs, tovEpoch)) return false;
    FILE* fp = std::fopen((path + ".meta").c_str(), "rb");
    std::string lastmod;
    if (fp) {
        char line[160] = {0};
        if (std::fgets(line, sizeof(line), fp) &&
            std::strncmp(line, "lastmod=", 8) == 0) {
            std::string lm(line + 8);
            while (!lm.empty() &&
                   (lm.back() == '\n' || lm.back() == '\r'))
                lm.pop_back();
            if (lm.size() <= 128) lastmod = lm;
        }
        std::fclose(fp);
    }
    out.bytes = std::move(buf);
    out.serverLastMod = lastmod;
    out.nCoeffs = nCoeffs;
    out.tovEpoch = tovEpoch;
    return true;
}

ScanResult loadCached(const std::string& dir) {
    ScanResult r;
    if (!ensureDir(dir)) return r;
    std::error_code ec;
    struct Raw {
        double epoch;
        std::string param, path;
    };
    std::vector<Raw> raws;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec) || ec) continue;
        const std::string name = e.path().filename().string();
        // Solo nuestra familia (el dir se comparte con tec_/kc2g_/irtam_).
        // Los sidecar .meta terminan en ".meta", nunca en ".txt": se excluyen.
        if (name.compare(0, 7, "irtamc_") != 0 || name.size() < 13 ||
            name.compare(name.size() - 4, 4, ".txt") != 0)
            continue;
        const std::string mid = name.substr(7, name.size() - 7 - 4);
        const std::size_t us = mid.rfind('_');
        if (us == std::string::npos || us == 0 || us + 1 >= mid.size())
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
    // Carga + validacion + self-heal.
    struct Good {
        Raw raw;
        Bucket b;
    };
    std::map<std::string, std::vector<Good>> byParam;
    for (const Raw& rw : raws) {
        Bucket b;
        if (!loadBucket(rw.path, b)) {
            removeWithMeta(rw.path);
            ++r.dropped;
            continue;
        }
        b.epoch = rw.epoch;
        b.param = rw.param;
        b.path = rw.path;
        byParam[rw.param].push_back({rw, std::move(b)});
    }
    // Cap POR PARAMETRO (los mas nuevos sobreviven), luego aplanado asc.
    std::vector<Good> keep;
    for (auto& kv : byParam) {
        std::vector<Good>& v = kv.second;
        std::sort(v.begin(), v.end(), [](const Good& a, const Good& b) {
            return a.raw.epoch < b.raw.epoch;
        });
        std::size_t drop =
            v.size() > (std::size_t)kCapBuckets ? v.size() - kCapBuckets : 0;
        for (std::size_t i = 0; i < drop; i++) {
            removeWithMeta(v[i].raw.path);
            ++r.pruned;
        }
        for (std::size_t i = drop; i < v.size(); i++)
            keep.push_back(std::move(v[i]));
    }
    std::sort(keep.begin(), keep.end(), [](const Good& a, const Good& b) {
        return a.raw.epoch < b.raw.epoch;
    });
    r.buckets.reserve(keep.size());
    for (auto& g : keep) r.buckets.push_back(std::move(g.b));
    return r;
}

}  // namespace irtamccache
