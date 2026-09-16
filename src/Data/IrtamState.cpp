// M-IRTAM-F2 (P5): estados honestos (ver IrtamState.h).
#include "Data/IrtamState.h"

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
                          "IRTAM %s: sin datos (%s; reintento pasivo)",
                          param.c_str(), error.c_str());
        } else {
            std::snprintf(buf, sizeof(buf),
                          "IRTAM %s: sin datos (backfill en curso)",
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
    if (ageSec >= 259200.0 && ageSec <= 345600.0) return ReplayZone::IrtamOnly;
    if (ageSec > 86400.0 && ageSec < 259200.0) return ReplayZone::DeadGap;
    return ReplayZone::TecOnly;
}

const char* zoneName(ReplayZone z) {
    switch (z) {
        case ReplayZone::IrtamOnly: return "solo-IRTAM [T-96,T-72]";
        case ReplayZone::DeadGap:
            return "hueco estructural [T-72,T-24] (lag E11)";
        default: return "solo-TEC [T-24,T]";
    }
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
                      "IRTAM %s: sin datos en este instante (replay)",
                      param.c_str());
        return std::string(buf);
    }
    const double ageMin = (nowUtc - shownTov) / 60.0;
    if (clamped) {
        std::snprintf(buf, sizeof(buf),
                      "IRTAM %s | retrospectivo @ %s | %.0f min "
                      "(borde de ventana)",
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
        return r;
    }
    if (epoch >= newest) {
        r.dataEpoch = newest;
        r.stale = epoch > newest;
        r.clamped = epoch > newest;
        return r;
    }
    r.dataEpoch = epoch;
    return r;
}

}  // namespace

LayerDataTime layerDataTime(int mapVariable, double replayEpoch, double nowUtc,
                            const LayerRanges& r, bool samplerBusy) {
    if (mapVariable >= 8 && mapVariable <= 11) {
        // IRTAM: ventana [now-96h, now-72h]; stale = clamp o worker ocupado.
        LayerDataTime out =
            clampToRange(replayEpoch, nowUtc - 345600.0, nowUtc - 259200.0);
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

}  // namespace irtamstate
