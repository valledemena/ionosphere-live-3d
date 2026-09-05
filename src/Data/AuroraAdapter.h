#pragma once
#include <string>

#include "Ionosphere/GridLayer.h"  // GridData

// AuroraAdapter: ovalo auroral NOAA Ovation (prediccion 30-90 min).
//
// Endpoint: https://services.swpc.noaa.gov/json/ovation_aurora_latest.json
//   {"Observation Time":"...","Forecast Time":"...",
//    "Data Format":"[Longitude, Latitude, Aurora]",
//    "coordinates":[[lon,lat,val], ...]}  (lon 0..359, lat -90..90, ~65k pts)
// Se rasteriza (max por celda) al layout 72x72 de GridLayer:
//   values[latIdx*72 + lonIdx], fila 0 = lat -90, col 0 = lon -180.
class AuroraAdapter {
public:
    AuroraAdapter();
    void init();
    bool fetchAurora(double utc);
    void setGridSize(int w, int h) { gridW_ = w; gridH_ = h; }
    const GridData& getGrid() const { return grid_; }
    double getLastFetchTime() const { return lastFetchUtc_; }
    bool isSimulated() const { return simulated_; }
    const std::string& getErrorMessage() const { return error_; }

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchURL(const std::string& url, std::string& response);
    bool parseOvation(const std::string& json, double utc);

    GridData grid_;
    int gridW_ = 72;
    int gridH_ = 72;
    double lastFetchUtc_ = 0.0;
    bool simulated_ = true;
    std::string error_;
};
