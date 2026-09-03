#pragma once
#include <string>
#include <vector>

struct EsaTECData {
    int width = 0;
    int height = 0;
    std::vector<float> values;  // TECU
    double timestamp = 0.0;
    bool valid = false;
};

class EsaAdapter {
public:
    void init();
    bool fetchTEC(double utc);
    bool loadCachedData(const std::string& path);
    bool saveCachedData(const std::string& path) const;
    const EsaTECData& getData() const { return data_; }

private:
    EsaTECData data_;
    std::string cacheDir_;
};
