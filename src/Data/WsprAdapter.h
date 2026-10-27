#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Spots WSPR (red de balizas de voluntarios WSPRnet) desde la tabla HTML
// publica de `http://wsprnet.org/olddb?mode=html&band=all`.
// Verificado en vivo el 2026-10-26: 302 -> www.wsprnet.org (mismo path) y
// 200 con ~35 KB, ~53 filas de spots reales. Columnas (14 celdas):
//   Date | Call | Frequency(MHz) | SNR | Drift | Grid | dBm | W | by | loc |
//   km | mi | Mode | Version
// Fecha "YYYY-MM-DD HH:MM" en UTC (sin segundos), celdas con `&nbsp;`.
// La cabecera de la tabla usa <th> y no tiene fecha: se descarta sola.
//
// Honestidad: SOLO spots reales parseados; sin fallback sintetico. Si la red
// falla el snapshot queda invalido (`valid == false`) con el texto en
// getErrorMessage() y las notas "sin spots". Persistencia DECLARADA de
// 10 minutos (TTL, los spots caducan por tiempo) y techo de 40 spots/arcos.
//
// La logica es el port exacto de ionosphere-web/src/lib/iono/wspr.ts:
// Maidenhead 4/6 -> centro de celda, tabla WSPR_DIAL con tolerancia 0.15 MHz
// por debajo de 30 MHz y 1.0 MHz por encima, y descarte de filas con fecha
// ilegible, frecuencia fuera de banda o grid invalido.
struct WsprSpot {
    std::string tx, rx, band;
    float freqMHz = 0.0f;
    float snrDb = 0.0f;
    double timeUtc = 0.0;       // epoch UTC (segundos)
    float txLat = 0.0f, txLon = 0.0f, rxLat = 0.0f, rxLon = 0.0f;
    float km = 0.0f;
};

struct WsprSnapshot {
    std::vector<WsprSpot> spots;
    double timeUtc = 0.0;       // instante de referencia del fetch (edad/TTL)
    bool valid = false;         // true solo si hay >= 1 spot real
    std::string note;           // "sin spots" / "N spots"
};

class WsprAdapter {
public:
    void init();
    // Descarga + parsea en hilo detached. No-op si ya hay una en curso.
    void fetchAsync(double utc);
    bool isFetching() const { return fetching_; }
    WsprSnapshot getSnapshot() const;       // copia bajo mutex

    double getLastFetchTime() const { return lastFetchUtc_; }
    // Copia bajo mutex: el texto lo escribe el hilo de fetch, asi que devolver
    // una referencia seria una carrera al leerla desde el hilo principal.
    std::string getErrorMessage() const;

    // Puro (sin red): parsea la tabla y aplica TTL + banda + grid + tope 40.
    // Devuelve true si hay al menos un spot valido.
    static bool parseTable(const std::string& html, double nowUtc, WsprSnapshot& out);
    // Maidenhead 4 o 6 caracteres -> lat/lon del CENTRO de la celda.
    static bool maidenheadToLatLon(const std::string& grid, float& lat, float& lon);
    // Banda WSPR de una frecuencia en MHz ("" si cae fuera de banda).
    static std::string bandForFreq(float mhz);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchPage(std::string& response);
    bool fetch(double utc);

    mutable std::mutex mutex_;
    WsprSnapshot latest_;
    std::atomic<bool> fetching_{false};
    double lastFetchUtc_ = 0.0;
    std::string error_;
};
