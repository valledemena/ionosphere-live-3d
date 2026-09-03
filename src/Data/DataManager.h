#pragma once
#include <string>

class DataManager {
public:
    void init() {}
    void update(double utc) {}
    bool fetchTEC(double utc) { return false; }
    bool fetchIonosondes(double utc) { return false; }
};
