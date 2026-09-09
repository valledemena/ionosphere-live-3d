#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

#include "Data/GiroAdapter.h"  // StationSample + GiroHistoryPoint (reuso)

// Fila cruda de https://prop.kc2g.com/api/stations.json (M4R-A, FASE A).
// Schema verificado en vivo (2026-09-09, 101 filas): array plano de objetos
//   {fof2, mufd, hmf2, fof1, foe, foes, hf2(h'F2), he(h'E), cs, md(CADENA),
//    tec, yf1, yf2, hmf1, hme, fbes, scalef2, id, time(ISO por fila),
//    source, station:{code, name, latitude(CADENA), longitude(CADENA 0-360), id}}
// con nulls frecuentes (filas NOAA congeladas 2023/2024 incluidas).
struct Kc2gRow {
    std::string code;        // URSI, p.ej. "EB040" (cadena: "09429" existe)
    std::string name;
    double latDeg = 0.0;
    double lonDeg = 0.0;     // NORMALIZADA a -180..180 en parse (origen 0-360)
    std::string source;      // "giro" | "noaa" | "ingv" | "aus-sws" | "giro_fastchar"
    std::string md;          // M(D) COMO CADENA (sello FASE A: sin coercion; "" si null)
    double timeUtc = 0.0;    // epoch de la fila; 0 si el ISO no es parseable
    float fof2 = 0.0f;
    float mufd = 0.0f;       // MUF(D), D=3000 km
    float hmf2 = 0.0f;
    float fof1 = 0.0f;
    float foe = 0.0f;
    float foes = 0.0f;
    float hf2 = 0.0f;        // h'F2 (altura virtual F2)
    float he = 0.0f;         // h'E (altura virtual E)
    float cs = 0.0f;         // confidence score (kc2g lo trae como float)
    // Sin mapear (sin campo en StationSample): tec, yf1, yf2, hmf1, hme,
    // fbes, scalef2, id. B0/B1/fmin/fxI NO EXISTEN en kc2g -> quedan 0 y
    // sanitizeB0/B1 devuelven 100/3 por defecto aguas abajo (sello FASE A:
    // funciona sin codigo extra).
};

// Kc2gAdapter: failover de GIRO via prop.kc2g.com (M4R, FASE A sellada:
// FAILOVER-ONLY, no always-on). lgdc sigue siendo primario escalonado;
// kc2g entra SOLO cuando lgdc falla (ronda con intentos y cero aciertos, o
// catalogo caido) y App conmuta el badge de fuente ("GIRO-live" /
// "kc2g-fallback" / "model"). 1 GET por ciclo (600 s) sustituye a la ronda
// entera de getbest cuando Lowell nos limita: la redistribucion de KC2G
// esta sancionada por GIRO (acceso con permiso, apoyo WWROF; fuentes
// giro/noaa/ingv/aus-sws) — atribucion GIRO/WWROF/eSWua en UI.
//
// Reglas selladas de la FASE A (parseo puro + time-filter, testeables):
//   - lon 0-360 -> -180..180 EN PARSE (convencion de la app).
//   - md como CADENA (nunca numero).
//   - grids: fila con dato y <= 6 h; panel: <= 24 h (marcada stale).
//   - EG931 excluida de grids POR REGLA EXPLICITA (EG931-2024: feed NOAA
//     congelado desde 2024-08-22; la fila puede seguir en panel si fuese
//     fresca).
class Kc2gAdapter {
public:
    // Ventanas del sello (s).
    static constexpr double kGridMaxAgeSec = 6.0 * 3600.0;
    static constexpr double kPanelMaxAgeSec = 24.0 * 3600.0;

    // Regla explicita de exclusion de grids (ver struct Kc2gRow arriba).
    static bool isGridBlockedCode(const std::string& code);

    // Parser puro (fixture-testable): array JSON -> filas crudas, SIN
    // filtrar nada. Acepta whitespace/pretty-print; false ante JSON
    // malformado. Filas sin station.code se descartan (documento arriba).
    static bool parseStations(const std::string& json, std::vector<Kc2gRow>& out);

    // Clasificacion pura (time-filter del sello; ahoraUtc explicito — sin
    // reloj, determinista para tests). Edad negativa (clock skew) cuenta
    // como fresca.
    static bool rowHasData(const Kc2gRow& r);
    static double rowAgeSec(const Kc2gRow& r, double nowUtc);
    static bool gridUsable(const Kc2gRow& r, double nowUtc);   // dato + <=6 h + !bloqueada
    static bool panelUsable(const Kc2gRow& r, double nowUtc);  // dato + <=24 h

    // Conversion kc2g -> StationSample (pipeline comun de la app):
    // valid = panelUsable; stale = !gridUsable (>6 h o EG931-2024: panel
    // si, grids no); historia de 1 punto (sampleHistoryAt clampa al
    // ultimo valor: replay congelado pero honesto, sello FASE A).
    static StationSample toSample(const Kc2gRow& r, double nowUtc);

    Kc2gAdapter();
    void init();
    // Un ciclo de failover: 1 GET + parse + conversion con el time-filter.
    // Guarda SOLO las filas del panel window (<=24 h). true si quedo alguna
    // usable. Sincrono (tests / smoke).
    bool fetch(double nowUtc);
    // Igual en hilo background (no bloquea el render). Patron GiroAdapter.
    void fetchAsync(double nowUtc);

    bool isFetching() const { return fetching_; }
    std::vector<StationSample> copySamples() const;
    float getDataIndicator() const;   // % filas usables (panel) del ultimo batch
    // Estado del ultimo ciclo para UI/badge (thread-safe).
    struct Kc2gStatus {
        int rows = 0;                  // filas parseadas
        int gridUsable = 0;            // filas <=6 h con dato (no bloqueadas)
        int panelUsable = 0;           // filas <=24 h con dato (almacenadas)
        int stale = 0;                 // banda 6-24 h (panel si, grids no)
        double freshestAgeMin = -1.0;  // de las gridUsable; -1 = ninguna
        double lastFetchUtc = 0.0;     // ultimo INTENTO (throttle del ciclo)
        double lastOkUtc = 0.0;        // ultimo batch bueno (conservado en fallo)
    };
    Kc2gStatus getStatus() const;
    const std::string& getErrorMessage() const { return error_; }

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchURL(const std::string& url, std::string& response);

    mutable std::mutex mutex_;
    std::vector<StationSample> samples_;   // panel window (<=24 h), flags al lote
    Kc2gStatus status_;
    std::atomic<bool> fetching_{false};
    std::string error_;
};
