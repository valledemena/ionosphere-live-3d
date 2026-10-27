#pragma once
// =============================================================================
// Lote 5 / modulo B: Voyager 1 y Voyager 2 (JPL Horizons) + enlace Friis.
//
// Port de ionosphere-web/src/lib/iono/voyager.ts (regla 252: fuente pineada
// ANTES de escribir codigo). Datos puros: sin DOM, sin GL, sin estado global.
//
// FUENTE (verificada viva el 2026-10-27, 5290 bytes, HTTP 200):
//   POST https://ssd.jpl.nasa.gov/api/horizons.api
//        format=text&COMMAND='-31'&OBJ_DATA='NO'&MAKE_EPHEM='YES'
//        &EPHEM_TYPE='OBSERVER'&CENTER='500@399'&START_TIME='YYYY-MM-DD'
//        &STOP_TIME='YYYY-MM-DD'&STEP_SIZE='1d'&QUANTITIES='20'
//   COMMAND '-31' = Voyager 1, '-32' = Voyager 2. Centro 500@399 = geocentro
//   terrestre. QUANTITIES '20' = columna 1 "delta" (rango aparente en AU,
//   con aberracion de tiempo-luz) y columna 2 "deldot" (km/s). Tabla entre
//   $$SOE y $$EOE, filas con fecha "YYYY-Mon-DD HH:MM" en UTC:
//     2026-Oct-26 00:00     172.732204628703  33.7197642
//     2026-Oct-27 00:00     172.751591207326  33.4172627
//     2026-Oct-28 00:00     172.770801698361  33.1099465
//   El rango se REFRESCA en cada descarga: aqui no hay pin de catalogo (el
//   pin del demo web, 172.4869 AU del 14-oct, ya no era cierto el 15-oct).
//
// SIN RED: snapshot invalido con el motivo. JAMAS una distancia inventada: el
// panel escribe "No Voyager range available" y el error. Si ya habia datos
// buenos se conservan (stale-while-revalidate) y el error se declara.
//
// MODELO DECLARADO (constantes publicas JPL, copiadas del demo web). NO es
// telemetria: es un presupuesto de enlace de orden de magnitud.
//   VOYAGER_FREQ_HZ = 8.42e9   banda X de bajada
//   VOYAGER_TX_W    = 23       TWTA de la nave
//   VOYAGER_DISH_M  = 3.7      antena de la nave
//   DSN_DISH_M      = 70       antena DSN de 70 m
//   ETA_TX = 0.55, ETA_RX = 0.65   eficiencias tipicas (declaradas, no medidas)
//   C_MS = 299792458, AU_KM = 1.496e8 (aprox.; Horizons usa 1.495978707e8)
//   dishGain(D, eta) = eta * (pi*D/lambda)^2
//   friisReceivedW(r_au) = Pt * Gt * Gr * (lambda/(4*pi*r))^2, r en metros
//   lightTimeSec(r_au)   = r_m / c
//   pulseLapse(lt, sweep) = max(1, round(lt*1000/sweep))
// A 172.75 AU sale Pr ~ 4e-19 W (1e-19 W = 1e-22 kW) y el tiempo-luz de ida
// ~23.9 h. Son los ordenes de magnitud reales de una recepcion DSN, no una
// medida de potencia: el numero depende de la potencia/eficiencia supuestas.
// El barrido del pulso se anima en kSweepMs = 10 s y el factor xN es VISIBLE
// en el panel: el transito real de 23.9 h se comprime, se dice, no se oculta.
//
// CACHE (best-effort, relativa al CWD): cache/voyager.txt, edad maxima 6 h.
// Formato de texto propio (v1), ASCII, lineas:
//   # voyager-cache v1 fetch=<epoch_utc>
//   VOY1 <epoch_fila> <au> <YYYY-Mon-DD HH:MM>
//   VOY2 <epoch_fila> <au> <YYYY-Mon-DD HH:MM>
//   # --- V1 raw
//   <texto crudo completo de Horizons para V1>
//   # --- V2 raw
//   <texto crudo completo de Horizons para V2>
// La cabecera lleva el epoch del fetch (caducidad) y cada linea VOY1/VOY2 el
// epoch de la fila elegida; el bloque crudo queda como procedencia (no se
// re-parsea al cargar, pero permite auditar). Se sirve solo si fetch tiene
// menos de 6 h y el AU esta en la banda 100-200; entonces fromCache = true y
// la nota del snapshot lo declara. Un fichero que no empieza por la cabecera
// v1 se ignora entero (nunca se adivina).
// =============================================================================
#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

