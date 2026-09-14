// M-IRTAM-F2 (P3): gate lgdc compartido (ver LgdcPacing.h).
#include "Data/LgdcPacing.h"

namespace lgdcpacing {
namespace {

// Choke point unico de la app: ultimo lanzamiento lgdc en ms (0 = ninguno).
// Function-local static (Meyers): una sola instancia en todo el proceso,
// compartida por catalogo + getbest + gambit.
std::atomic<long long>& gate() {
    static std::atomic<long long> last{0};
    return last;
}

}  // namespace

void recordLaunch(long long nowMs) {
    // CAS-max: solo avanzar. Un worker tardio no retrasa el reloj.
    long long cur = gate().load(std::memory_order_relaxed);
    while (nowMs > cur) {
        if (gate().compare_exchange_weak(cur, nowMs,
                                         std::memory_order_relaxed)) {
            break;
        }
    }
}

long long lastLaunchMs() {
    return gate().load(std::memory_order_relaxed);
}

long long msSinceLastLaunch(long long nowMs) {
    return nowMs - lastLaunchMs();
}

bool canLaunchGambit(long long nowMs) {
    return msSinceLastLaunch(nowMs) >= kGambitGapMs;
}

void resetForTest() {
    gate().store(0, std::memory_order_relaxed);
}

}  // namespace lgdcpacing
