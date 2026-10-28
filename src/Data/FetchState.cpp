// C2a: estado de la puerta de red (implementacion; ver FetchState.h).
#include "Data/FetchState.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fetchstate {
namespace {

constexpr const char* kHeader = "# fetch-state v1";

bool parseKeyValue(const std::string& line, std::string& key, std::string& val) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    key.clear();
    val.clear();
    for (size_t i = 0; i < eq; i++) {
        char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r') continue;
        key.push_back(c);
    }
    for (size_t i = eq + 1; i < line.size(); i++) {
        char c = line[i];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        val.push_back(c);
    }
    return !key.empty() && !val.empty();
}

double toNum(const std::string& v, double fallback) {
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || !(d == d)) return fallback;   // NaN != NaN
    return d;
}

int toInt(const std::string& v, int fallback) {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str()) return fallback;
    return static_cast<int>(n);
}

}  // namespace

bool save(const std::string& path, const fetchgate::State& st) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    if (ec) return false;

    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        // Precision completa: con los 6 digitos por defecto, un epoch de
        // 1.79e9 se redondeaba a pasos de ~1000 s y el backoff se corrompia.
        f << std::setprecision(17);
        f << kHeader << '\n';
        f << "last_attempt=" << st.lastAttemptUtc << '\n';
        f << "last_success=" << st.lastSuccessUtc << '\n';
        f << "blocked_until=" << st.blockedUntilUtc << '\n';
        f << "last_http=" << st.lastHttp << '\n';
        f << "last_curl=" << st.lastCurl << '\n';
        f << "retry_fails=" << st.retryFails << '\n';
        if (!f.good()) return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        // Windows no sobrescribe en rename: borrar y reintentar.
        fs::remove(p, ec);
        ec.clear();
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

bool load(const std::string& path, fetchgate::State& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    fetchgate::State st;
    bool sawAny = false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::string key, val;
        if (!parseKeyValue(line, key, val)) continue;
        if (key == "last_attempt") {
            st.lastAttemptUtc = toNum(val, st.lastAttemptUtc);
            sawAny = true;
        } else if (key == "last_success") {
            st.lastSuccessUtc = toNum(val, st.lastSuccessUtc);
            sawAny = true;
        } else if (key == "blocked_until") {
            st.blockedUntilUtc = toNum(val, st.blockedUntilUtc);
            sawAny = true;
        } else if (key == "last_http") {
            st.lastHttp = toInt(val, st.lastHttp);
            sawAny = true;
        } else if (key == "last_curl") {
            st.lastCurl = toInt(val, st.lastCurl);
            sawAny = true;
        } else if (key == "retry_fails") {
            st.retryFails = toInt(val, st.retryFails);
            if (st.retryFails < 0) st.retryFails = 0;
            sawAny = true;
        }
    }
    if (!sawAny) return false;
    out = st;
    return true;
}

}  // namespace fetchstate
