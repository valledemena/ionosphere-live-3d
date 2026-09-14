// M-IRTAM-F2 (P1a): parse del mensaje IRTAM Coefficients (ver IrtamCoeffParse.h).
#include "Data/IrtamCoeffParse.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace irtamcoeff {
namespace {

bool isLeap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

// Howard Hinnant days_from_civil (dominio publico): dias desde 1970-01-01.
long long daysFromCivil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned mp = (unsigned)((m + 9) % 12);
    const unsigned doy = (153 * mp + 2) / 5 + (unsigned)(d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        --b;
    return s.substr(a, b - a);
}

std::vector<std::string> splitLines(const std::string& t) {
    std::vector<std::string> v;
    std::size_t p = 0;
    for (;;) {
        std::size_t q = t.find('\n', p);
        if (q == std::string::npos) {
            v.push_back(t.substr(p));
            break;
        }
        v.push_back(t.substr(p, q - p));
        p = q + 1;
    }
    return v;
}

std::vector<std::string> splitWs(const std::string& s) {
    std::vector<std::string> v;
    std::size_t p = 0;
    while (p < s.size()) {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r'))
            ++p;
        if (p >= s.size()) break;
        std::size_t q = p;
        while (q < s.size() && s[q] != ' ' && s[q] != '\t' && s[q] != '\r')
            ++q;
        v.push_back(s.substr(p, q - p));
        p = q;
    }
    return v;
}

// Token numerico estricto: charset [0-9+-.eE] + strtod con consumo total +
// finitud (rechaza "nan"/"inf"/hex). Acepta F16.8 y cientifica (dual, ruling).
bool parseNumber(const std::string& tok, double& out) {
    if (tok.empty() || tok.size() > 64) return false;
    for (char c : tok) {
        const bool ok = (c >= '0' && c <= '9') || c == '+' || c == '-' ||
                        c == '.' || c == 'e' || c == 'E';
        if (!ok) return false;
    }
    const char* beg = tok.c_str();
    char* end = nullptr;
    const double v = std::strtod(beg, &end);
    if (end != beg + tok.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool parseIsoFields(const std::string& s, int& Y, int& M, int& D, int& h,
                    int& mi, int& sec, double& frac, int& tzMin) {
    // "YYYY-MM-DDTHH:MM:SS[.frac](Z|±hh:mm)" — separadores estrictos.
    if (s.size() < 20) return false;
    if (s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' ||
        s[16] != ':')
        return false;
    auto digits = [&](std::size_t p, int n, int& v) {
        if (p + (std::size_t)n > s.size()) return false;
        v = 0;
        for (int i = 0; i < n; i++) {
            if (s[p + i] < '0' || s[p + i] > '9') return false;
            v = v * 10 + (s[p + i] - '0');
        }
        return true;
    };
    if (!digits(0, 4, Y) || !digits(5, 2, M) || !digits(8, 2, D) ||
        !digits(11, 2, h) || !digits(14, 2, mi) || !digits(17, 2, sec))
        return false;
    std::size_t p = 19;
    frac = 0.0;
    if (p < s.size() && s[p] == '.') {
        ++p;
        double div = 10.0;
        bool any = false;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
            frac += (s[p] - '0') / div;
            div *= 10.0;
            ++p;
            any = true;
        }
        if (!any) return false;
    }
    tzMin = 0;
    if (p < s.size()) {
        if (s[p] == 'Z' || s[p] == 'z') {
            ++p;
        } else if (s[p] == '+' || s[p] == '-') {
            const int sign = s[p] == '+' ? 1 : -1;
            int th = 0, tm = 0;
            if (!digits(p + 1, 2, th)) return false;
            p += 3;
            if (p < s.size() && s[p] == ':') ++p;
            if (!digits(p, 2, tm)) return false;
            p += 2;
            tzMin = sign * (th * 60 + tm);
        } else {
            return false;
        }
    }
    if (p != s.size()) return false;  // trailing = corrupto
    static const int kMlen[12] = {31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
    if (Y < 1900 || Y > 2200 || M < 1 || M > 12 || h > 23 || mi > 59 ||
        sec > 59)
        return false;
    int mlen = kMlen[M - 1] + (M == 2 && isLeap(Y) ? 1 : 0);
    if (D < 1 || D > mlen) return false;
    return true;
}

// Marcadores con "#" opcional + espacio (el servlet emite "# START_HEADER").
bool isMarker(const std::string& line, const char* name) {
    std::string t = trim(line);
    if (!t.empty() && t[0] == '#') t = trim(t.substr(1));
    return t == name;
}

}  // namespace

bool parseIsoUtc(const std::string& iso, double& epochOut) {
    int Y = 0, M = 0, D = 0, h = 0, mi = 0, sec = 0, tzMin = 0;
    double frac = 0.0;
    if (!parseIsoFields(trim(iso), Y, M, D, h, mi, sec, frac, tzMin))
        return false;
    const long long days = daysFromCivil(Y, M, D);
    epochOut = (double)days * 86400.0 + h * 3600.0 + mi * 60.0 + sec + frac -
               tzMin * 60.0;
    return true;
}

bool isStandardBasis(const Header& h) {
    return h.nTemporal == 14 && h.nSpatial == 76;
}

bool parseMessage(const std::string& text, Message& out, std::string& err) {
    out = Message{};
    err.clear();
    if (text.empty() || text.size() > 8u * 1024u * 1024u) {
        err = "vacio o > 8 MB";
        return false;
    }
    const std::vector<std::string> lines = splitLines(text);
    std::size_t i = 0;
    while (i < lines.size() && trim(lines[i]).empty()) ++i;
    if (i >= lines.size() || !isMarker(lines[i], "START_HEADER")) {
        err = "sin #START_HEADER";
        return false;
    }
    ++i;
    std::vector<std::string> head, body;
    bool closed = false;
    for (; i < lines.size(); i++) {
        if (isMarker(lines[i], "END_HEADER")) {
            closed = true;
            ++i;
            break;
        }
        head.push_back(trim(lines[i]));
    }
    if (!closed) {
        err = "sin #END_HEADER";
        return false;
    }
    for (; i < lines.size(); i++) {
        const std::string t = trim(lines[i]);
        if (!t.empty()) body.push_back(t);
    }
    Header h;
    bool haveChar = false, haveTov = false, haveBasis = false,
         haveGrid = false;
    bool inStations = false;
    for (const std::string& ln : head) {
        if (ln.find("Ionospheric Characteristic:") != std::string::npos) {
            const std::vector<std::string> w = splitWs(
                ln.substr(ln.find(':') + 1));
            if (w.empty()) {
                err = "characteristic vacia";
                return false;
            }
            h.characteristic = w[0];
            haveChar = true;
        } else if (ln.find("Time of Validity") != std::string::npos) {
            const std::vector<std::string> w = splitWs(
                ln.substr(ln.find("Validity") + 8));
            if (w.empty() || !parseIsoUtc(w[0], h.tovEpoch)) {
                err = "TOV invalido";
                return false;
            }
            haveTov = true;
        } else if (ln.find("Basis Lengths:") != std::string::npos) {
            int nt = 0, ns = 0;
            if (std::sscanf(ln.substr(ln.find(':') + 1).c_str(),
                            "%d(temporal) x %d(spatial)", &nt, &ns) != 2 ||
                nt <= 0 || ns <= 0 || nt > 64 || ns > 4096) {
                err = "Basis Lengths invalido";
                return false;
            }
            h.nTemporal = nt;
            h.nSpatial = ns;
            haveBasis = true;
        } else if (ln.find("Earth Grid:") != std::string::npos) {
            int la = 0, lo = 0;
            if (std::sscanf(ln.substr(ln.find(':') + 1).c_str(),
                            "%d lats x %d lons", &la, &lo) != 2 || la <= 0 ||
                lo <= 0) {
                err = "Earth Grid invalido";
                return false;
            }
            h.nLats = la;
            h.nLons = lo;
            haveGrid = true;
        } else if (ln.find("Assimilative Engine:") != std::string::npos) {
            h.engine = trim(ln.substr(ln.find(':') + 1));
        } else if (ln.find("Generated by") != std::string::npos &&
                   ln.find(" on ") != std::string::npos) {
            h.generatedUtc = trim(ln.substr(ln.find(" on ") + 4));
        } else if (ln.find("Assimilated stations:") != std::string::npos) {
            inStations = true;
        } else if (inStations) {
            if (ln.find("Requested by") != std::string::npos) continue;
            const std::string body2 =
                ln.size() > 0 && ln[0] == '#' ? trim(ln.substr(1)) : ln;
            h.stationCount += (int)splitWs(body2).size();
        }
    }
    if (!haveChar) {
        err = "falta Characteristic";
        return false;
    }
    if (!haveTov) {
        err = "falta TOV";
        return false;
    }
    if (!haveBasis) {
        err = "falta Basis Lengths";
        return false;
    }
    if (!haveGrid) {
        err = "falta Earth Grid";
        return false;
    }
    const long long expect =
        (long long)h.nTemporal * (long long)h.nSpatial;
    std::vector<double> coeffs;
    coeffs.reserve((std::size_t)expect);
    for (const std::string& ln : body) {
        if (!ln.empty() && ln[0] == '#') continue;  // comentario tardio
        for (const std::string& tok : splitWs(ln)) {
            double v = 0.0;
            if (!parseNumber(tok, v)) {
                err = "token no-numerico";
                return false;
            }
            coeffs.push_back(v);
            if ((long long)coeffs.size() > expect) {
                err = "sobran coeficientes";
                return false;
            }
        }
    }
    if ((long long)coeffs.size() != expect) {
        err = "cuenta != nT*nS";
        return false;
    }
    out.header = h;
    out.coeffs = std::move(coeffs);
    out.valid = true;
    return true;
}

}  // namespace irtamcoeff
