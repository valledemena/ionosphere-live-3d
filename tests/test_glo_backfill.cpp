// =============================================================================
// test_glo_backfill - C1b: planificador del backfill GloTEC con reloj y
// transporte INYECTADOS. Sin red, sin curl, sin disco.
//
// Llama a PRODUCCION (namespace glotbf en src/Data/GloTecBackfill), no replica.
// Cubre:
//   (1) orden estricto nuevo -> viejo,
//   (2) frames ya cacheados no se piden,
//   (3) 3 x 404 consecutivos paran y NO escriben bloqueo (no hay estado de
//       puerta en este modulo: basta con no almacenar nada),
//   (4) un 404 aislado se cuenta y se salta,
//   (5) 429 con Retry-After espera al menos N,
//   (6) un 5xx para,
//   (7) ritmo >= 1 s entre peticiones,
//   (8) profundidad 36/144/432 limita las peticiones,
//   (9) stop() interrumpe antes de la siguiente peticion.
// =============================================================================
#include "Data/GloTecBackfill.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int g_ok = 0, g_fail = 0;

static void check_bool(const char* what, bool cond) {
    if (cond) ++g_ok; else ++g_fail;
    std::printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
}

static void check_int(const char* what, int got, int want) {
    check_bool(what, got == want);
    if (got != want) std::printf("      got %d want %d\n", got, want);
}

// Indice sintetico: N frames de 10 min acabando en t0 (el mas nuevo).
static const double kStep = 600.0;
static std::vector<glotbf::IndexFrame> makeIndex(double t0, int n) {
    std::vector<glotbf::IndexFrame> idx;
    idx.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        glotbf::IndexFrame f;
        f.timeUtc = t0 - i * kStep;
        f.url = "/f/" + std::to_string((long long)f.timeUtc);
        idx.push_back(f);
    }
    return idx;
}

static bool strictlyDescending(const std::vector<double>& v) {
    for (size_t i = 1; i < v.size(); ++i)
        if (!(v[i] < v[i - 1])) return false;
    return true;
}

// Transporte guionizado: devuelve status segun la URL (por epoch).
struct Script {
    std::vector<double> order;          // epochs pedidos, en orden
    std::vector<double> sleepEvents;    // segundos de cada sleep
    int calls = 0;
};

static glotbf::Transport scriptedTransport(
    Script& s, const std::vector<double>& st404,
    const std::vector<double>& st5xx) {
    return [&s, st404, st5xx](const std::string& url) {
        const double epoch = std::atof(url.c_str() + 3);  // "/f/<epoch>"
        s.order.push_back(epoch);
        s.calls++;
        glotbf::Response r;
        r.transportOk = true;
        r.http = 200;
        r.body = "ok";
        for (double e : st404) if (e == epoch) r.http = 404;
        for (double e : st5xx) if (e == epoch) r.http = 500;
        return r;
    };
}

