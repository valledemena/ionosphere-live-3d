#include "DataManager.h"
#include <curl/curl.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

DataManager::DataManager() {}

DataManager::~DataManager() {}

void DataManager::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Create cache directory
    cacheDir_ = "data/cache";
    fs::create_directories(cacheDir_);

    printf("[OK] DataManager initialized (cache: %s)\n", cacheDir_.c_str());
}

void DataManager::update(double utc) {
    // Auto-fetch if enough time has passed
    if (utc - lastFetchTime_ > fetchInterval_ && !fetching_) {
        fetchTECAsync(utc);
    }
}

size_t DataManager::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

bool DataManager::fetchURL(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "[DataManager] curl error: %s\n", curl_easy_strerror(res));
        return false;
    }

    if (httpCode != 200) {
        fprintf(stderr, "[DataManager] HTTP %ld for %s\n", httpCode, url.c_str());
        return false;
    }

    return true;
}

bool DataManager::downloadTECFile(const std::string& url, const std::string& destPath) {
    FILE* fp = fopen(destPath.c_str(), "wb");
    if (!fp) return false;

    CURL* curl = curl_easy_init();
    if (!curl) { fclose(fp); return false; }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (res != CURLE_OK) {
        fprintf(stderr, "[DataManager] Download failed: %s\n", curl_easy_strerror(res));
        fs::remove(destPath);
        return false;
    }

    return true;
}

void DataManager::fetchTECAsync(double utc, FetchCallback callback) {
    if (fetching_) return;
    fetching_ = true;
    lastFetchTime_ = utc;

    // Run fetch in a simple thread-like manner (blocking for now)
    // In production, use std::thread
    printf("[DataManager] Fetching TEC data...\n");

    // Try ESA Space Weather API first
    // https://weather.spacedata.copernicus.eu/swagger-ui.html
    std::string baseUrl = "https://weather.spacedata.copernicus.eu/api/v1/";

    // For now, generate simulated TEC data
    // Real implementation would fetch NetCDF from ESA and parse it
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        tecData_.width = 72;   // 5-degree resolution
        tecData_.height = 36;
        tecData_.values.resize(tecData_.width * tecData_.height);
        tecData_.timestamp = utc;
        tecData_.valid = true;

        // Simulated diurnal TEC pattern
        for (int lat = 0; lat < tecData_.height; lat++) {
            for (int lon = 0; lon < tecData_.width; lon++) {
                float latDeg = -90.0f + lat * 5.0f;
                float lonDeg = -180.0f + lon * 5.0f;

                // Peak at equator, ~14 LT
                float eqFactor = expf(-powf(latDeg / 30.0f, 2.0f));
                float hourAngle = fmodf((float)(utc / 3600.0) + lonDeg / 15.0f + 12.0f, 24.0f) - 12.0f;
                float diurnal = expf(-powf(hourAngle / 6.0f, 2.0f));

                float tecu = 10.0f + 40.0f * eqFactor * diurnal;
                tecData_.values[lat * tecData_.width + lon] = tecu;
            }
        }

        printf("[DataManager] TEC data generated: %dx%d\n", tecData_.width, tecData_.height);
    }

    fetching_ = false;
    if (callback) callback(true, "");
}
