#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

// StormAdapter: celdas de tormenta reales y geolocalizadas para los sprites de
// WhistlerLayer. Portado de la demo web (ionosphere-web, lightning.ts).
//
// FUENTE Y HONESTIDAD (declarado, mismas conclusiones que la web):
//
//   - Los granulos GLM-L2-LCFA (AWS Open Data, buckets noaa-goes19/18) son
//     NetCDF-4: sin HDF5 local no hay localizacion legible, asi que en este
//     motor NO se parsean flashes. La app NO debe afirmar recuentos de rayos:
//     no hay ningun recuento detras (la web solo declara frescura, cobertura
//     y tamano de fichero). NetCDF queda FUERA DE ALCANCE aqui.
//   - Lo unico usable sin dependencias nuevas es la API publica de avisos NWS
//     (GeoJSON FeatureCollection): los poligonos "Severe Thunderstorm Warning"
//     dan cuspides de tormenta honestas y geolocalizadas sobre CONUS, que si
//     cae dentro de la cobertura GLM.
//   - Cobertura: SOLO CONUS. Un aviso fuera de EE.UU. continental no existe
//     para esta capa; no es un mapa global de tormentas.
//   - Centroide = MEDIA SIMPLE de los [lon, lat] del anillo exterior
//     (polyCentroid de la web, declarado simple): no es el centroide de area
//     ni el centro del aviso.
//   - Sin red no hay sustituto simulado: sin respuesta valida el snapshot se
//     queda invalido y el texto de error se conserva (nunca celdas falsas).
//
// El consumidor (WhistlerLayer) canaliza el whistler por la LINEA DE CAMPO
// DIPOLAR REAL del punto (Helliwell 1965), no por una animacion.
//
// Endpoint verificado en vivo:
//   https://api.weather.gov/alerts/active?event=Severe%20Thunderstorm%20Warning
// NWS exige User-Agent propio y acepta Accept: application/geo+json o JSON.
struct StormCell {
    std::string id, area;      // properties.id, properties.areaDesc
    float lat = 0.0f, lon = 0.0f;
    double onsetUtc = 0.0, endsUtc = 0.0;
    bool hasEnds = false;
};

struct StormSnapshot {
    std::vector<StormCell> cells;
    double timeUtc = 0.0;
    bool valid = false;        // true si el JSON se parseo (aunque no haya celdas)
    std::string note;          // "sin avisos" / "N avisos"
};

class StormAdapter {
public:
    void init();
    void fetchAsync(double utc);
    bool isFetching() const;
    StormSnapshot getSnapshot() const;
    double getLastFetchTime() const;
    // Copia bajo mutex: el texto lo escribe el worker de fetchAsync, asi que
    // devolver una referencia seria una carrera al leerla desde el render.
    std::string getErrorMessage() const;

    // Parser puro (sin red): expuesto para probes/tests. Devuelve false si el
    // documento no es un FeatureCollection. Recorta a 24 celdas.
    static bool parseAlerts(const std::string& json, double nowUtc, StormSnapshot& out);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetch(double utc);
    bool fetchURL(const std::string& url, std::string& response);

    mutable std::mutex mutex_;
    StormSnapshot snapshot_;
    std::atomic<bool> fetching_{false};
    double lastFetchUtc_ = 0.0;
    std::string error_;
};
