#include "OrbitProp.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

// Port propio del modelo de la demo web (sgp4/sgpCoord.ts) SIN SGP4: aqui
// solo elementos medios + J2 secular. La documentacion de las formulas y de
// los limites vive en OrbitProp.h.

namespace orbitprop {

namespace {

// Dias desde 1970-01-01 (Howard Hinnant). Valido para fechas modernas.
long long daysFromCivil(int y, int m, int d) {
    y -= (m <= 2) ? 1 : 0;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

// Quita CR/LF y espacios finales (el TLE estandar mide 69 columnas).
std::string trimLine(const std::string& in) {
    size_t n = in.size();
    while (n > 0) {
        char c = in[n - 1];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') --n;
        else break;
    }
    return in.substr(0, n);
}

// Lee un campo numerico de ancho fijo. false si hay basura o si esta vacio.
bool readField(const std::string& s, size_t start, size_t len, double& out) {
    if (start + len > s.size()) return false;
    const std::string tok = s.substr(start, len);
    const char* begin = tok.c_str();
    char* end = nullptr;
    double v = std::strtod(begin, &end);
    if (end == begin) return false;
    while (end != nullptr && *end == ' ') ++end;
    if (end == nullptr || *end != '\0') return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

// Normaliza un angulo (grados) a [0, 360).
double normDeg(double d) {
    double r = std::fmod(d, 360.0);
    if (r < 0.0) r += 360.0;
    return r;
}

}  // namespace

bool tleChecksumOk(const std::string& line) {
    const std::string s = trimLine(line);
    if (s.size() < (size_t)kTleLineLen) return false;
    int sum = 0;
    for (int k = 0; k < kTleLineLen - 1; ++k) {
        const char c = s[(size_t)k];
        if (c >= '0' && c <= '9') sum += (c - '0');
        else if (c == '-') sum += 1;
    }
    const char check = s[(size_t)(kTleLineLen - 1)];
    if (check < '0' || check > '9') return false;
    return (sum % 10) == (check - '0');
}

double tleEpochToUtc(int yy, double dayOfYear) {
    // Pivote estandar del TLE (57-99 -> 19YY, 00-56 -> 20YY).
    const int year = (yy >= 57) ? (1900 + yy) : (2000 + yy);
    const long long days = daysFromCivil(year, 1, 1);
    return (double)days * 86400.0 + (dayOfYear - 1.0) * 86400.0;
}

bool parseTle(const std::string& line1, const std::string& line2, OrbitElements& out) {
    const std::string l1 = trimLine(line1);
    const std::string l2 = trimLine(line2);

    // Longitud exacta del estandar y prefijos "1 " / "2 ".
    if (l1.size() != (size_t)kTleLineLen || l2.size() != (size_t)kTleLineLen) return false;
    if (l1[0] != '1' || l1[1] != ' ') return false;
    if (l2[0] != '2' || l2[1] != ' ') return false;
    // El numero de catalogo debe coincidir entre las dos lineas.
    if (l1.compare(2, 5, l2, 2, 5) != 0) return false;
    if (!tleChecksumOk(l1) || !tleChecksumOk(l2)) return false;

    // Epoca: YY + DDD.DDDDDDDD (las dos primeras cifras son el ano, no una
    // parte decimal: no se puede leer el campo entero como un solo numero).
    const std::string epochField = l1.substr(18, 14);
    if (epochField.size() != 14) return false;
    for (size_t i = 0; i < epochField.size(); ++i) {
        const char c = epochField[i];
        const bool dot = (i == 5) && (c == '.');
        const bool digit = (c >= '0' && c <= '9');
        if (!dot && !digit) return false;
    }
    const int yy = std::atoi(epochField.substr(0, 2).c_str());
    const double doy = std::atof(epochField.substr(2, 12).c_str());

    double inc = 0.0, raan = 0.0, ecc = 0.0, argp = 0.0, ma = 0.0, mm = 0.0;
    // Columnas 1-based -> indices 0-based.
    if (!readField(l2, 8, 8, inc)) return false;     // inclinacion
    if (!readField(l2, 17, 8, raan)) return false;   // RAAN
    if (!readField(l2, 26, 7, ecc)) return false;    // excentricidad implicita
    ecc *= 1.0e-7;                                   // "0006932" -> 0.0006932
    if (!readField(l2, 34, 8, argp)) return false;   // argumento del perigeo
    if (!readField(l2, 43, 8, ma)) return false;     // anomalia media
    if (!readField(l2, 52, 11, mm)) return false;    // movimiento medio

    if (!(yy >= 0 && yy <= 99)) return false;
    if (!(doy > 0.0 && doy < 367.0)) return false;
    if (!(inc >= 0.0 && inc <= 180.0)) return false;
    if (!(ecc >= 0.0 && ecc < 1.0)) return false;
    // Un movimiento medio fuera de (0, 20] rev/dia no es un satelite terrestre.
    if (!(mm > 0.0 && mm <= 20.0)) return false;

    OrbitElements el;
    el.epochUtc = tleEpochToUtc(yy, doy);
    el.incDeg = inc;
    el.raanDeg = normDeg(raan);
    el.ecc = ecc;
    el.argpDeg = normDeg(argp);
    el.meanAnomDeg = normDeg(ma);
    el.meanMotionRevDay = mm;
    el.valid = true;
    out = el;   // solo se escribe si TODO ha ido bien (sin basura a medias)
    return true;
}

double gmstRad(double utc) {
    if (!std::isfinite(utc)) return 0.0;
    const double jd = utc / 86400.0 + 2440587.5;
    const double t = (jd - 2451545.0) / 36525.0;
    double temp = -6.2e-6 * t * t * t + 0.093104 * t * t +
                  (876600.0 * 3600.0 + 8640184.812866) * t + 67310.54841;
    temp = std::fmod(temp * kDeg / 240.0, 2.0 * kPi);
    if (temp < 0.0) temp += 2.0 * kPi;
    return temp;
}

double semiMajorAxisKm(const OrbitElements& el) {
    if (!(el.meanMotionRevDay > 0.0)) return 0.0;
    const double n = el.meanMotionRevDay * 2.0 * kPi / 86400.0;
    return std::cbrt(kMu / (n * n));
}

double orbitalPeriodMin(const OrbitElements& el) {
    if (!(el.meanMotionRevDay > 0.0)) return 0.0;
    return 1440.0 / el.meanMotionRevDay;
}

double meanAltitudeKm(const OrbitElements& el) {
    const double a = semiMajorAxisKm(el);
    if (!(a > 0.0)) return 0.0;
    return a - kEarthRadiusKm;
}

double j2RaanDotDegPerDay(const OrbitElements& el) {
    if (!(el.meanMotionRevDay > 0.0) || !(el.ecc >= 0.0 && el.ecc < 1.0)) return 0.0;
    const double n = el.meanMotionRevDay * 2.0 * kPi / 86400.0;
    const double a = std::cbrt(kMu / (n * n));
    const double p = a * (1.0 - el.ecc * el.ecc);
    if (!(p > 0.0)) return 0.0;
    const double inc = el.incDeg * kDeg;
    const double k = 1.5 * kJ2 * (kEarthRadiusKm / p) * (kEarthRadiusKm / p) * n;
    return (k * -std::cos(inc)) * 86400.0 / kDeg;
}

bool solveKepler(double meanAnomRad, double ecc, double& eccAnomRad) {
    if (!std::isfinite(meanAnomRad)) return false;
    if (!(ecc >= 0.0 && ecc < 1.0)) return false;

    double m = std::fmod(meanAnomRad, 2.0 * kPi);
    if (m > kPi) m -= 2.0 * kPi;
    if (m < -kPi) m += 2.0 * kPi;

    double e = m + ecc * std::sin(m);   // semilla clasica
    for (int it = 0; it < 40; ++it) {
        const double f = e - ecc * std::sin(e) - m;
        const double fp = 1.0 - ecc * std::cos(e);
        if (!(std::fabs(fp) > 1e-12)) return false;
        const double step = f / fp;
        e -= step;
        if (std::fabs(step) < 1e-13) break;
    }
    if (std::fabs(e - ecc * std::sin(e) - m) > 1e-9) return false;
    eccAnomRad = e;
    return true;
}

bool propagateTeme(const OrbitElements& el, double utc, double rKm[3]) {
    if (!el.valid) return false;
    if (!std::isfinite(utc)) return false;
    if (!(el.meanMotionRevDay > 0.0 && el.meanMotionRevDay <= 20.0)) return false;
    if (!(el.ecc >= 0.0 && el.ecc < 1.0)) return false;

    const double n = el.meanMotionRevDay * 2.0 * kPi / 86400.0;   // rad/s
    const double a = std::cbrt(kMu / (n * n));
    const double e = el.ecc;
    const double inc = el.incDeg * kDeg;
    const double sini = std::sin(inc);
    const double cosi = std::cos(inc);

    // Deriva secular J2 (unidades rad/s).
    const double p = a * (1.0 - e * e);
    const double k = 1.5 * kJ2 * (kEarthRadiusKm / p) * (kEarthRadiusKm / p) * n;
    const double raanDot = -k * cosi;
    const double argpDot = k * (2.0 - 2.5 * sini * sini);
    const double mDot = n + k * std::sqrt(1.0 - e * e) * (1.0 - 1.5 * sini * sini);

    const double dt = utc - el.epochUtc;
    const double raan = el.raanDeg * kDeg + raanDot * dt;
    const double argp = el.argpDeg * kDeg + argpDot * dt;
    const double ma = el.meanAnomDeg * kDeg + mDot * dt;

    double eccAnom = 0.0;
    if (!solveKepler(ma, e, eccAnom)) return false;

    const double cosE = std::cos(eccAnom);
    const double sinE = std::sin(eccAnom);
    const double r = a * (1.0 - e * cosE);
    const double nu = std::atan2(std::sqrt(1.0 - e * e) * sinE, cosE - e);
    const double xp = r * std::cos(nu);
    const double yp = r * std::sin(nu);

    const double cr = std::cos(raan), sr = std::sin(raan);
    const double ca = std::cos(argp), sa = std::sin(argp);

    rKm[0] = (cr * ca - sr * sa * cosi) * xp + (-cr * sa - sr * ca * cosi) * yp;
    rKm[1] = (sr * ca + cr * sa * cosi) * xp + (-sr * sa + cr * ca * cosi) * yp;
    rKm[2] = (sa * sini) * xp + (ca * sini) * yp;
    return std::isfinite(rKm[0]) && std::isfinite(rKm[1]) && std::isfinite(rKm[2]);
}

void temeToEcef(const double rTemeKm[3], double gmst, double rEcefKm[3]) {
    const double c = std::cos(gmst);
    const double s = std::sin(gmst);
    rEcefKm[0] = c * rTemeKm[0] + s * rTemeKm[1];
    rEcefKm[1] = -s * rTemeKm[0] + c * rTemeKm[1];
    rEcefKm[2] = rTemeKm[2];
}

bool propagateEcef(const OrbitElements& el, double utc, double rKm[3]) {
    double rTeme[3] = {0.0, 0.0, 0.0};
    if (!propagateTeme(el, utc, rTeme)) return false;
    temeToEcef(rTeme, gmstRad(utc), rKm);
    return true;
}

void ecefToGeodetic(const double rEcefKm[3], double& latDeg, double& lonDeg, double& altKm) {
    // Hirvonen iterativo, WGS84 (5 iteraciones, como sgpCoord.ts).
    const double a = kEarthRadiusKm;
    const double f = 1.0 / 298.257223563;
    const double e2 = 2.0 * f - f * f;
    const double x = rEcefKm[0], y = rEcefKm[1], z = rEcefKm[2];
    const double p = std::sqrt(x * x + y * y);
    const double lon = std::atan2(y, x);

    if (p == 0.0) {
        // Polo exacto: Hirvonen divide por cero.
        latDeg = (z >= 0.0) ? 90.0 : -90.0;
        lonDeg = 0.0;
        altKm = std::fabs(z) - a * (1.0 - f);
        return;
    }

    double lat = std::atan2(z, p * (1.0 - e2));
    double alt = 0.0;
    for (int i = 0; i < 5; ++i) {
        const double sinLat = std::sin(lat);
        const double nn = a / std::sqrt(1.0 - e2 * sinLat * sinLat);
        alt = p / std::cos(lat) - nn;
        lat = std::atan2(z, p * (1.0 - (e2 * nn) / (nn + alt)));
    }
    latDeg = lat / kDeg;
    lonDeg = lon / kDeg;
    altKm = alt;
}

void latLonAltToScene(double latDeg, double lonDeg, double altKm,
                      double& x, double& y, double& z) {
    // Convencion del motor, identica a RefShells/GridLayer/WsprArcs.
    const double phi = kPi * 0.5 - latDeg * kDeg;
    const double theta = kPi - lonDeg * kDeg;
    const double r = 1.0 + altKm / kEngineRadiusKm;
    const double sp = std::sin(phi);
    x = r * sp * std::cos(theta);
    y = r * std::cos(phi);
    z = r * sp * std::sin(theta);
}

bool toScene(const OrbitElements& el, double utc,
             double& x, double& y, double& z, double& altKm) {
    double rEcef[3] = {0.0, 0.0, 0.0};
    if (!propagateEcef(el, utc, rEcef)) return false;
    double lat = 0.0, lon = 0.0, alt = 0.0;
    ecefToGeodetic(rEcef, lat, lon, alt);
    if (!std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(alt)) return false;
    latLonAltToScene(lat, lon, alt, x, y, z);
    altKm = alt;
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

}  // namespace orbitprop
