#include "Data/ProviderStatus.h"

#include <cstdio>

namespace provstatus {

namespace {

const char* kLabels[PROV_COUNT] = {
    "GIRO", "kc2g", "IRTAM", "GloTEC", "Indices", "Solar wind", "Aurora",
    "Kp/HF", "X-ray", "SDO", "DIAS", "Ebro", "ESA", "NOAA",
};

bool startsWithGate(const std::string& e) {
    return e.size() >= 5 && e.compare(0, 5, "gate:") == 0;
}

std::string ageCompact(double ageSec) {
    if (ageSec < 0.0) ageSec = 0.0;
    char b[32];
    if (ageSec < 120.0) {
        std::snprintf(b, sizeof(b), "%ds", (int)ageSec);
    } else if (ageSec < 7200.0) {
        std::snprintf(b, sizeof(b), "%d min", (int)(ageSec / 60.0));
    } else if (ageSec < 172800.0) {
        const int t = (int)(ageSec / 360.0);  // decimas de hora (ints: sin %f)
        std::snprintf(b, sizeof(b), "%d.%d h", t / 10, t % 10);
    } else {
        std::snprintf(b, sizeof(b), "%d d", (int)(ageSec / 86400.0));
    }
    return std::string(b);
}

}  // namespace

const char* providerLabel(ProviderId id) {
    const int i = (int)id;
    if (i < 0 || i >= (int)PROV_COUNT) return "?";
    return kLabels[i];
}

const double kStaleSec[PROV_COUNT] = {
    900.0,    // GIRO: cadencia ~5-15 min
    21600.0,  // kc2g: gridUsable 6 h
    270000.0,  // IRTAM: lag 72 h estructural + margen (73 h ok, 76 h stale)
    1200.0,   // TEC: cadencia 10 min
    7200.0,   // indices
    1800.0,   // viento
    7200.0,   // aurora
    1800.0,   // Kp-HF
    1800.0,   // X-ray
    1800.0,   // SDO: 15 min
    7200.0,   // DIAS
    7200.0,   // Ebro
    0.0,      // ESA: n/a (mock/off por diseno)
    0.0,      // NOAA stub: n/a (off permanente)
};

bool hard429(int roundAttempts, int roundOk) {
    return roundAttempts >= 3 && roundOk == 0;
}

State classify(const Snapshot& s, double nowUtc) {
    (void)nowUtc;
    if (s.id == PROV_NOAA_STUB) return State::Off;
    if (!s.hasData) {
        if (s.error.empty()) return State::Off;
        return State::Failed;
    }
    if (s.rateLimited) return State::RateLimited;
    if (s.retryInMin > 0.0 || startsWithGate(s.error)) return State::Degraded;
    const double ks = (s.id >= 0 && (int)s.id < (int)PROV_COUNT)
                          ? kStaleSec[(int)s.id]
                          : 0.0;
    if (ks > 0.0 && s.dataEpoch > 0.0 && (nowUtc - s.dataEpoch) > ks)
        return State::Stale;
    return State::Ok;
}

std::string stateText(State st, const Snapshot& s) {
    const char* w = "ok";
    switch (st) {
        case State::Off: w = "off"; break;
        case State::Failed: w = "failed"; break;
        case State::RateLimited: w = "rate-limited"; break;
        case State::Degraded: w = "degraded"; break;
        case State::Stale: w = "stale"; break;
        case State::Ok: w = "ok"; break;
    }
    std::string t(w);
    if (s.fetching) t += " (fetching)";
    return t;
}

std::vector<Row> buildRows(const Snapshot* snaps, size_t n,
                           double nowUtc) {
    std::vector<Row> rows;
    rows.reserve((size_t)PROV_COUNT);
    for (int i = 0; i < (int)PROV_COUNT; i++) {
        const Snapshot* f = nullptr;
        for (size_t k = 0; k < n; k++) {
            if (snaps[k].id == (ProviderId)i) {
                f = &snaps[k];
                break;
            }
        }
        Row r;
        r.id = (ProviderId)i;
        r.provider = providerLabel(r.id);
        if (f == nullptr) {
            r.state = State::Off;
            r.stateText = stateText(r.state, Snapshot());
            rows.push_back(r);
            continue;
        }
        r.state = classify(*f, nowUtc);
        r.stateText = stateText(r.state, *f);
        r.data = f->data;
        if (f->hasData && f->dataEpoch > 0.0)
            r.data += " · " + ageCompact(nowUtc - f->dataEpoch);
        if (!f->error.empty())
            r.reason = f->error;
        else if (f->retryInMin > 0.0) {
            char b[32];
            std::snprintf(b, sizeof(b), "retry %d min",
                          (int)(f->retryInMin + 0.5));
            r.reason = b;
        }
        rows.push_back(r);
    }
    return rows;
}

}  // namespace provstatus
