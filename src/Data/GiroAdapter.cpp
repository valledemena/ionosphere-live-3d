#include "GiroAdapter.h"
#include <curl/curl.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <ctime>
#include <regex>
#include <algorithm>

#define GIRO_DIDBASE_URL  "https://giro.uml.edu/didbase/scaled.php"
#define GIRO_CATALOG_URL  "https://lgdc.uml.edu/common/DIDBFastStationList?orderByField=ursiCode"

// Columnas de la respuesta de scaled.php (ver header FastChar. Verificado con
// respuesta real EB040 2026-09-04: M(D)=MUF/foF2 cuadra, B0/B1/fmin presentes):
// Time CS foF2 QD foF1 QD foE QD foEs QD fxI QD MUF(D) QD M(D) QD
// h'F2 QD h'F QD h'E QD h'Es QD hmF2 QD B0 QD B1 QD fmin QD
enum { COL_FOF2 = 2, COL_MUF = 12, COL_HMF2 = 24, COL_CS = 1, COL_TIME = 0,
       COL_FOF1 = 4, COL_FOE = 6, COL_FOES = 8, COL_FXI = 10,
       COL_HF2 = 16, COL_HF = 18, COL_HE = 20, COL_HES = 22,
       COL_B0 = 26, COL_B1 = 28, COL_FMIN = 30 };

GiroAdapter::GiroAdapter() {}

void GiroAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    printf("[OK] GiroAdapter initialized\n");
}

size_t GiroAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool GiroAdapter::fetchURL(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { return false; }
    return http == 200;
}

bool GiroAdapter::postURL(const std::string& url, const std::string& body, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
    // IMPORTANTE: NUNCA usar CURLOPT_CUSTOMREQUEST "POST" -> HTTP 405. Con
    // POSTFIELDS curl emite POST implicito que el servidor acepta (HTTP 200).
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return false;
    return http == 200;
}

std::string GiroAdapter::utcNowStr(int hoursAgo) {
    std::time_t t = std::time(nullptr) - hoursAgo * 3600;
    std::tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return std::string(buf);
}

bool GiroAdapter::fetchCatalog() {
    std::string html;
    if (!fetchURL(GIRO_CATALOG_URL, html)) {
        fprintf(stderr, "[GiroAdapter] No se pudo obtener el catalogo de estaciones\n");
        return false;
    }
    // Cada fila: ...ursiCode=XXXXX">XXXXX</a></big></td><td...><big>NAME</big></td>
    //            <td...><big> LAT</big></td><td...><big> LON</big></td>
    std::regex re(R"raw(ursiCode=([A-Z0-9]{5})">[^<]*</a></big></td><td[^>]*><big>([^<]*)</big></td><td[^>]*><big>\s*(-?[0-9.]+)</big></td><td[^>]*><big>\s*(-?[0-9.]+)</big></td>)raw");
    std::vector<StationSample> parsed;
    std::smatch m;
    std::string::const_iterator begin = html.cbegin();
    while (std::regex_search(begin, html.cend(), m, re)) {
        StationSample s;
        s.code = m[1].str();
        s.name = m[2].str();
        // quitar espacios multiples / tabs
        std::string nm = m[2].str();
        std::string clean;
        for (char c : nm) if (c != '\t') clean.push_back(c);
        s.name = clean;
        s.latDeg = (float)atof(m[3].str().c_str());
        float lon = (float)atof(m[4].str().c_str());
        if (lon > 180.0f) lon -= 360.0f;   // cat�logo en 0..360 E -> -180..180
        s.lonDeg = lon;
        s.valid = false;
        parsed.push_back(s);
        begin = m.suffix().first;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_ = std::move(parsed);
        progress_ = 0;
    }
    printf("[GiroAdapter] Catalogo cargado: %d estaciones\n", (int)samples_.size());
    return !samples_.empty();
}