struct VoyagerFix {
    double au = 0.0;        // rango geocentrico (AU), columna "delta"
    double timeUtc = 0.0;   // epoch UTC de la fila
    std::string date;       // "2026-Oct-27 00:00" (como lo imprime Horizons)
    bool valid = false;
};

struct VoyagerProbe {
    VoyagerFix fix;
    double lightHours = 0.0;    // tiempo-luz de ida, horas
    double receivedW = 0.0;     // presupuesto Friis declarado, W
    double pulseFactor = 1.0;   // barrido: xN para 10 s de animacion
};

// v1/v2 con fix.valid a true solo si esa sonda llego y se parseo.
struct VoyagerSnapshot {
    VoyagerProbe v1, v2;
    double timeUtc = 0.0;    // epoch del fetch
    bool valid = false;      // true si al menos una sonda tiene rango
    bool fromCache = false;  // servido de cache/voyager.txt
    std::string note;        // "live: 2 probes" / "Voyager 2: <motivo>" / "cache ..."
};

class VoyagerAdapter {
public:
    void init();
    // Descarga las DOS sondas en hilo background (no-op si ya hay una en
    // curso). Sin TTL en vivo: el kick del panel debe refrescar de verdad;
    // la caducidad de 6 h es la del cache servido.
    void fetchAsync(double utc);
    bool isFetching() const { return fetching_.load(); }

    VoyagerSnapshot getSnapshot() const;   // copia bajo mutex
    double getLastFetchTime() const;       // ultimo fetch correcto (0 si nunca)
    std::string getErrorMessage() const;   // copia bajo mutex

    // Parseo de una tabla Horizons completa (cabeceras, $$SOE/$$EOE, cola de
    // documentacion). Rellena out.v1 con la fila PASADA mas cercana al reloj
    // real (nunca una futura), o con la primera si todas son futuras. Solo
    // filas "YYYY-Mon-DD HH:MM" con rango en (100, 200) AU. out.v2 no se toca.
    // Devuelve false (y out invalido con nota) con texto vacio, basura o sin
    // filas validas. Es la entrada pura que usa el test; no toca red ni mutex.
    static bool parseHorizons(const std::string& text, VoyagerSnapshot& out);

    // Filas crudas de una tabla (misma validacion, sin elegir). Util para
    // auditar cuantas filas trajo la respuesta y en que banda caen.
    static bool parseRows(const std::string& text, std::vector<VoyagerFix>& rows);

    // Fila pasada mas cercana a nowUtc. Si todas son futuras, la mas temprana
    // (la primera de una tabla Horizons ascendente). false si rows esta vacia.
    static bool pickCurrentRow(const std::vector<VoyagerFix>& rows, double nowUtc,
                               VoyagerFix& out);

    // Presupuesto de enlace declarado (W) a rAu. rAu <= 0 -> 0.
    static double friisReceivedW(double rAu);
    // Tiempo-luz de ida (s) a rAu. rAu <= 0 -> 0.
    static double lightTimeSec(double rAu);
    // Factor visible del barrido: max(1, round(lightSec*1000/sweepMs)) con
    // sweepMs > 0; cualquier entrada no positiva -> 1.
    static double pulseLapse(double lightSec, double sweepMs);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb,
                                std::string* userp);
    bool httpPost(const std::string& body, std::string& out, std::string& err);
    // Una sonda: POST + parseo + fila elegida + magnitudes derivadas.
    VoyagerProbe fetchProbe(double utc, const char* command, std::string& err, std::string& raw);
    bool fetch(double utc);
    static bool saveCache(const std::string& path, const VoyagerSnapshot& snap,
                          const std::string& rawV1, const std::string& rawV2);
    static bool loadCache(const std::string& path, double nowUtc, VoyagerSnapshot& out);

    mutable std::mutex mutex_;
    VoyagerSnapshot latest_;
    std::atomic<bool> fetching_{false};
    double lastFetchUtc_ = 0.0;
    std::string error_;
};
