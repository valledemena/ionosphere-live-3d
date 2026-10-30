#pragma once
// =============================================================================
// PeriodicWait: la espera entre ciclos del backfill periodico, extraida de
// App::histPreloadThread para poder PROBARLA (hay un hoyo: sin plazo, el
// bucle solo salia por cambio de profundidad o cierre — y el ciclo horario
// no corria NUNCA si el usuario no tocaba nada; el hueco tras una
// suspension no se rellenia).
//
// Contrato: esperar hasta PERIOD (1 h en App) SIEMPRE que no ocurra antes
// un cambio de profundidad o un cierre. El `waitStep` produce la granularidad
// de re-chequeo (en App: depthCv.wait_for(1 s), que ademas DESPIERTA YA por
// notify en un cambio de profundidad; en test: tue ritmo falsificado).
// Header solo, std: testeable sin App, sin GL, sin red.
// =============================================================================
#include <functional>

namespace periodicwait {

// `periodSec`: plazo maximo entre ciclos (3600 en App).
// `stopped()`: cierre pedido (shutdownRequested).
// `depth()`: profundidad actual (tecDepthSlots.load()).
// `waitStep()`: granularidad de espera — debe re-chequear al emitirse una
//               senal (cv con notify) o al llegar su slice propio.
// `nowSec()`: reloj inyectable (steady en App; falsificado en test).
//
// Devuelve false si la espera termino por CIERRE (no correr el ciclo).
template <typename WaitStep, typename NowSec, typename Stopped, typename Depth>
bool waitNextCycle(double periodSec,
                   Stopped&& stopped,
                   Depth&& depth,
                   WaitStep&& waitStep,
                   NowSec&& nowSec) {
    const int lastDepth = depth();
    const double deadline = nowSec() + periodSec;
    while (!stopped() && depth() == lastDepth && nowSec() < deadline) {
        waitStep();
    }
    return !stopped();
}

}  // namespace periodicwait
