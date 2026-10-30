#include "EbroAdapter.h"
#include <curl/curl.h>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <thread>
#include <vector>
#include <string>

#define EBRO_FTP_BASE "ftp://ftpoeb.obsebre.es/ionospheric/"
#define EBRO_USERPWD  "anonymous:m@obs.es"

void EbroAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    latest_ = EbroSample{};
    printf("[OK] EbroAdapter initialized (FTP Ebre EB040)\n");
}

size_t EbroAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool EbroAdapter::fetchFile(const std::string& fname, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    std::string url = std::string(EBRO_FTP_BASE) + fname;
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERPWD, EBRO_USERPWD);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    std::lock_guard<std::mutex> lk(mutex_);
    if (res != CURLE_OK) { error_ = std::string("ftp: ") + curl_easy_strerror(res); return false; }
    if (response.empty()) { error_ = "ftp: empty " + fname; return false; }
    return true;
}

std::string EbroAdapter::saoFilename(double utcMinusMin) {
    std::time_t t = (std::time_t)(utcMinusMin);
    std::tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    int mm = (tmv.tm_min / 5) * 5;  // redondeo a 5 min como fof2-monitor
    char buf[64];
    snprintf(buf, sizeof(buf), "EB040_%04d%03d%02d%02d01.SAO",
             tmv.tm_year + 1900, tmv.tm_yday + 1, tmv.tm_hour, mm);
    return std::string(buf);
}

static float sfield(const std::vector<std::string>& L, size_t k) {
    if (k >= L.size()) return ebro::nan();
    const std::string& v = L[k];
    if (v.empty()) return ebro::nan();
    float f = (float)atof(v.c_str());
    if (fabsf(f) > 900.0f) return ebro::nan();  // 9999 = N/A
    return f;
}

bool EbroAdapter::parseSAO(const std::string& text, const std::string& fname, EbroSample& out) {
    out = EbroSample{};
    // Quitar \r y partir en lineas.
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : text) {
            if (c == '\r') continue;
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    size_t ff = lines.size();
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].compare(0, 2, "FF") == 0) { ff = i; break; }
    }
    if (ff + 3 >= lines.size()) return false;

    auto split8 = [](const std::string& ln) {
        std::vector<std::string> f;
        for (size_t j = 0; j < ln.size(); j += 8) {
            std::string s = ln.substr(j, 8);
            size_t a = s.find_first_not_of(" \t");
            size_t b = s.find_last_not_of(" \t");
            f.push_back(a == std::string::npos ? "" : s.substr(a, b - a + 1));
        }
        return f;
    };
    std::vector<std::string> L6 = split8(lines[ff + 1]);
    std::vector<std::string> L8 = split8(lines[ff + 3]);

    out.foF2 = sfield(L6, 0);
    out.foF1 = sfield(L6, 1);
    out.foE  = sfield(L6, 2);
    out.muf  = sfield(L6, 3);
    out.foEs = sfield(L6, 5);
    out.fmin = sfield(L6, 7);
    out.fxI  = sfield(L6, 9);
    out.hF   = sfield(L6, 10);
    out.hF2  = sfield(L6, 11);
    out.hE   = sfield(L6, 12);
    out.hEs  = sfield(L6, 13);
    out.hmF2 = sfield(L8, 1);
    out.B0   = sfield(L8, 10);
    out.B1   = sfield(L8, 11);
    if (!std::isnan(out.muf) && !std::isnan(out.foF2) && out.foF2 > 0.0f)
        out.mD = out.muf / out.foF2;

    // Timestamp del nombre: EB040_YYYYDDDHHMM01.SAO
    {
        std::string b = fname;
        size_t p = b.find("EB040_");
        if (p != std::string::npos) b = b.substr(p + 6);
        size_t d = b.find('.');
        if (d != std::string::npos) b = b.substr(0, d);
        if (b.size() >= 13) {
            int Y = atoi(b.substr(0, 4).c_str());
            int doy = atoi(b.substr(4, 3).c_str());
            int H = atoi(b.substr(7, 2).c_str());
            int M = atoi(b.substr(9, 2).c_str());
            // dias desde epoch para 1-ene-Y + (doy-1)
            auto daysFromCivil = [](int y, int m, int dd) -> long long {
                y -= m <= 2;
                long long era = (y >= 0 ? y : y - 399) / 400;
                unsigned yoe = (unsigned)(y - era * 400);
                unsigned ddoy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
                unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + ddoy;
                return era * 146097 + (long long)doe - 719468;
            };
            out.timeUtc = (double)daysFromCivil(Y, 1, 1) * 86400.0 +
                          (double)(doy - 1) * 86400.0 + H * 3600.0 + M * 60.0;
        }
    }

    out.sourceFile = fname;
    out.valid = !std::isnan(out.foF2) || !std::isnan(out.muf) || !std::isnan(out.hmF2);
    return out.valid;
}

bool EbroAdapter::fetch(double utc) {
    // Candidatos de 5 min, mas reciente primero (12 = 1 h atras).
    for (int i = 0; i < 12; i++) {
        std::string fn = saoFilename(utc - i * 300.0);
        std::string body;
        if (!fetchFile(fn, body)) continue;   // 404/timeout -> probar anterior
        EbroSample s;
        if (parseSAO(body, fn, s)) {
            std::lock_guard<std::mutex> lk(mutex_);
            latest_ = s;
            lastFetchUtc_ = utc;
            error_.clear();
            printf("[Ebro] %s: foF2=%.2f MUF=%.1f hmF2=%.0f foEs=%.2f\n",
                   fn.c_str(), s.foF2, s.muf, s.hmF2, s.foEs);
            return true;
        }
    }
    if (error_.empty()) {
        std::lock_guard<std::mutex> lk(mutex_);
        error_ = "no SAO found (12 candidates)";
    }
    fprintf(stderr, "[Ebro] %s\n", getErrorMessage().c_str());
    return false;
}

void EbroAdapter::fetchAsync(double utc) {
    if (fetching_.exchange(true)) return;   // ya en curso
    std::thread([this, utc]() {
        fetch(utc);
        fetching_.store(false);
    }).detach();
}

EbroSample EbroAdapter::getLatest() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return latest_;
}

std::string EbroAdapter::getErrorMessage() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return error_;
}
