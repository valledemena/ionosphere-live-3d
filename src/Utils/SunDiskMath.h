#pragma once
// =============================================================================
// M11: matematica del disco solar (plan §13.3 verbatim + recetas GLM).
//
// Modulo PURO header-only: sin GL, sin curl, sin App. Testeable en CI
// (tests/test_sdo_projection.cpp). Convenciones:
//   - L0 Carrington con nombre propio (leccion kPulseRateHz: el L0 local de
//     SolarPosition.cpp es la longitud media λm de Meeus, OTRA cosa).
//   - Este-positiva; W de Location = negativo (plan §13.3).
// =============================================================================
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace sundisk {

inline constexpr double kCarringtonJD = 2399101.5;   // CR1 (plan §13.3)
inline constexpr double kCarringtonRotDays = 27.2753;
inline constexpr double kSolarNodeDeg = 75.77;       // nodo ascendente Ω
inline constexpr double kSolarTiltDeg = 7.25;        // inclinacion

inline double wrap360(double d) {
    d = std::fmod(d, 360.0);
    if (d < 0.0) d += 360.0;
    return d;
}

inline double wrap180(double d) {
    d = wrap360(d);
    if (d > 180.0) d -= 360.0;
    return d;
}

// JD desde epoch unix.
inline double unixToJD(double utcSeconds) {
    return utcSeconds / 86400.0 + 2440587.5;
}

// L0: longitud Carrington del meridiano central (deg [0,360), crece).
inline double carringtonL0(double jd) {
    double f = std::fmod(jd - kCarringtonJD, kCarringtonRotDays) / kCarringtonRotDays;
    if (f < 0.0) f += 1.0;
    return f * 360.0;
}

// B0: latitud heliografica del centro del disco (deg, |B0| <= 7.25).
inline double solarB0(double lambdaSDeg) {
    const double kDeg = 3.14159265358979323846 / 180.0;
    return std::asin(std::sin(kSolarTiltDeg * kDeg) *
                     std::sin((lambdaSDeg - kSolarNodeDeg) * kDeg)) / kDeg;
}

struct SpotProj {
    double x = 0.0, y = 0.0;  // disco unidad; E-W invertido (plan §13.3)
    bool visible = false;     // |Δlon| <= 90
};

// Ortografica al disco unidad con inversion E-W: x = -ρ·sin(Δlon).
inline SpotProj projectSpot(double latDeg, double dLonDeg, double b0Deg) {
    const double kDeg = 3.14159265358979323846 / 180.0;
    const double lat = latDeg * kDeg, dl = dLonDeg * kDeg, b0 = b0Deg * kDeg;
    const double rho = std::cos(lat);
    SpotProj p;
    p.x = -rho * std::sin(dl);
    p.y = std::sin(lat) * std::cos(b0) - rho * std::sin(b0) * std::cos(dl);
    p.visible = std::fabs(dLonDeg) <= 90.0;
    return p;
}

// "S12W45" -> lat=-12, dLon=-45 (W negativo). 1-2 digitos lat, 1-3 lon.
inline bool parseLocation(const std::string& loc, double& latDeg, double& dLonDeg) {
    if (loc.size() < 4) return false;
    const char ns = loc[0];
    if (ns != 'N' && ns != 'S') return false;
    const size_t e = loc.find_first_of("EW", 1);
    if (e == std::string::npos || e < 2 || e + 1 >= loc.size()) return false;
    const char ew = loc[e];
    int la = 0, lo = 0;
    try {
        la = std::stoi(loc.substr(1, e - 1));
        lo = std::stoi(loc.substr(e + 1));
    } catch (...) {
        return false;
    }
    if (la < 0 || la > 90 || lo < 0 || lo > 180) return false;
    latDeg = (ns == 'S' ? -1.0 : 1.0) * la;
    dLonDeg = (ew == 'W' ? -1.0 : 1.0) * lo;
    return true;
}

struct Sunspot {
    int region = 0;
    double lat = 0.0;      // heliografica (Location) o fallback
    double lonCarr = 0.0;  // Carrington (campo Carlon; fallback si no hay Location)
    bool hasLonCarr = false;
    double dlon = 0.0;     // Δlon directa del campo Longitude (W negativo)
    bool hasDlon = false;
    std::string loc;       // "S12W45" (puede venir vacio -> fallback)
    int numspot = 0;       // manchas del grupo (tooltip)
    double timeUtc = 0.0;  // time_tag (seen)
};

// M12/N6: regla de edad — Location caduca a ~13.2°/dia. Filtro 48 h;
// >12 h prefiere Carlon (estable en rotacion) sobre Location.
inline constexpr double kMaxSpotAgeS = 172800.0;      // 48 h
inline constexpr double kCarlonPreferAgeS = 43200.0;  // 12 h

inline bool isSpotFresh(double timeUtc, double nowUtc) {
    return (nowUtc - timeUtc) <= kMaxSpotAgeS;
}

inline std::vector<Sunspot> filterFreshSpots(const std::vector<Sunspot>& v,
                                             double nowUtc) {
    std::vector<Sunspot> out;
    for (const auto& s : v)
        if (isSpotFresh(s.timeUtc, nowUtc)) out.push_back(s);
    return out;
}

struct SpotPos {
    double lat = 0.0, dlon = 0.0;
    bool ok = false;
};

// Posicion usable: fresca + Location; >12 h prefiere Carlon; fallback dlon;
// sin nada posicionable -> ok=false.
inline SpotPos resolveSpot(const Sunspot& s, double l0deg, double nowUtc) {
    SpotPos r;
    if (!isSpotFresh(s.timeUtc, nowUtc)) return r;
    const double age = nowUtc - s.timeUtc;
    double la = 0.0, dl = 0.0;
    if (s.hasLonCarr && age > kCarlonPreferAgeS) {
        la = s.lat;
        dl = wrap180(s.lonCarr - l0deg);
    } else if (!s.loc.empty() && parseLocation(s.loc, la, dl)) {
        // Location Stonyhurst al momento del reporte (fresca).
    } else if (s.hasDlon) {
        la = s.lat;
        dl = s.dlon;
    } else if (s.hasLonCarr) {
        la = s.lat;
        dl = wrap180(s.lonCarr - l0deg);
    } else {
        return r;
    }
    r.lat = la;
    r.dlon = dl;
    r.ok = true;
    return r;
}

// Ultimo reporte por Region (campo seen del report).
inline std::map<int, Sunspot> latestByRegion(const std::vector<Sunspot>& v) {
    std::map<int, Sunspot> m;
    for (const auto& s : v) {
        auto it = m.find(s.region);
        if (it == m.end() || s.timeUtc > it->second.timeUtc) m[s.region] = s;
    }
    return m;
}

}  // namespace sundisk
