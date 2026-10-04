// M-IRTAM-F2 (P5): estados honestos (ver IrtamState.h).
#include "Data/IrtamState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace irtamstate {

std::string badgeLine(const std::string& param, bool valid, double dataEpoch,
                      const std::string& error, double ageMin, bool inReplay) {
    char buf[256];
    if (!valid || dataEpoch <= 0.0 || ageMin < 0.0) {
        // Sin dato: texto honesto SIEMPRE; el motivo viaja en la linea
        // (no solo en tooltip) — 429/504/parse-fail/ventana/TOV-no-comun.
        if (!error.empty()) {
            std::snprintf(buf, sizeof(buf),
                          "IRTAM %s: no data (%s; passive retry)",
                          param.c_str(), error.c_str());
        } else {
            std::snprintf(buf, sizeof(buf),
                          "IRTAM %s: no data (backfill running)",
                          param.c_str());
        }
        return std::string(buf);
    }
    // Con dato: badge DATA-age (E10) + nota replay si aplica (C9).
    if (inReplay) {
        std::snprintf(buf, sizeof(buf),
                      "IRTAM %s | retrospectivo (lag ~3d) | %.0f min "
                      "(retrospectivo, fuera de ventana de replay)",
                      param.c_str(), ageMin);
    } else {
        std::snprintf(buf, sizeof(buf),
                      "IRTAM %s | retrospectivo (lag ~3d) | %.0f min",
                      param.c_str(), ageMin);
    }
    return std::string(buf);
}

ReplayZone zoneForAge(double ageSec) {
    if (ageSec >= 259200.0 && ageSec <= 604800.0) return ReplayZone::IrtamOnly;
    return ReplayZone::TecOnly;
}

const char* zoneName(ReplayZone z) {
    switch (z) {
        case ReplayZone::IrtamOnly: return "solo-IRTAM [T-168,T-72]";
        default: return "solo-TEC [T-72,T]";
    }
}

const char* perLayerZoneName(double replayEpoch, double nowUtc,
                             const LayerRanges& r) {
    const bool tec = (r.tecOldest > 0.0 && r.tecOldest <= replayEpoch &&
                      replayEpoch <= r.tecNewest);
    const bool giro = (r.giroOldest > 0.0 && r.giroOldest <= replayEpoch &&
                       replayEpoch <= r.giroNewest);
    const bool irtam = (nowUtc - 604800.0 <= replayEpoch &&
                        replayEpoch <= nowUtc - 259200.0);
    if (tec && giro && irtam) return "TEC+GIRO+IRTAM";
    if (tec && giro) return "TEC+GIRO";
    if (tec && irtam) return "TEC+IRTAM";
    if (giro && irtam) return "GIRO+IRTAM";
    if (tec) return "solo-TEC";
    if (giro) return "solo-GIRO";
    if (irtam) return "solo-IRTAM";
    return "no data";
}

TovBracket bracketTovs(const std::vector<double>& sortedTovs, double epoch) {
    TovBracket r;
    if (sortedTovs.empty()) return r;
    r.empty = false;
    if (epoch <= sortedTovs.front()) {
        r.i0 = r.i1 = 0;
        r.clamped = true;
        return r;
    }
    if (epoch >= sortedTovs.back()) {
        r.i0 = r.i1 = sortedTovs.size() - 1;
        r.clamped = true;
        return r;
    }
    for (std::size_t i = 0; i + 1 < sortedTovs.size(); i++) {
        if (epoch >= sortedTovs[i] && epoch <= sortedTovs[i + 1]) {
            r.i0 = i;
            r.i1 = i + 1;
            const double span = sortedTovs[i + 1] - sortedTovs[i];
            r.f = span > 0.0 ? (epoch - sortedTovs[i]) / span : 0.0;
            return r;
        }
    }
    r.i0 = r.i1 = sortedTovs.size() - 1;
    r.clamped = true;
    return r;
}

bool lerpValues(const std::vector<float>& a, const std::vector<float>& b,
                double f, std::vector<float>& out) {
    if (a.size() != b.size() || a.empty()) return false;
    if (!std::isfinite(f)) return false;
    out.assign(a.size(), 0.0f);
    for (std::size_t i = 0; i < a.size(); i++) {
        const double v =
            (double)a[i] * (1.0 - f) + (double)b[i] * f;
        if (!std::isfinite(v)) return false;
        out[i] = (float)v;
    }
    return true;
}

std::string formatTovShort(double tovEpoch) {
    const std::time_t tt = (std::time_t)(long long)tovEpoch;
    std::tm g{};
#ifdef _WIN32
    gmtime_s(&g, &tt);
#else
    gmtime_r(&tt, &g);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d UTC", g.tm_mon + 1,
                  g.tm_mday, g.tm_hour, g.tm_min);
    return std::string(buf);
}

