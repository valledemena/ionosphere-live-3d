// C2a: puerta de red (implementacion; ver FetchGate.h).
#include "Data/FetchGate.h"

#include <algorithm>

namespace fetchgate {
namespace {

// Retry-After absurdo (servidor roto o cabecera basura): no bloquear mas de un
// dia. Por debajo de 1 min tampoco tiene sentido respetarlo.
constexpr double kRetryAfterMaxSec = 24.0 * 3600.0;
constexpr double kRetryAfterMinSec = 60.0;

}  // namespace

Decision decide(double nowUtc, const State& st, double cacheAgeSec, double jitterSec) {
    Decision d;

    const double blockedUntil = sanitizeBlockedUntil(st.blockedUntilUtc, nowUtc);
    if (blockedUntil > nowUtc) {
        d.action = Action::Blocked;
        d.fetchAtUtc = blockedUntil;
        d.reason = "bloqueado por la ultima respuesta";
        return d;
    }

    const bool haveCache = cacheAgeSec >= 0.0;

    // Regla dura: una cache joven no se toca y NO se pide. Es lo que evita el
    // 403 de la segunda descarga dentro de la misma actualizacion de 2 h.
    if (haveCache && cacheAgeSec < kLiveTtlSec) {
        d.action = Action::CacheOnly;
        d.fetchAtUtc = 0.0;
        d.reason = "cache viva (<6 h)";
        return d;
    }

    // Fallo blando reciente (transporte o parseo): respetar el backoff. La
    // cache vieja se sigue sirviendo mientras tanto.
    if (st.retryFails > 0 && st.lastAttemptUtc > 0.0) {
        const double backoff = transportBackoffSec(st.retryFails);
        const double retryAt = st.lastAttemptUtc + backoff;
        if (nowUtc < retryAt) {
            d.action = Action::CacheOnly;
            d.fetchAtUtc = retryAt;
            d.reason = "espera tras fallo de red";
            return d;
        }
    }

    if (haveCache && cacheAgeSec < kMaxFallbackAgeSec) {
        const double jitter = std::min(std::max(jitterSec, 0.0), kJitterMaxSec);
        d.action = Action::FetchJittered;
        d.fetchAtUtc = nowUtc + jitter;
        d.reason = (jitter > 0.0) ? "cache vieja, reintento programado" : "cache vieja";
        return d;
    }

    d.action = Action::FetchNow;
    d.fetchAtUtc = nowUtc;
    d.reason = haveCache ? "cache caducada (>72 h)" : "sin cache";
    return d;
}

double sanitizeBlockedUntil(double blockedUntilUtc, double nowUtc) {
    if (!(blockedUntilUtc > 0.0)) return 0.0;
    const double maxUntil = nowUtc + kBlockSkewSec;
    if (blockedUntilUtc > maxUntil) return maxUntil;
    return blockedUntilUtc;
}

double transportBackoffSec(int consecutiveFails) {
    switch (consecutiveFails) {
        case 0: return 0.0;
        case 1: return 5.0 * 60.0;
        case 2: return 10.0 * 60.0;
        case 3: return 20.0 * 60.0;
        case 4: return 40.0 * 60.0;
        default: return 60.0 * 60.0;
    }
}

Outcome classify(long httpCode, int curlCode, double retryAfterSec) {
    Outcome out;
    out.retryAfterSec = retryAfterSec;

    if (curlCode != 0) {
        out.transport = true;
        out.label = "error de transporte";
        return out;
    }
    if (httpCode == 200) {
        out.label = "200";
        return out;
    }
    if (httpCode == 0) {
        // Sin codigo y sin error de curl: respuesta vacia o abortada.
        out.transport = true;
        out.label = "sin respuesta";
        return out;
    }

    out.block = true;
    if (httpCode == 301 || httpCode == 302 || httpCode == 303 || httpCode == 307 ||
        httpCode == 308) {
        out.label = "URL obsoleta (3xx)";
    } else if (httpCode == 403) {
        out.label = "403 (cuota o firewall)";
    } else if (httpCode == 404) {
        out.label = "404 (URL o grupo inexistente)";
    } else if (httpCode == 429) {
        out.label = "429 (rate limit)";
    } else if (httpCode >= 500) {
        out.label = "5xx (servidor)";
    } else {
        out.label = "HTTP distinto de 200";
    }
    return out;
}

double blockDurationSec(const Outcome& out) {
    if (out.retryAfterSec >= kRetryAfterMinSec) {
        return std::min(out.retryAfterSec, kRetryAfterMaxSec);
    }
    return kBlockSec;
}

}  // namespace fetchgate
