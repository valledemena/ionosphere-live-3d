#include "Data/GloTecBackfill.h"

#include <algorithm>
#include <cmath>

namespace glotbf {

std::vector<IndexFrame> plan(const std::vector<IndexFrame>& index,
                             const std::vector<double>& cachedEpochs,
                             double nowUtc, int depthSlots) {
    const int depth = depthSlots > 0 ? depthSlots : kDepth6h;
    std::vector<IndexFrame> out;
    if (index.empty() || depth <= 0) return out;

    const double oldest = nowUtc - kWindowSec;
    std::vector<IndexFrame> inWindow;
    inWindow.reserve(index.size());
    for (const IndexFrame& f : index) {
        if (!(f.timeUtc > 0.0) || !std::isfinite(f.timeUtc)) continue;
        if (f.timeUtc < oldest) continue;
        inWindow.push_back(f);
    }
    std::sort(inWindow.begin(), inWindow.end(),
              [](const IndexFrame& a, const IndexFrame& b) {
                  return a.timeUtc > b.timeUtc;  // nuevo -> viejo
              });

    out.reserve((size_t)depth);
    for (const IndexFrame& f : inWindow) {
        bool cached = false;
        for (double e : cachedEpochs) {
            if (e == f.timeUtc) { cached = true; break; }
        }
        if (cached) continue;
        out.push_back(f);
        if ((int)out.size() >= depth) break;
    }
    return out;
}

Result run(const std::vector<IndexFrame>& index,
           const std::vector<double>& cachedEpochs,
           double nowUtc, const Options& opt,
           const Transport& transport, const Store& store,
           const Sleep& sleepFn, const Stop& stop) {
    Result r;

    // skippedCached cuenta TODOS los frames de la ventana ya cacheados, no
    // solo los que habrian entrado en la profundidad pedida.
    {
        const double oldest = nowUtc - kWindowSec;
        for (const IndexFrame& f : index) {
            if (!(f.timeUtc > 0.0) || f.timeUtc < oldest) continue;
            for (double e : cachedEpochs) {
                if (e == f.timeUtc) { ++r.skippedCached; break; }
            }
        }
    }

    const std::vector<IndexFrame> todo =
        plan(index, cachedEpochs, nowUtc, opt.depthSlots);
    if (todo.empty()) return r;

    int consec404 = 0;
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

        if (!res.transportOk) {
            ++r.httpErrors;
            if (++consec404 >= opt.stopAfter404) { r.stoppedOn404 = true; break; }
            continue;
        }
        if (res.http == 200) {
            if (store(f, res.body)) r.stored.push_back(f.timeUtc);
            else ++r.httpErrors;
            consec404 = 0;
            continue;
        }
        if (res.http == 404) {
            ++r.notFound;
            if (++consec404 >= opt.stopAfter404) { r.stoppedOn404 = true; break; }
            continue;
        }
        if (res.http == 429) {
            ++r.httpErrors;
            consec404 = 0;
            double wait = res.retryAfterSec > 0.0 ? res.retryAfterSec : 0.0;
            if (wait > opt.maxWaitSec) wait = opt.maxWaitSec;
            pendingWait = std::max(pendingWait, wait);
            continue;
        }
        if (res.http >= 500) {
            ++r.httpErrors;
            r.stoppedOn5xx = true;
            break;
        }
        ++r.httpErrors;
        consec404 = 0;
    }
    return r;
}

}  // namespace glotbf
