// M-IRTAM-F2 (P5): estados honestos (ver IrtamState.h).
#include "Data/IrtamState.h"

#include <cmath>
#include <cstdio>

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

}  // namespace irtamstate