bool GiroAdapter::parseScaled(const std::string& text, StationSample& out) {
    // Buscar la linea de datos mas reciente (lineas sin '#', con >=25 tokens).
    std::vector<std::vector<std::string>> rows;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string l = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        // quitar \r
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.empty() || l[0] == '#') continue;
        if (l.compare(0, 4, "Time") == 0) continue;
        // tokenizar por espacios
        std::vector<std::string> toks;
        std::string cur;
        for (char c : l) {
            if (c == ' ' || c == '\t') { if (!cur.empty()) { toks.push_back(cur); cur.clear(); } }
            else cur.push_back(c);
        }
        if (!cur.empty()) toks.push_back(cur);
        if (toks.size() >= 25) rows.push_back(std::move(toks));
    }
    if (rows.empty()) return false;

    auto numRow = [&](const std::vector<std::string>& row, int idx) -> float {
        if (idx >= (int)row.size()) return 0.0f;
        const std::string& v = row[idx];
        if (v.empty() || v == "---" || v == "//" || v == "__") return 0.0f;
        if (v.find_first_not_of("0123456789.-") != std::string::npos) return 0.0f;
        float f = (float)atof(v.c_str());
        if (f <= 0.0f) return 0.0f;   // descartar invalidos no positivos
        return f;
    };
    auto rowTime = [](const std::vector<std::string>& row) -> double {
        // "2026-09-04T21:30:01.000Z" -> epoch (19 primeros chars).
        const std::string& t = row[0];
        if (t.size() < 19) return 0.0;
        int Y  = atoi(t.substr(0, 4).c_str());
        int Mo = atoi(t.substr(5, 2).c_str());
        int D  = atoi(t.substr(8, 2).c_str());
        int H  = atoi(t.substr(11, 2).c_str());
        int Mi = atoi(t.substr(14, 2).c_str());
        int S  = atoi(t.substr(17, 2).c_str());
        auto daysFromCivil = [](int y, int m, int d) -> long long {
            y -= m <= 2;
            long long era = (y >= 0 ? y : y - 399) / 400;
            unsigned yoe = (unsigned)(y - era * 400);
            unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + (long long)doe - 719468;
        };
        return (double)daysFromCivil(Y, Mo, D) * 86400.0 + H * 3600.0 + Mi * 60.0 + S;
    };

    // Serie completa (cap 36 filas mas recientes) para replay + sparklines.
    {
        std::vector<GiroHistoryPoint> hist;
        size_t start = rows.size() > 36 ? rows.size() - 36 : 0;
        for (size_t k = start; k < rows.size(); k++) {
            const auto& rw = rows[k];
            GiroHistoryPoint p;
            p.timeUtc = rowTime(rw);
            p.foF2 = numRow(rw, COL_FOF2);
            p.muf  = numRow(rw, COL_MUF);
            p.hmF2 = numRow(rw, COL_HMF2);
            p.foE  = numRow(rw, COL_FOE);
            p.foEs = numRow(rw, COL_FOES);
            p.B0   = numRow(rw, COL_B0);
            p.B1   = numRow(rw, COL_B1);
            p.fmin = numRow(rw, COL_FMIN);
            hist.push_back(p);
        }
        out.history = std::move(hist);
    }

    const auto& r = rows.back();   // la fila mas reciente
    auto num = [&](int idx) -> float { return numRow(r, idx); };

    float foF2 = num(COL_FOF2);
    float muf  = num(COL_MUF);
    float hmF2 = num(COL_HMF2);
    float cs   = num(COL_CS);

    if (foF2 <= 0.0f && muf <= 0.0f && hmF2 <= 0.0f) return false;

    out.timeUtc = out.history.empty() ? 0.0 : out.history.back().timeUtc;
    out.foF2 = foF2;
    out.muf  = muf;
    out.hmF2 = hmF2;
    out.cs   = (int)cs;
    // Columnas extendidas (0 si ausentes esa hora, p.ej. foF1/foE de noche).
    out.foF1 = num(COL_FOF1);
    out.foE  = num(COL_FOE);
    out.foEs = num(COL_FOES);
    out.fxI  = num(COL_FXI);
    out.hF2  = num(COL_HF2);
    out.hF   = num(COL_HF);
    out.hE   = num(COL_HE);
    out.hEs  = num(COL_HES);
    out.B0   = num(COL_B0);
    out.B1   = num(COL_B1);
    out.fmin = num(COL_FMIN);
    // NmF2 [m^-3] = 1.24e10 * (foF2[MHz])^2  (foF2=10 MHz -> 1.24e12).
    out.nmF2 = (foF2 > 0.0f) ? (float)(1.24e10 * (double)foF2 * (double)foF2) : 0.0f;
    out.valid = true;
    return true;
}

