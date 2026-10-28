// C2a: estado de la puerta de red (implementacion; ver FetchState.h).
#include "Data/FetchState.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace fetchstate {
namespace {

constexpr const char* kHeader = "# fetch-state v2";

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

bool save(const std::string& path, const std::string& key, const fetchgate::State& st) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p = fs::u8path(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    if (ec) return false;

    const std::string prefix = key.empty() ? std::string() : (key + ".");

    // Lectura-modificacion-escritura: el fichero guarda varias puertas
    // (celestrak.active, celestrak.satnogs), asi que solo se reemplazan las
    // claves de `key` y se conservan las demas tal cual. Reescribir el fichero
    // entero borraria la otra puerta.
    std::vector<std::string> keep;
    {
        std::ifstream in(p, std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::string k, val;
            if (!parseKeyValue(line, k, val)) continue;
            if (!prefix.empty() && k.compare(0, prefix.size(), prefix) == 0) continue;
            keep.push_back(line);
        }
    }

    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        // Precision completa: con los 6 digitos por defecto, un epoch de
        // 1.79e9 se redondeaba a pasos de ~1000 s y el backoff se corrompia.
        f << std::setprecision(17);
        f << kHeader << '\n';
        for (const std::string& line : keep) f << line << '\n';
        f << prefix << "last_attempt=" << st.lastAttemptUtc << '\n';
        f << prefix << "last_success=" << st.lastSuccessUtc << '\n';
        f << prefix << "blocked_until=" << st.blockedUntilUtc << '\n';
        f << prefix << "last_http=" << st.lastHttp << '\n';
        f << prefix << "last_curl=" << st.lastCurl << '\n';
        f << prefix << "retry_fails=" << st.retryFails << '\n';
        if (!f.good()) return false;
    }
    fs::rename(fs::u8path(tmp), p, ec);
    if (ec) {
        // Windows no sobrescribe en rename: borrar y reintentar.
        fs::remove(p, ec);
        ec.clear();
        fs::rename(fs::u8path(tmp), p, ec);
        if (ec) {
            fs::remove(fs::u8path(tmp), ec);
            return false;
        }
    }
    return true;
}

bool load(const std::string& path, const std::string& key, fetchgate::State& out) {
    namespace fs = std::filesystem;
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;

    const std::string prefix = key.empty() ? std::string() : (key + ".");
    fetchgate::State st;
    bool sawAny = false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::string k, val;
        if (!parseKeyValue(line, k, val)) continue;
        if (!prefix.empty()) {
            if (k.compare(0, prefix.size(), prefix) != 0) continue;
            k = k.substr(prefix.size());
        }
        if (k == "last_attempt") {
            st.lastAttemptUtc = toNum(val, st.lastAttemptUtc);
            sawAny = true;
        } else if (k == "last_success") {
            st.lastSuccessUtc = toNum(val, st.lastSuccessUtc);
            sawAny = true;
        } else if (k == "blocked_until") {
            st.blockedUntilUtc = toNum(val, st.blockedUntilUtc);
            sawAny = true;
        } else if (k == "last_http") {
            st.lastHttp = toInt(val, st.lastHttp);
            sawAny = true;
        } else if (k == "last_curl") {
            st.lastCurl = toInt(val, st.lastCurl);
            sawAny = true;
        } else if (k == "retry_fails") {
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
