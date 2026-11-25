#pragma once

#include <cmath>
#include <string>
#include <vector>
#include "Data/IrtamCoeffCache.h"
#include "Data/IrtamCoeffParse.h"
#include "Data/IrtamGridEval.h"
#include "Data/IrtamState.h"
#include "Ionosphere/GridLayer.h"

// M-irtam-replay-028: entrega del BRACKET del par (endpoints para lerp en
// render por cursor — simetria TEC). Lee del cache (solo-lectura), no toca
// el publish. pA/pB en minusculas ("fof2"/"b0"...). Rellena el bracket
// (GridData 45x46 + timestamp + valid en cada endpoint) + t0/t1/clamped.
// Nunca lanza. Hilo: SOLO worker (ver sampleIrtamBracketWorker).
static bool evalTovGrid(double tov, const std::string& path, double ryear,
                        std::vector<float>& out) {
    irtamccache::Bucket bk;
    if (!irtamccache::loadBucket(path, bk)) return false;
    irtamcoeff::Message m;
    std::string err;
    if (!irtamcoeff::parseMessage(
            std::string(bk.bytes.begin(), bk.bytes.end()), m, err) ||
        !irtamcoeff::isStandardBasis(m.header))
        return false;
    const long long day = (long long)std::floor(tov / 86400.0);
    const float h = (float)((tov - (double)day * 86400.0) / 3600.0);
    return irtameval::evalGrid(m.coeffs, ryear, h, h, out);
}

// M-irtam-replay-028: lerp en render por cursor sobre el bracket entregado
// (simetria TEC; microsegundos, wait-free). f clampado a [0,1]; a fallo
// devuelve grid invalido (ausente honesto).
static GridData lerpBracketGrid(const std::vector<float>& v0,
                                const std::vector<float>& v1, double t0,
                                double t1, double epoch) {
    GridData g;
    double f = (t1 > t0) ? (epoch - t0) / (t1 - t0) : 0.0;
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    if (!irtamstate::lerpValues(v0, v1, f, g.values)) return GridData{};
    g.width = irtameval::kGridW;
    g.height = irtameval::kGridH;
    g.timestamp = epoch;
    g.valid = true;
    return g;
}
