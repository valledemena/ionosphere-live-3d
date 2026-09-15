// Ciclo instrumentación-stderr: traza LGDC (ver LgdcTrace.h).
#include "Data/LgdcTrace.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace lgdctrace {
namespace {

std::mutex& mtx() {
    static std::mutex m;
    return m;
}
std::function<void(const std::string&)>& sink() {
    static std::function<void(const std::string&)> s;
    return s;
}
bool& enabled() {
    static bool e = true;
    return e;
}

long long nowMsUtc() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

std::string formatIsoMs(long long msSinceEpoch) {
    const std::time_t sec = (std::time_t)(msSinceEpoch / 1000);
    const int ms = (int)(msSinceEpoch % 1000);
    std::tm g{};
#ifdef _WIN32
    gmtime_s(&g, &sec);
#else
    gmtime_r(&sec, &g);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour,
                  g.tm_min, g.tm_sec, ms < 0 ? 0 : ms);
    return std::string(buf);
}

void trace(const std::string& category, const std::string& detail) {
    std::lock_guard<std::mutex> lk(mtx());
    if (!enabled()) return;
    const std::string line =
        "[LGDC " + formatIsoMs(nowMsUtc()) + "] " + category + " " + detail;
    const auto& s = sink();
    if (s) {
        s(line);
        return;
    }
    std::fprintf(stderr, "%s\n", line.c_str());
}

void setSink(std::function<void(const std::string&)> s) {
    std::lock_guard<std::mutex> lk(mtx());
    sink() = std::move(s);
}

void setEnabled(bool on) {
    std::lock_guard<std::mutex> lk(mtx());
    enabled() = on;
}

}  // namespace lgdctrace