int main() {
    const double t0 = 1792937906.0;

    std::printf("--- C1b: orden nuevo -> viejo y dedup contra cache ---\n");
    {
        auto idx = makeIndex(t0, 48);  // 8 h
        std::vector<double> cached = {t0 - 5 * kStep, t0 - 20 * kStep};
        glotbf::Options opt;
        opt.depthSlots = 36;
        Script s;
        int stored = 0;
        auto tr = scriptedTransport(s, {}, {});
        auto store = [&stored](const glotbf::IndexFrame&, const std::string&) {
            ++stored;
            return true;
        };
        std::vector<double> sleeps;
        auto sleepFn = [&sleeps](double sec) { sleeps.push_back(sec); };
        glotbf::Result r = glotbf::run(idx, cached, t0, opt, tr, store, sleepFn, {});
        check_bool("pide 36 (profundidad 6 h)", (int)r.requested.size() == 36);
        check_bool("primer pedido es el mas nuevo",
                   !r.requested.empty() && r.requested.front() == t0);
        check_bool("orden estricto nuevo -> viejo", strictlyDescending(r.requested));
        bool cachedAsked = false;
        for (double e : r.requested)
            if (e == t0 - 5 * kStep || e == t0 - 20 * kStep) cachedAsked = true;
        check_bool("frames cacheados no se piden", !cachedAsked);
        check_int("skippedCached cuenta los dos", r.skippedCached, 2);
        check_int("los 36 se almacenan", (int)r.stored.size(), 36);
        check_int("store llamado 36 veces", stored, 36);
    }

    std::printf("--- C1b: 3 x 404 consecutivos paran sin almacenar ---\n");
    {
        auto idx = makeIndex(t0, 48);
        glotbf::Options opt;
        opt.depthSlots = 36;
        // Los tres mas nuevos dan 404.
        std::vector<double> notFound = {t0, t0 - kStep, t0 - 2 * kStep};
        Script s;
        auto tr = scriptedTransport(s, notFound, {});
        auto store = [](const glotbf::IndexFrame&, const std::string&) { return true; };
        auto sleepFn = [](double) {};
        glotbf::Result r = glotbf::run(idx, {}, t0, opt, tr, store, sleepFn, {});
        check_bool("para por 404", r.stoppedOn404);
        check_int("solo 3 peticiones", (int)r.requested.size(), 3);
        check_int("3 404 contados", r.notFound, 3);
        check_int("nada almacenado", (int)r.stored.size(), 0);
        check_bool("no para por 5xx", !r.stoppedOn5xx);
    }

    std::printf("--- C1b: 404 aislado se cuenta y se salta ---\n");
    {
        auto idx = makeIndex(t0, 6);
        glotbf::Options opt;
        opt.depthSlots = 6;
        std::vector<double> notFound = {t0 - 2 * kStep};
        Script s;
        auto tr = scriptedTransport(s, notFound, {});
        int stored = 0;
        auto store = [&stored](const glotbf::IndexFrame&, const std::string&) {
            ++stored;
            return true;
        };
        auto sleepFn = [](double) {};
        glotbf::Result r = glotbf::run(idx, {}, t0, opt, tr, store, sleepFn, {});
        check_bool("no para", !r.stoppedOn404);
        check_int("6 peticiones", (int)r.requested.size(), 6);
        check_int("1 404", r.notFound, 1);
        check_int("5 almacenados", stored, 5);
    }

    std::printf("--- C1b: 429 con Retry-After espera al menos N ---\n");
    {
        auto idx = makeIndex(t0, 4);
        glotbf::Options opt;
        opt.depthSlots = 4;
        Script s;
        // El segundo pedido (t0 - step) responde 429 con Retry-After: 7.
        auto tr = [&s, t0](const std::string& url) {
            const double epoch = std::atof(url.c_str() + 3);
            s.order.push_back(epoch);
            glotbf::Response r;
            r.http = 200;
            r.body = "ok";
            if (epoch == t0 - kStep) { r.http = 429; r.retryAfterSec = 7.0; }
            return r;
        };
        auto store = [](const glotbf::IndexFrame&, const std::string&) { return true; };
        std::vector<double> sleeps;
        auto sleepFn = [&sleeps](double sec) { sleeps.push_back(sec); };
        glotbf::Result r = glotbf::run(idx, {}, t0, opt, tr, store, sleepFn, {});
        check_bool("sigue tras el 429", !r.stoppedOn404 && !r.stoppedOn5xx);
        check_int("4 peticiones", (int)r.requested.size(), 4);
        bool honoured = false;
        for (double sec : sleeps) if (sec >= 7.0) honoured = true;
        check_bool("espera >= Retry-After", honoured);
        check_bool("la espera va antes del siguiente pedido",
                   sleeps.size() >= 2 && sleeps[1] >= 7.0);
    }

    std::printf("--- C1b: 5xx para ---\n");
    {
        auto idx = makeIndex(t0, 10);
        glotbf::Options opt;
        opt.depthSlots = 10;
        Script s;
        auto tr = scriptedTransport(s, {}, {t0 - kStep});
        auto store = [](const glotbf::IndexFrame&, const std::string&) { return true; };
        auto sleepFn = [](double) {};
        glotbf::Result r = glotbf::run(idx, {}, t0, opt, tr, store, sleepFn, {});
        check_bool("para por 5xx", r.stoppedOn5xx);
        check_int("2 peticiones (200 + 500)", (int)r.requested.size(), 2);
    }

    std::printf("--- C1b: ritmo ~1 req/s con reloj inyectado ---\n");
    {
        auto idx = makeIndex(t0, 5);
        glotbf::Options opt;
        opt.depthSlots = 5;
        Script s;
        auto tr = scriptedTransport(s, {}, {});
        auto store = [](const glotbf::IndexFrame&, const std::string&) { return true; };
        std::vector<double> sleeps;
        double total = 0.0;
        auto sleepFn = [&](double sec) { sleeps.push_back(sec); total += sec; };
        glotbf::Result r = glotbf::run(idx, {}, t0, opt, tr, store, sleepFn, {});
        check_int("5 peticiones", (int)r.requested.size(), 5);
        check_int("4 esperas (una por peticion salvo la primera)", (int)sleeps.size(), 4);
        bool allOne = true;
        for (double sec : sleeps) if (std::fabs(sec - 1.0) > 1e-9) allOne = false;
        check_bool("espera 1 s entre peticiones", allOne);
        check_bool("total 4 s", std::fabs(total - 4.0) < 1e-9);
        check_bool("sleptSec coincide", std::fabs(r.sleptSec - 4.0) < 1e-9);
    }

    std::printf("--- C1b: profundidad 36/144/432 ---\n");
    {
        auto idx = makeIndex(t0, 500);  // > 72 h
        const int depths[3] = {36, 144, 432};
        for (int d : depths) {
            glotbf::Options opt;
            opt.depthSlots = d;
            Script s;
            auto tr = scriptedTransport(s, {}, {});
            auto store = [](const glotbf::IndexFrame&, const std::string&) { return true; };
            auto sleepFn = [](double) {};
            glotbf::Result r = glotbf::run(idx, {}, t0, opt, tr, store, sleepFn, {});
            check_int(d == 36 ? "36 peticiones" : d == 144 ? "144 peticiones"
                                                          : "432 peticiones",
                      (int)r.requested.size(), d);
        }
        // La ventana de 72 h recorta: con 500 frames (83 h) no se piden los
        // mas viejos aunque depthSlots sea mayor que los de la ventana.
        std::vector<glotbf::IndexFrame> wide = makeIndex(t0, 500);
        auto p432 = glotbf::plan(wide, {}, t0, 432);
        check_int("432 dentro de 72 h", (int)p432.size(), 432);
        check_bool("el ultimo planificado es el de 72 h",
                   !p432.empty() && std::fabs(p432.back().timeUtc - (t0 - 431 * kStep)) < 1e-9);
        auto p500 = glotbf::plan(wide, {}, t0, 500);
        check_int("la ventana 72 h limita aunque se pidan 500", (int)p500.size(), 433);
    }

    std::printf("--- C1b: stop() interrumpe ---\n");
    {
        auto idx = makeIndex(t0, 10);
        glotbf::Options opt;
        opt.depthSlots = 10;
        Script s;
        auto tr = scriptedTransport(s, {}, {});
        auto store = [](const glotbf::IndexFrame&, const std::string&) { return true; };
        auto sleepFn = [](double) {};
        int seen = 0;
        auto stop = [&seen]() { return seen >= 3; };
        auto countingStore = [&seen](const glotbf::IndexFrame&, const std::string&) {
            ++seen;
            return true;
        };
        glotbf::Result r = glotbf::run(idx, {}, t0, opt, tr, countingStore, sleepFn, stop);
        check_bool("parada por el llamante", r.stoppedByCaller);
        check_int("no pide el resto", (int)r.requested.size(), 3);
    }

    std::printf("\n[test_glo_backfill] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
