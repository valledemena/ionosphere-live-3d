#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#include "Ionosphere/GridLayer.h"  // TECGrid
#include "Utils/Paths.h"           // cache configurable (Paths.h)

// GloTecAdapter: obtiene el TEC ionosférico real desde NOAA SWPC GloTEC.
//
// El endpoint ofrece productos en varias resoluciones. Usamos el GeoJSON
// "geojson_2d_urt.json" que contiene un indice de archivos:
//   [{ url: "/products/glotec/glotec_icao_<fecha>.geojson", time_tag: "..." }]
// y el archivo es una FeatureCollection de 5184 features (72 lon x 72 lat,
// paso lon 5deg,  lat 2.5deg), cada una con properties {tec, anomaly, hmF2,
// NmF2, quality_flag}.
//
// El grid resultante usa el mismo layout que TecLayer::updateTexture() espera:
//   values[latIdx * width + lonIdx], fila 0 = lat -90 (sur), col 0 = lon -180
// (aqui col 0 = lon -177.5, desplazamiento de media celda aceptado).
// Una entrada del indice GloTEC (fichero historico disponible).
struct GloTecFrame {
    std::string url;      // tal cual viene en el indice (normalmente absoluta o /products/...)
    double timeUtc = 0.0; // time_tag del indice
};

class GloTecAdapter {
public:
    GloTecAdapter();
    void init();
    bool fetchTEC(double utc, int depthSlots = 36);
    // backfill 6/24/72 h: 36/144/432 frames @ 10 min
    // Historico para el replay: lista completa del indice + descarga de un
    // frame concreto a un GridData independiente (thread-safe entre adapters
    // distintos; no llamar concurrente sobre el MISMO adapter).
    bool fetchIndex(std::vector<GloTecFrame>& out);
    bool fetchFrame(const std::string& url, double timeUtc, GridData& out);
    // Backfill sobre un indice ya descargado (misma planificacion glotbf).
    // Con `onFrame` vacio guarda en cache y deja el MAS NUEVO en newestOut;
    // con `onFrame` entrega cada frame al llamante (que decide persistencia).
    struct BackfillStats {
        int planned = 0;    // peticiones planificadas
        int cached = 0;     // frames del horizonte ya cacheados
        int stored = 0;     // frames obtenidos
        long long netBytes = 0;  // bytes de red reales descargados (glotbf::Result)
    };
    bool backfillFrom(const std::vector<GloTecFrame>& index,
                      const std::vector<double>& cachedEpochs,
                      double utc, int depthSlots,
                      const std::function<void(const GridData&, double)>& onFrame,
                      GridData* newestOut, BackfillStats* stats);
    void setGridSize(int w, int h) { gridW_ = w; gridH_ = h; }
    // Override del directorio del cache TEC en disco (teccache::saveFrame). El
    // backfill no vuelve a pedir los frames que ya estan ahi. VACIO = seguir la
    // raiz global configurable (paths::cacheRootString, la familia "cache" de
    // settings.cfg); los tests lo fijan a un directorio temporal. Resuelto EN
    // CADA USO (effectiveCacheDir), no en construccion: los objetos pueden
    // existir antes de que la app fije la raiz.
    void setCacheDir(const std::string& dir) { cacheDirOverride_ = dir; }
    std::string effectiveCacheDir() const {
        return cacheDirOverride_.empty() ? paths::cacheRootString()
                                         : cacheDirOverride_;
    }
    // Hilo interrumpible: el backfill consulta esta bandera antes de cada
    // peticion (no hay sleeps largos no interrumpibles).
    void setStopFlag(const std::atomic<bool>* flag) { stop_ = flag; }
    // Sumidero de bytes de red REALES (C1b/medido): cada peticion del backfill
    // (precarga o worker) suma el cuerpo recibido aqui. App lo proyecta al
    // indicador de progreso en vivo; frames servidos de cache suman 0.
    void setNetBytesSink(std::atomic<long long>* sink) { netBytesSink_ = sink; }
    const TECGrid& getGrid() const { return grid_; }
    double getLastFetchTime() const { return lastFetchUtc_; }
    const std::string& getErrorMessage() const { return error_; }

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata);
    bool fetchURL(const std::string& url, std::string& response);
    // Extrae todos los valores 'tec' y 'coordinates' (lon, lat) del GeoJSON
    // y rellena grid_. Devuelve true si encontro el numero esperado de celdas.
    bool parseGeoJSON(const std::string& json);

    TECGrid grid_;
    int gridW_ = 72;
    int gridH_ = 72;
    double lastFetchUtc_ = 0.0;
    std::string error_;
    // Ultimo resultado de fetchURL (para el backfill: HTTP, Retry-After y
    // error de transporte no son lo mismo).
    long lastHttp_ = 0;
    int lastCurl_ = 0;
    double lastRetryAfter_ = 0.0;
    std::string cacheDirOverride_;   // vacio = seguir la raiz global (Paths.h)
    const std::atomic<bool>* stop_ = nullptr;
    std::atomic<long long>* netBytesSink_ = nullptr;   // C1b/medido: bytes reales
};
