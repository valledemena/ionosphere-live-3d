#pragma once
// =============================================================================
// C1b: planificador del backfill GloTEC (puro).
//
// Sin curl, sin disco, sin reloj real: el transporte, el reloj (sleep), el
// almacen y la senal de parada se INYECTAN. Asi el orden, el ritmo, la parada
// por 404 y el Retry-After se prueban sin tocar NOAA (tests/test_glo_backfill).
//
// Reglas:
//   - Plan: horizonte de depthSlots frames MAS NUEVOS dentro de la ventana de
//     72 h (36 = 6 h, 144 = 24 h, 432 = 72 h a 10 min/frame). Dentro de ese
//     horizonte se descartan los epochs que ya estan en el cache.
//   - Ritmo: >= minIntervalSec entre peticiones (1 s por defecto).
//   - Un 404 se cuenta y se salta; 3 404 CONSECUTIVOS paran. Un 404 aislado
//     no bloquea nada: este modulo no escribe estado de puerta.
//   - Un 429 espera al menos Retry-After (acotado a maxWaitSec) antes de la
//     siguiente peticion.
//   - Un 5xx para: el servidor esta caido, insistir solo empeora.
// =============================================================================
#include <functional>
#include <string>
#include <vector>

namespace glotbf {

// Mismo horizonte que teccache::kHistWindowSec (72 h).
inline constexpr double kWindowSec = 259200.0;
inline constexpr int kDepth6h = 36;    // 6 h  / 10 min
inline constexpr int kDepth24h = 144;  // 24 h / 10 min
inline constexpr int kDepth72h = 432;  // 72 h / 10 min

// Entrada del indice GloTEC, ya parseada.
struct IndexFrame {
    std::string url;
    double timeUtc = 0.0;
};

// Respuesta del transporte inyectado.
struct Response {
    long http = 0;              // 0 si el transporte ni siquiera obtuvo respuesta
    bool transportOk = true;    // false = error de curl/transporte
    double retryAfterSec = 0.0; // cabecera Retry-After, 0 si no hay
    std::string body;
};

using Transport = std::function<Response(const std::string& url)>;
// Guarda un frame; true solo si el cuerpo era valido y se almaceno.
using Store = std::function<bool(const IndexFrame&, const std::string& body)>;
// Duerme N segundos (reloj inyectado en tests).
using Sleep = std::function<void(double seconds)>;
// Senal de parada (hilo interrumpible). Vacia = nunca parar.
using Stop = std::function<bool()>;

struct Options {
    int depthSlots = kDepth6h;
    double minIntervalSec = 1.0;
    int stopAfter404 = 3;
    double maxWaitSec = 600.0;
};

struct Result {
    std::vector<double> requested;   // epochs pedidos, en orden
    std::vector<double> stored;      // epochs almacenados, en orden
    int skippedCached = 0;           // frames del horizonte ya en cache
    int notFound = 0;                // 404 recibidos
    int httpErrors = 0;              // transporte, 429 y otros no-200
    bool stoppedOn404 = false;       // 3 404 consecutivos
    bool stoppedOn5xx = false;       // primer 5xx
    bool stoppedByCaller = false;    // stop() devolvio true
    double sleptSec = 0.0;           // suma de esperas
};

// Puro: horizonte de `depthSlots` frames mas nuevos de la ventana de 72 h y,
// DENTRO de ese horizonte, descarta los ya cacheados. El horizonte se fija
// antes del dedup: si no, en regimen estable se pedirian 36 frames viejos en
// cada pase y en ~12 pases se bajaria la ventana entera.
std::vector<IndexFrame> plan(const std::vector<IndexFrame>& index,
                             const std::vector<double>& cachedEpochs,
                             double nowUtc, int depthSlots);

// Ejecuta el plan. Devuelve el detalle de lo pedido/almacenado.
Result run(const std::vector<IndexFrame>& index,
           const std::vector<double>& cachedEpochs,
           double nowUtc, const Options& opt,
           const Transport& transport, const Store& store,
           const Sleep& sleepFn, const Stop& stop);

}  // namespace glotbf