std::string badgeSampled(const std::string& param, double shownTov,
                         double nowUtc, bool clamped) {
    char buf[256];
    if (shownTov <= 0.0) {
        std::snprintf(buf, sizeof(buf),
                      "IRTAM %s: no data at this time (replay)",
                      param.c_str());
        return std::string(buf);
    }
    const double ageMin = (nowUtc - shownTov) / 60.0;
    if (clamped) {
        std::snprintf(buf, sizeof(buf),
                      "IRTAM %s | retrospectivo @ %s | %.0f min "
                      "(window edge)",
                      param.c_str(), formatTovShort(shownTov).c_str(), ageMin);
    } else {
        std::snprintf(buf, sizeof(buf), "IRTAM %s | retrospectivo @ %s | %.0f min",
                      param.c_str(), formatTovShort(shownTov).c_str(), ageMin);
    }
    return std::string(buf);
}

namespace {

LayerDataTime clampToRange(double epoch, double oldest, double newest) {
    LayerDataTime r;
    if (oldest < 0.0 || newest < 0.0 || newest < oldest) {
        r.dataEpoch = -1.0;
        r.stale = true;
        return r;
    }
    if (epoch <= oldest) {
        r.dataEpoch = oldest;
        r.stale = epoch < oldest;
        r.clamped = epoch < oldest;
        r.gapSec = 0.0;  // dato por delante del cursor: sin retraso
        return r;
    }
    if (epoch >= newest) {
        r.dataEpoch = newest;
        r.stale = epoch > newest;
        r.clamped = epoch > newest;
        r.gapSec = r.stale ? (epoch - newest) : 0.0;
        return r;
    }
    r.dataEpoch = epoch;
    return r;
}

}  // namespace

std::string staleGapText(double gapSec) {
    if (gapSec <= 0.0) return "";
    if (gapSec > 8640000.0) gapSec = 8640000.0;  // tope 100 d (anti-UB del cast)
    if (gapSec >= 3600.0) {
        const int t = (int)(gapSec / 360.0);  // decimas de hora (ints: sin %f)
        char b[32];
        std::snprintf(b, sizeof(b), "+%d.%d h", t / 10, t % 10);
        return std::string(b);
    }
    int minutes = (int)(gapSec / 60.0);
    if (minutes < 1) minutes = 1;
    char b[32];
    std::snprintf(b, sizeof(b), "+%d min", minutes);
    return std::string(b);
}

std::string staleSuffix(const LayerDataTime& ldt, bool samplerBusy) {
    if (!ldt.stale) return "";
    std::string s = "(stale";
    const std::string g = staleGapText(ldt.gapSec);
    if (!g.empty()) {
        s += ", ";
        s += g;
    }
    if (samplerBusy) s += ", worker";
    s += ")";
    return s;
}

LayerDataTime layerDataTime(int mapVariable, double replayEpoch, double nowUtc,
                            const LayerRanges& r, bool samplerBusy) {
    if (mapVariable >= 8 && mapVariable <= 11) {
        // IRTAM: ventana [now-96h, now-72h]; stale = clamp o worker ocupado.
        LayerDataTime out =
            clampToRange(replayEpoch, nowUtc - 604800.0, nowUtc - 259200.0);
        if (samplerBusy) out.stale = true;
        return out;
    }
    if (mapVariable >= 1 && mapVariable <= 7) {
        return clampToRange(replayEpoch, r.giroOldest, r.giroNewest);
    }
    if (mapVariable == 0) {
        return clampToRange(replayEpoch, r.tecOldest, r.tecNewest);
    }
    LayerDataTime none;
    none.stale = true;
    return none;
}

PairBracket pairBracketTovs(const std::vector<TovEntry>& entries,
                            const std::string& pA, const std::string& pB,
                            double epoch) {
    PairBracket r;
    std::vector<double> tovs;
    for (const auto& e : entries) {
        if (e.tovEpoch <= 0.0) continue;
        const bool isA = (e.param == pA);
        const bool isB = (e.param == pB);
        if (!isA && !isB) continue;
        const std::string& other = isA ? pB : pA;
        bool mate = false;
        for (const auto& o : entries) {
            if (o.param == other && o.tovEpoch == e.tovEpoch) {
                mate = true;
                break;
            }
        }
        if (mate) tovs.push_back(e.tovEpoch);
    }
    std::sort(tovs.begin(), tovs.end());
    tovs.erase(std::unique(tovs.begin(), tovs.end()), tovs.end());
    const TovBracket br = bracketTovs(tovs, epoch);
    if (br.empty) return r;
    r.t0 = tovs[br.i0];
    r.t1 = tovs[br.i1];
    r.f = br.f;
    r.clamped = br.clamped;
    r.empty = false;
    return r;
}

LayerLoopRange layerLoopRange(int mapVariable, double nowUtc,
                              const LayerRanges& r) {
    LayerLoopRange out;
    double oldest = -1.0, newest = -1.0;
    if (mapVariable == 0) {
        oldest = r.tecOldest;
        newest = r.tecNewest;
    } else if (mapVariable >= 1 && mapVariable <= 7) {
        oldest = r.giroOldest;
        newest = r.giroNewest;
    } else if (mapVariable >= 8 && mapVariable <= 11) {
        oldest = nowUtc - 604800.0;
        newest = nowUtc - 259200.0;
    } else {
        return out;
    }
    if (oldest > 0.0 && newest > oldest) {
        out.oldest = oldest;
        out.newest = newest;
        out.valid = true;
    }
    return out;
}

}  // namespace irtamstate
