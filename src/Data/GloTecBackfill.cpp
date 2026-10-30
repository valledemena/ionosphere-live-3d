#include "Data/GloTecBackfill.h"

#include <algorithm>
#include <cmath>

namespace glotbf {

namespace {

bool isCached(double epoch, const std::vector<double>& cached) {
    for (double e : cached) {
        if (e == epoch) return true;
    }
    return false;
}

// Frames de la ventana [now-72 h, now] de NUEVO a VIEJO, cortados a depth.
// El borde usa `<=`: un frame exactamente a 72 h queda fuera, igual que la
// poda del anillo (432 frames), para no pedir uno que se podaria al instante.
std::vector<IndexFrame> horizon(const std::vector<IndexFrame>& index,
                                double nowUtc, int depthSlots) {
    const int depth = depthSlots > 0 ? depthSlots : kDepth6h;
    std::vector<IndexFrame> out;
    if (index.empty() || depth <= 0) return out;

    const double oldest = nowUtc - kWindowSec;
    for (const IndexFrame& f : index) {
        if (!(f.timeUtc > 0.0) || !std::isfinite(f.timeUtc)) continue;
        if (f.timeUtc <= oldest) continue;
        out.push_back(f);
    }
    std::sort(out.begin(), out.end(),
              [](const IndexFrame& a, const IndexFrame& b) {
                  return a.timeUtc > b.timeUtc;  // nuevo -> viejo
              });
    if ((int)out.size() > depth) out.resize((size_t)depth);
    return out;
}

}  // namespace

std::vector<IndexFrame> plan(const std::vector<IndexFrame>& index,
                             const std::vector<double>& cachedEpochs,
                             double nowUtc, int depthSlots) {
    const std::vector<IndexFrame> h = horizon(index, nowUtc, depthSlots);
    std::vector<IndexFrame> out;
    out.reserve(h.size());
    for (const IndexFrame& f : h) {
        if (isCached(f.timeUtc, cachedEpochs)) continue;
        out.push_back(f);
    }
    return out;
}

Result run(const std::vector<IndexFrame>& index,
           const std::vector<double>& cachedEpochs,
           double nowUtc, const Options& opt,
           const Transport& transport, const Store& store,
           const Sleep& sleepFn, const Stop& stop) {
    Result r;

    const std::vector<IndexFrame> todo =
        plan(index, cachedEpochs, nowUtc, opt.depthSlots);
    // skippedCached = frames del horizonte que ya estaban (no reduce el
    // horizonte, solo se saltan).
    const std::vector<IndexFrame> h = horizon(index, nowUtc, opt.depthSlots);
    r.skippedCached = (int)h.size() - (int)todo.size();

    if (todo.empty()) return r;

    int consec404 = 0;
    int consec429 = 0;
    int consecTransport = 0;
    double pendingWait = 0.0;
    bool first = true;

    for (const IndexFrame& f : todo) {
        if (stop && stop()) { r.stoppedByCaller = true; break; }

        if (!first) {
            double wait = opt.minIntervalSec;
            if (pendingWait > wait) wait = pendingWait;
            if (wait > 0.0) { sleepFn(wait); r.sleptSec += wait; }
        } else if (pendingWait > 0.0) {
            sleepFn(pendingWait);
            r.sleptSec += pendingWait;
        }
        pendingWait = 0.0;
        first = false;

        r.requested.push_back(f.timeUtc);
        const Response res = transport(f.url);
        // Bytes de red REALES de ESTA peticion: se cuenta el cuerpo recibido
        // sea 200 o error (una pagina de error tambien gasta cuota); un
        // fallo de transporte puro trae cuerpo vacio y cuenta 0.
        r.netBytes += (long long)res.body.size();

        if (!res.transportOk) {
            ++r.httpErrors;
            if (++consecTransport >= opt.stopAfter404) {
                r.stoppedOnTransport = true;
                break;
            }
            continue;
        }
        consecTransport = 0;

        if (res.http == 200) {
            if (store(f, res.body)) r.stored.push_back(f.timeUtc);
            else ++r.httpErrors;
            consec404 = 0;
            consec429 = 0;
            continue;
        }
        if (res.http == 404) {
            ++r.notFound;
            consec429 = 0;
            if (++consec404 >= opt.stopAfter404) { r.stoppedOn404 = true; break; }
            continue;
        }
        if (res.http == 429) {
            ++r.rateLimited;
            ++r.httpErrors;
            consec404 = 0;
            // Retry-After por encima del tope: mejor parar que recortarlo y
            // volver a llamar a un servidor que pide horas de espera.
            if (res.retryAfterSec > opt.maxWaitSec) {
                r.stoppedOnRetryAfter = true;
                break;
            }
            if (++consec429 >= opt.stopAfter404) { r.stoppedOn429 = true; break; }
            pendingWait = std::max(pendingWait, res.retryAfterSec);
            continue;
        }
        if (res.http >= 500) {
            ++r.httpErrors;
            r.stoppedOn5xx = true;
            break;
        }
        ++r.httpErrors;
        consec404 = 0;
        consec429 = 0;
    }
    return r;
}

}  // namespace glotbf
