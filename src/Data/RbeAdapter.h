#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Eventos RBE (Radiation Belt Enhancement) del catalogo DONKI del CCMC.
// Modulo de datos puro: sin DOM, sin OpenGL y sin estado global.
//
// FUENTE PINADA (verificada viva el 2026-10-27):
//   GET https://ccmc.gsfc.nasa.gov/DONKI-API/get/RBE?startDate=YYYY-MM-DD&endDate=YYYY-MM-DD
//   HTTP 200, ARRAY JSON (no objeto contenedor). Ejemplo real recortado:
//     [{"rbeID":"2026-10-22T20:20:00-RBE-001","eventTime":"2026-10-22T20:20Z",
//       "instruments":[{"displayName":"GOES-P: SEISS >2MeV"}],
//       "link":"https://ccmc.gsfc.nasa.gov/DONKI/view/RBE/49205/-1",
//       "linkedEvents":[{"activityID":"2026-10-16T12:38:00-CME-001"}],
//       "submissionTime":"2026-10-22T20:26Z","versionId":1}]
//   La ventana 2026-08-30..2026-10-27 devolvio 6652 bytes y 8 eventos reales.
//
// LIMITES DECLARADOS:
//   - DONKI rechaza ventanas de mas de 60 dias (HTTP 400: "Date range cannot
//     exceed 60 days"). Aqui se piden 58 dias (hoy-58..hoy) para no rozar el
//     borde con husos ni redondeos de dia.
//   - Tope de 8 eventos publicados (el boletin de 58 dias nunca dio mas).
//   - Sin `eventTime` legible el objeto se DESCARTA: sin tiempo no se puede
//     ordenar y rellenarlo seria inventar el dato.
//   - `instruments` puede faltar o ser null -> instrument queda vacio, el
//     evento sigue siendo valido.
//   - `[]` es una respuesta VALIDA ("no RBE events in 58 days"), no un error.
//   - JSON basura o truncado -> parseo false y snapshot invalido con motivo.
//
// Cache: `cache/rbe.json` (relativa al CWD), cabecera "RBE1 <epoch>\n" seguida
// de la respuesta cruda. Se sirve solo si tiene menos de 24 h y solo cuando no
// hay datos buenos previos (stale-while-revalidate); en ese caso
// `fromCache = true` y el error declara la edad. Nunca se escribe cache de un
// cuerpo que no parseo.

struct RbeEvent {
    std::string id;            // rbeID, p.ej. "2026-10-22T20:20:00-RBE-001"
    std::string eventTimeIso;  // eventTime crudo, p.ej. "2026-10-22T20:20Z"
    std::string instrument;    // instruments[0].displayName ("" si no hay)
    std::string link;          // link al visor DONKI
    double timeUtc = 0.0;      // eventTime en epoch UTC (siempre > 0)
};

// events[] va ordenado por timeUtc DESCENDENTE (el mas reciente primero), no en
// el orden del JSON (DONKI los publica del mas viejo al mas nuevo).
struct RbeSnapshot {
    std::vector<RbeEvent> events;
    double timeUtc = 0.0;   // instante de la peticion (o del cache, si se sirve)
    bool valid = false;     // true si el array se parseo (aunque tenga 0 eventos)
    bool fromCache = false; // true si viene de cache/rbe.json
    std::string note;       // "N RBE events" / "no RBE events in 58 days" / motivo
};

class RbeAdapter {
public:
    void init();
    // Descarga + parsea en hilo detached (no bloquea el render). No-op si ya
    // hay una descarga en curso.
    void fetchAsync(double utc);
    bool isFetching() const { return fetching_; }
    RbeSnapshot getSnapshot() const;              // copia bajo mutex
    double getLastFetchTime() const { return lastFetchUtc_; }
    std::string getErrorMessage() const;          // copia bajo mutex

    // Expuesto para pruebas: no toca red, mutex ni disco. Resetea `out` al
    // empezar. `nowUtc` solo se guarda en out.timeUtc (no se usa para filtrar).
    static bool parseRbe(const std::string& json, double nowUtc, RbeSnapshot& out);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetch(double utc);
    static bool readCache(std::string& body, double& epoch);
    static void writeCache(double epoch, const std::string& body);

    mutable std::mutex mutex_;
    RbeSnapshot latest_;
    std::atomic<bool> fetching_{false};
    double lastFetchUtc_ = 0.0;
    std::string error_;
};
