#include "EsaAdapter.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

void EsaAdapter::init() {
    cacheDir_ = "data/cache/esa";
    fs::create_directories(cacheDir_);
    printf("[OK] EsaAdapter initialized\n");
}

bool EsaAdapter::fetchTEC(double utc) {
    // ESA Space Weather TEC API endpoint
    // https://weather.spacedata.copernicus.eu/
    //
    // For MVP, generate simulated TEC based on:
    // - IRI model-like diurnal variation
    // - Equatorial anomaly peaks
    // - Solar activity level

    data_.width = 72;   // 5-degree longitude steps
    data_.height = 36;  // 5-degree latitude steps
    data_.values.resize(data_.width * data_.height);
    data_.timestamp = utc;
    data_.valid = true;

    float solarNoon = 12.0f;  // Approximate solar noon at prime meridian

    for (int lat = 0; lat < data_.height; lat++) {
        for (int lon = 0; lon < data_.width; lon++) {
            float latDeg = -90.0f + lat * 5.0f;
            float lonDeg = -180.0f + lon * 5.0f;

            // Local time at this longitude
            float localHour = fmodf((float)(utc / 3600.0f) + lonDeg / 15.0f + 24.0f, 24.0f);

            // Equatorial ionization anomaly (EIA) - peaks at +/- 15 deg latitude
            float eqLat = fabsf(latDeg);
            float eiaFactor = expf(-powf((eqLat - 15.0f) / 10.0f, 2.0f));

            // Diurnal variation - peak at 14 LT
            float hourAngle = localHour - 14.0f;
            if (hourAngle > 12.0f) hourAngle -= 24.0f;
            if (hourAngle < -12.0f) hourAngle += 24.0f;
            float diurnal = expf(-powf(hourAngle / 5.0f, 2.0f));

            // Nighttime minimum
            float nightFactor = 0.15f + 0.85f * diurnal;

            // Base TECU
            float baseTEC = 5.0f + 35.0f * eiaFactor * nightFactor;

            // Add some longitudinal variation (wave-4 pattern from tides)
            float tide = 0.1f * sinf(lonDeg * 4.0f * 3.14159f / 180.0f);

            float tecu = baseTEC * (1.0f + tide);
            data_.values[lat * data_.width + lon] = fmaxf(0.0f, tecu);
        }
    }

    printf("[EsaAdapter] TEC generated: %dx%d, range [%.1f, %.1f] TECU\n",
           data_.width, data_.height,
           *std::min_element(data_.values.begin(), data_.values.end()),
           *std::max_element(data_.values.begin(), data_.values.end()));

    return true;
}

bool EsaAdapter::loadCachedData(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return false;

    fread(&data_.width, sizeof(int), 1, fp);
    fread(&data_.height, sizeof(int), 1, fp);
    fread(&data_.timestamp, sizeof(double), 1, fp);

    int valid;
    fread(&valid, sizeof(int), 1, fp);
    data_.valid = (valid != 0);

    data_.values.resize(data_.width * data_.height);
    fread(data_.values.data(), sizeof(float), data_.values.size(), fp);

    fclose(fp);
    printf("[EsaAdapter] Loaded cached TEC: %dx%d\n", data_.width, data_.height);
    return true;
}

bool EsaAdapter::saveCachedData(const std::string& path) const {
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) return false;

    fwrite(&data_.width, sizeof(int), 1, fp);
    fwrite(&data_.height, sizeof(int), 1, fp);
    fwrite(&data_.timestamp, sizeof(double), 1, fp);

    int valid = data_.valid ? 1 : 0;
    fwrite(&valid, sizeof(int), 1, fp);

    fwrite(data_.values.data(), sizeof(float), data_.values.size(), fp);

    fclose(fp);
    return true;
}
