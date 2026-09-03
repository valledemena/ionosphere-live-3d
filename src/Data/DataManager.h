#pragma once
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>

struct TECData {
    int width = 0;
    int height = 0;
    std::vector<float> values;  // TECU values, row-major [lat][lon]
    double timestamp = 0.0;
    bool valid = false;
};

class DataManager {
public:
    using FetchCallback = std::function<void(bool success, const std::string& error)>;

    DataManager();
    ~DataManager();

    void init();
    void update(double utc);
    void fetchTECAsync(double utc, FetchCallback callback = nullptr);
    bool isFetching() const { return fetching_; }
    const TECData& getTECData() const { return tecData_; }

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchURL(const std::string& url, std::string& response);
    bool downloadTECFile(const std::string& url, const std::string& destPath);

    std::atomic<bool> fetching_{false};
    std::mutex dataMutex_;
    TECData tecData_;
    std::string cacheDir_;
    double lastFetchTime_ = 0;
    double fetchInterval_ = 300.0;  // 5 minutes between fetches
};
