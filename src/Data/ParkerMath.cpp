// Lote 5 / modulo D: espiral de Parker (implementacion; ver .h).
//
// Solo geometria: sin GL, sin red, sin ImGui. Las constantes y las formulas
// son las del demo web (parker.ts) para que el panel 3D y el web coincidan.
#include "Data/ParkerMath.h"

#include <cmath>

namespace parker {

const double SIDEREAL_ROTATION_DAYS = 25.38;
const double AU_KM = 1.496e8;

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegPerRad = 180.0 / kPi;
constexpr double kRInnerAu = 0.05;
constexpr double kROuterAu = 1.5;
constexpr double kMaxGapSec = 2.0 * 3600.0;  // hueco maximo con cruce valido
}  // namespace

double spiralLagDeg(double vKms, double rAu) {
    if (!std::isfinite(vKms) || !(vKms > 0.0)) return 0.0;
    if (!std::isfinite(rAu)) return 0.0;
    const double transitDays = (rAu * AU_KM) / vKms / 86400.0;
    const double lag = (transitDays / SIDEREAL_ROTATION_DAYS) * 360.0;
    return std::isfinite(lag) ? lag : 0.0;
}

std::vector<Pt> spiralPoints(double vKms, int n) {
    if (n < 1) n = 1;
    const double lag1 = spiralLagDeg(vKms, 1.0);
    std::vector<Pt> pts;
    pts.reserve((size_t)n + 1u);
    for (int i = 0; i <= n; ++i) {
        Pt p;
        p.r = kRInnerAu + ((kROuterAu - kRInnerAu) * (double)i) / (double)n;
        p.phi = lag1 - spiralLagDeg(vKms, p.r);
        const double rad = p.phi / kDegPerRad;
        p.x = p.r * std::cos(rad);
        p.y = p.r * std::sin(rad);
        // Blindaje: con v>0 todo es finito; con v<=0 phi=0 y r<=1.5.
        if (!std::isfinite(p.x)) p.x = 0.0;
        if (!std::isfinite(p.y)) p.y = 0.0;
        pts.push_back(p);
    }
    return pts;
}

const char* sectorPolarity(float bxNt) {
    // 0 exacto y NaN caen en "toward" (regla declarada: bx >= 0).
    return (bxNt >= 0.0f) ? "toward" : "away";
}

std::vector<double> findCrossings(const std::vector<BxSample>& series) {
    std::vector<double> out;
    for (size_t i = 1; i < series.size(); ++i) {
        const BxSample& a = series[i - 1];
        const BxSample& b = series[i];
        if (!std::isfinite(a.timeUtc) || !std::isfinite(b.timeUtc)) continue;
        if (!std::isfinite(a.bx) || !std::isfinite(b.bx)) continue;
        const double dt = b.timeUtc - a.timeUtc;
        if (!(dt > 0.0) || dt > kMaxGapSec) continue;  // no ascendente o hueco
        // 0 exacto cuenta como >= 0 ("toward"): solo cambia el signo real.
        const bool aToward = (a.bx >= 0.0f);
        const bool bToward = (b.bx >= 0.0f);
        if (aToward != bToward) out.push_back(b.timeUtc);
    }
    return out;
}

}  // namespace parker
