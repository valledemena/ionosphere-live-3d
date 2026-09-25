// M-prov-matrix (048): estado por proveedor, MODULO PURO (sin curl/GL/ImGui).
//
// Contrato: una fila por proveedor con UN estado (precedencia
// off > failed > rate-limited > degraded > stale > ok). "rate-limited" es
// SOLO-declarado: la app ya lo dice en otro sitio (badge GIRO/fallback,
// detector 429-duro) y la matriz lo repite, jamas lo inventa. El gate LGDC
// (cortesia propia, no 429 del servidor) es degraded, no rate-limited.
// Llama a PRODUCCION (provstatus), no replica.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace provstatus {

enum class State { Off, Failed, RateLimited, Degraded, Stale, Ok };

enum ProviderId {
    PROV_GIRO = 0,
    PROV_KC2G,
    PROV_IRTAM,
    PROV_TEC,
    PROV_INDICES,
    PROV_WIND,
    PROV_AURORA,
    PROV_KPHF,
    PROV_XRAY,
    PROV_SDO,
    PROV_DIAS,
    PROV_EBRO,
    PROV_ESA,
    PROV_NOAA_STUB,
    PROV_COUNT
};

struct Snapshot {
    ProviderId id = PROV_NOAA_STUB;
    bool hasData = false;
    double dataEpoch = 0.0;
    bool fetching = false;
    bool rateLimited = false;
    double retryInMin = 0.0;
    std::string data;
    std::string error;
};

struct Row {
    ProviderId id = PROV_NOAA_STUB;
    State state = State::Off;
    std::string provider;
    std::string stateText;
    std::string data;
    std::string reason;
};

const char* providerLabel(ProviderId id);

// Umbrales stale = 2x cadencia nominal (fijo, testeado). IRTAM: lag 72 h
// estructural + margen (73 h ok, 76 h stale); ESA/NOAA 0 = n/a.
extern const double kStaleSec[PROV_COUNT];

// Detector 429-duro (rondas con 0 ok): el escritor lo evalua y pone
// Snapshot::rateLimited; aqui vive la regla pura y testeable.
bool hard429(int roundAttempts, int roundOk);

State classify(const Snapshot& s, double nowUtc);
std::string stateText(State st, const Snapshot& s);
std::vector<Row> buildRows(const Snapshot* snaps, size_t n, double nowUtc);

}  // namespace provstatus