bool GiroAdapter::fetchStationsSync(double utc) {
    std::vector<StationSample> cat;
    { std::lock_guard<std::mutex> lk(mutex_); cat = samples_; }
    if (cat.empty()) {
        if (!fetchCatalog()) return false;
        { std::lock_guard<std::mutex> lk(mutex_); cat = samples_; }
    }

    unsigned nthreads = std::thread::hardware_concurrency();
    if (nthreads == 0) nthreads = 4;
    if (nthreads > 16) nthreads = 16;

    std::atomic<int> next{0};
    std::atomic<int> ok{0};
    int total = (int)cat.size();

    auto worker = [&]() {
        for (;;) {
            int i = next.fetch_add(1);
            if (i >= total) break;
            StationSample base = cat[i];
            StationSample s = base;
            std::string body = "location=" + base.code +
                               "&date_start=" + utcNowStr(2) +
                               "&date_end=" + utcNowStr(0) +
                               "&DMUF=3000&query_submit=Search";
            const char* chars[] = {"0","1","2","3","7","8","9","10","11","12","13","17","25","26","33"};
            for (const char* c : chars) body += "&chosenchars%5B%5D=" + std::string(c);
            std::string resp;
            if (postURL(GIRO_DIDBASE_URL, body, resp) && parseScaled(resp, s)) ok++;
            { std::lock_guard<std::mutex> lk(mutex_);
              for (auto& e : samples_) if (e.code == base.code) e = s; }
            progress_ = (int)(100.0f * (next.load()) / total);
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(nthreads);
    for (unsigned t = 0; t < nthreads; t++) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    lastFetchUtc_ = (utc > 0) ? utc : (double)std::time(nullptr);
    printf("[GiroAdapter] Sincrono: %d/%d estaciones con datos\n", ok.load(), total);
    return ok.load() > 0;
}

void GiroAdapter::fetchStationsAsync() {
    if (fetching_.exchange(true)) return;   // ya en curso
    std::thread([this]() {
        std::vector<StationSample> cat;
        { std::lock_guard<std::mutex> lk(mutex_); cat = samples_; }
        if (cat.empty()) {
            if (!fetchCatalog()) { fetching_.store(false); return; }
            { std::lock_guard<std::mutex> lk(mutex_); cat = samples_; }
        }

        unsigned nthreads = std::thread::hardware_concurrency();
        if (nthreads == 0) nthreads = 4;
        if (nthreads > 16) nthreads = 16;

        std::atomic<int> next{0};
        std::atomic<int> ok{0};
        int total = (int)cat.size();

        auto worker = [&]() {
            for (;;) {
                int i = next.fetch_add(1);
                if (i >= total) break;
                StationSample base = cat[i];
                StationSample s = base;
                std::string body = "location=" + base.code +
                                   "&date_start=" + utcNowStr(2) +
                                   "&date_end=" + utcNowStr(0) +
                                   "&DMUF=3000&query_submit=Search";
                const char* chars[] = {"0","1","2","3","7","8","9","10","11","12","13","17","25","26","33"};
                for (const char* c : chars) body += "&chosenchars%5B%5D=" + std::string(c);
                std::string resp;
                if (postURL(GIRO_DIDBASE_URL, body, resp) && parseScaled(resp, s)) ok++;
                { std::lock_guard<std::mutex> lk(mutex_);
                  for (auto& e : samples_) if (e.code == base.code) e = s; }
                progress_ = (int)(100.0f * next.load() / total);
            }
        };

        std::vector<std::thread> pool;
        pool.reserve(nthreads);
        for (unsigned t = 0; t < nthreads; t++) pool.emplace_back(worker);
        for (auto& t : pool) t.join();

        lastFetchUtc_ = (double)std::time(nullptr);
        printf("[GiroAdapter] Async completado: %d/%d estaciones con datos\n", ok.load(), total);
        fetching_.store(false);
    }).detach();
}

std::vector<StationSample> GiroAdapter::copySamples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_;
}

float GiroAdapter::getDataIndicator() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (samples_.empty()) return 0.0f;
    int v = 0;
    for (const auto& s : samples_) if (s.valid) v++;
    return 100.0f * (float)v / (float)samples_.size();
}

float GiroAdapter::getLastUpdateAge() const {
    double now = (double)std::time(nullptr);
    return (float)(now - lastFetchUtc_);
}

std::vector<StationSample> GiroAdapter::sampleHistoryAt(double epoch) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto lerp = [](float a, float b, double f) -> float {
        if (a > 0.0f && b > 0.0f) return (float)(a + (b - a) * f);
        if (f < 0.5) return a > 0.0f ? a : b;
        return b > 0.0f ? b : a;
    };
    auto fromHist = [](StationSample& c, const GiroHistoryPoint& p) {
        c.foF2 = p.foF2; c.muf = p.muf; c.hmF2 = p.hmF2;
        c.foE = p.foE; c.foEs = p.foEs; c.B0 = p.B0; c.B1 = p.B1; c.fmin = p.fmin;
        c.nmF2 = (p.foF2 > 0.0f) ? (float)(1.24e10 * (double)p.foF2 * (double)p.foF2) : 0.0f;
        c.timeUtc = p.timeUtc;
    };
    std::vector<StationSample> out;
    out.reserve(samples_.size());
    for (const auto& s : samples_) {
        StationSample c = s;
        const auto& h = s.history;
        if (!h.empty()) {
            size_t i = 0;
            while (i + 1 < h.size() && h[i + 1].timeUtc <= epoch) i++;
            if (h.size() == 1 || epoch <= h.front().timeUtc) {
                fromHist(c, h.front());
            } else if (epoch >= h.back().timeUtc) {
                fromHist(c, h.back());
            } else {
                double t0 = h[i].timeUtc, t1 = h[i + 1].timeUtc;
                double f = (t1 > t0) ? (epoch - t0) / (t1 - t0) : 0.0;
                GiroHistoryPoint p;
                p.timeUtc = epoch;
                p.foF2 = lerp(h[i].foF2, h[i + 1].foF2, f);
                p.muf  = lerp(h[i].muf,  h[i + 1].muf, f);
                p.hmF2 = lerp(h[i].hmF2, h[i + 1].hmF2, f);
                p.foE  = lerp(h[i].foE,  h[i + 1].foE, f);
                p.foEs = lerp(h[i].foEs, h[i + 1].foEs, f);
                p.B0   = lerp(h[i].B0,   h[i + 1].B0, f);
                p.B1   = lerp(h[i].B1,   h[i + 1].B1, f);
                p.fmin = lerp(h[i].fmin, h[i + 1].fmin, f);
                fromHist(c, p);
            }
        }
        out.push_back(c);
    }
    return out;
}
