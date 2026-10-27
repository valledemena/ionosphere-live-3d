#include "Data/ProviderStatus.h"

#include <cstdio>

namespace provstatus {

namespace {

const char* kLabels[PROV_COUNT] = {
    "GIRO", "kc2g", "IRTAM", "GloTEC", "Indices", "Solar wind", "Aurora",
    "Kp/HF", "X-ray", "SDO", "DIAS", "Ebro", "ESA", "NOAA",
    "WSPRnet", "ENLIL", "NWS alerts",
    "Satellites", "LASCO C2", "D-RAP", "DONKI flares",
    "AE (Kyoto)", "Voyager", "DONKI RBE",
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
    // Lote 3: dataEpoch = ultimo fetch correcto (no la fecha del evento), con
    // el mismo criterio de 2x la cadencia nominal del adaptador.
    600.0,    // WSPRnet: cadencia 5 min (TTL de spot 10 min)
    3600.0,   // ENLIL: cadencia 30 min (el modelo publica a su ritmo)
    600.0,    // NWS alerts: cadencia 5 min
    // Lote 4: mismo criterio (2x la cadencia nominal del adaptador).
    21600.0,  // TLE CelesTrak: cadencia 1 h (los TLE cambian despacio)
    1800.0,   // LASCO C2: cadencia 15 min
    1800.0,   // D-RAP: cadencia 15 min
    3600.0,   // DONKI flares: cadencia 30 min
    // Lote 5. AE: su dataEpoch es el ultimo FETCH correcto, no la muestra
    // (Kyoto publica con hasta ~3 semanas de retraso por diseno).
    900.0,    // AE (Kyoto): cadencia 15 min
    43200.0,  // Voyager: TTL 6 h -> stale a las 12 h
    3600.0,   // DONKI RBE: cadencia 30 min
};

bool hard429(int roundAttempts, int roundOk) {
    return roundAttempts >= 3 && roundOk == 0;
}

State classify(const Snapshot& s, double nowUtc) {
    (void)nowUtc;
    if (s.id == PROV_NOAA_STUB) return State::Off;
    // El gate de cortesia (LGDC) es espera declarada, no un fallo: tambien
    // cuando aun no hay dato (antes caia en "failed" y contradecia el
    // contrato de la matriz).
    const bool gate = startsWithGate(s.error);
    if (!s.hasData) {
        if (s.error.empty()) return State::Off;
        return gate ? State::Degraded : State::Failed;
    }
    if (s.rateLimited) return State::RateLimited;
    if (s.retryInMin > 0.0 || gate) return State::Degraded;
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
        // Motivo = causa declarada + espera, no una u otra: un backoff parcial
        // (algunas estaciones) con reintento pendiente necesita ambas.
        if (!f->error.empty()) r.reason = f->error;
        if (f->retryInMin > 0.0) {
            char b[40];
            std::snprintf(b, sizeof(b), "retry %d min",
                          (int)(f->retryInMin + 0.5));
            if (!r.reason.empty()) r.reason += " · ";
            r.reason += b;
        }
        rows.push_back(r);
    }
    return rows;
}

}  // namespace provstatus
