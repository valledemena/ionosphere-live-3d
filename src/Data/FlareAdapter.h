#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Fulguraciones solares (FLR) del catalogo DONKI del CCMC (NASA). Port de la
// fuente que usa la demo web para el panel Geospace. Puro calculo de datos:
// sin DOM ni OpenGL.
//
// FUENTE VERIFICADA VIVA EL 2026-10-27 (ventana 2026-10-24..2026-10-27):
//   GET https://ccmc.gsfc.nasa.gov/DONKI-API/get/FLR?startDate=YYYY-MM-DD&endDate=YYYY-MM-DD
//     -> 200, ARRAY de eventos. Ese dia devolvio 2 eventos reales (C1.9, C2.8).
//   Forma alterna tambien verificada 200 (mismo cuerpo):
//     GET https://ccmc.gsfc.nasa.gov/DONKI/WS/get/FLR?startDate=...&endDate=...
//   Espejo kauai: GET https://kauai.ccmc.gsfc.nasa.gov/DONKI/WS/get/FLR?...
//     el 2026-10-27 rechaza la conexion TCP 443 desde esta red (HTTP 000).
//     Se mantiene como tercer intento por si vuelve; nunca se inventa dato.
//
// Forma real de un evento (recortado de la respuesta del 2026-10-27):
//   {"flrID":"2026-10-26T08:21:00-FLR-001",
//    "catalog":"M2M_CATALOG",
//    "instruments":[{"displayName":"GOES-P: EXIS 1.0-8.0"}],
//    "beginTime":"2026-10-26T08:21Z",
//    "peakTime":"2026-10-26T08:38Z",
//    "endTime":"2026-10-26T08:51Z",
//    "classType":"C1.9",
//    "sourceLocation":"S10W70",
//    "activeRegionNum":14561,
//    "note":"",
//    "link":"https://ccmc.gsfc.nasa.gov/DONKI/view/FLR/49256/-1",
//    "linkedEvents":[{"activityID":"2026-10-26T09:00:00-CME-001"}],
//    "sentNotifications":null,
//    "submissionTime":"2026-10-26T15:38Z",
//    "versionId":1}
//   Horas ISO "YYYY-MM-DDTHH:MMZ" (sin segundos; se aceptan con segundos,
//   fraccion u offset). peakTime puede faltar -> se usa beginTime.
//   activeRegionNum puede faltar o ser null -> -1. sourceLocation puede ser
//   cadena vacia (region no asignada); se conserva tal cual, sin inventar.
//
// Sin parser JSON general (Utils/Json.h solo saca el primer numero de una
// clave en todo el documento: inservible aqui), se usa un escaner dirigido:
// se parte el array en objetos de llaves balanceadas (respetando cadenas y
// escapes) y dentro de cada objeto se busca la PRIMERA aparicion de cada
// clave. Nunca lanza: JSON malformado -> snapshot invalido.
//
// Honestidad (O1.1): cero eventos sinteticos. Si la red falla y hay cache de
// disco fresca se sirve declarandolo (fromCache=true, timeUtc = epoch del
// cache); si no, el snapshot se queda invalido con el motivo en note /
// getErrorMessage(). Si ya habia datos buenos NO se vacian
// (stale-while-revalidate): se conservan y solo se rellena el error.
//
// LIMITES DECLARADOS:
//   - Ventana de 72 h calculada en el cliente (startDate/endDate del endpoint,
//     que filtra por DIA UTC, no por hora).
//   - Orden: peakTime descendente (beginTime si falta peakTime); tope 50.
//   - Se descartan objetos sin ninguna hora legible: sin tiempo no hay edad ni
//     orden posible, y un evento a medias seria un dato inventado.
//   - rank NO es flujo fisico (GOES es logaritmico): es el codigo de clase
//     pedido por el panel: C=1, M=10, X=100 mas el numero (M1.2 -> 11.2).
//   - Cache de disco opcional cache/flares.json (respuesta cruda + epoch de
//     fetch), servible hasta 24 h; mas vieja se ignora.

struct FlareEvent {
    std::string id, classType, sourceLocation;
    double beginUtc = 0.0, peakUtc = 0.0, endUtc = 0.0;   // 0 si falta
    int activeRegion = -1;                                // -1 si falta
    float rank = 0.0f;                                    // C=1, M=10, X=100
    bool hasPeak = false;
};

// events[] va del mas reciente al mas antiguo (peakTime descendente, cap 50).
// valid=true solo si el JSON se parseo; una lista vacia es valida y se
// declara "sin fulguraciones" (no es error).
struct FlareSnapshot {
    std::vector<FlareEvent> events;
    double timeUtc = 0.0;      // instante del fetch (edad), o del cache servido
    bool valid = false;        // true si el parseo fue correcto
    bool fromCache = false;
    std::string note;          // "N flares" / "sin fulguraciones" / motivo
};

class FlareAdapter {
public:
    void init();
    // Descarga + parsea en hilo detached. No-op si ya hay una en curso.
    void fetchAsync(double utc);
    bool isFetching() const { return fetching_; }
    FlareSnapshot getSnapshot() const;      // copia bajo mutex
    double getLastFetchTime() const { return lastFetchUtc_; }
    // Copia bajo mutex: el texto lo escribe el hilo de fetch, asi que devolver
    // una referencia seria una carrera al leerla desde el hilo principal.
    std::string getErrorMessage() const;

    // Expuesto para pruebas: no toca red ni mutex ni disco.
    // Devuelve true si el JSON es un array parseable (aunque este vacio).
    static bool parseFlares(const std::string& json, double nowUtc, FlareSnapshot& out);
    // Clase GOES -> rango numerico: C=n, M=10+n, X=100+n (M1.2 -> 11.2).
    // Acepta solo mayuscula de clase + numero; nada mas ("" "Z9" "M" "M1.2x"
    // -> false y rank intacto).
    static bool flrRank(const std::string& classType, float& rank);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetch(double utc);
    // Cache de disco opcional: respuesta cruda + epoch. Best-effort, nunca
    // fatal (si el directorio no se puede crear se sigue sin cache).
    static bool readCache(std::string& body, double& epoch);
    static void writeCache(double epoch, const std::string& body);

    mutable std::mutex mutex_;
    FlareSnapshot latest_;
    std::atomic<bool> fetching_{false};
    double lastFetchUtc_ = 0.0;
    std::string error_;
};
