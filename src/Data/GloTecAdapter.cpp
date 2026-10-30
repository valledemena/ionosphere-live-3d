#include "GloTecAdapter.h"
#include "Data/GloTecBackfill.h"
#include "Data/TecCache.h"
#include <curl/curl.h>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <algorithm>

// Indice GloTEC: lista de archivos geojson disponibles (el ultimo es el mas reciente).
#define GLOTEC_INDEX_URL "https://services.swpc.noaa.gov/products/glotec/geojson_2d_urt.json"

static void skipWsCl(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                            s[i] == '\r' || s[i] == ':' || s[i] == ',')) i++;
}

// Lee un numero flotante en s[i..] y devuelve su valor; avanza i hasta el fin del token.
static double readNumber(const std::string& s, size_t& i) {
    skipWsCl(s, i);
    size_t start = i;
    while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '.' ||
                            s[i] == '-' || s[i] == '+')) i++;
    if (i == start) return 0.0;
    return atof(s.substr(start, i - start).c_str());
}

// Lee una cadena entre comillas (sin escapes). Devuelve el contenido.
static bool readString(const std::string& s, size_t& i, std::string& out) {
    skipWsCl(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    i++;  // abrir comilla
    out.clear();
    while (i < s.size() && s[i] != '"') { out.push_back(s[i]); i++; }
    if (i >= s.size()) return false;
    i++;  // cerrar comilla
    return true;
}

// Localiza el inicio de un objeto "{..." desde 'from', devolviendo la posicion del '{'.
// Recorre strings correctamente.
static size_t findOpenBrace(const std::string& s, size_t from) {
    for (size_t i = from; i < s.size(); i++) {
        if (s[i] == '{') return i;
        if (s[i] == '"') { size_t j = i + 1; while (j < s.size() && s[j] != '"') j++; i = j; }
    }
    return std::string::npos;
}

// Dada la posicion de '{' de un objeto, extrae el valor numerico de la clave
// 'key' dentro de ese objeto (nivel directo). Devuelve true y pone en 'value'
// y 'after' la posicion tras el valor.
static bool objectNumber(const std::string& s, size_t open, size_t close,
                         const std::string& key, double& value, size_t& after) {
    size_t i = open + 1;
    while (i < close && i < s.size()) {
        std::string k;
        if (!readString(s, i, k)) { i++; continue; }
        size_t v = i;
        while (v < close && v < s.size() && s[v] != ':' && s[v] != ',') v++;
        // v apunta a ':' o ','; saltar ws y ':' 
        v++;
        skipWsCl(s, v);
        if (k == key) {
            value = readNumber(s, v);
            after = v;
            return true;
        }
        i = v;
    }
    return false;
}

GloTecAdapter::GloTecAdapter() = default;

void GloTecAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    grid_.width = gridW_;
    grid_.height = gridH_;
    grid_.values.assign(gridW_ * gridH_, 0.0f);
    grid_.valid = false;
    printf("[OK] GloTecAdapter initialized (%dx%d)\n", gridW_, gridH_);
}

size_t GloTecAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

// Retry-After de la ultima respuesta (segundos; 0 si no hay o no es numerico).
size_t GloTecAdapter::headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    const size_t len = size * nitems;
    double* out = (double*)userdata;
    std::string line(buffer, len);
    std::string lower = line;
    for (char& c : lower) c = (char)tolower((unsigned char)c);
    static const std::string kKey = "retry-after:";
    if (lower.compare(0, kKey.size(), kKey) == 0) {
        const char* v = line.c_str() + kKey.size();
        while (*v == ' ' || *v == '\t') ++v;
        *out = atof(v);
    }
    return len;
}

bool GloTecAdapter::fetchURL(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    response.clear();
    lastHttp_ = 0;
    lastCurl_ = 0;
    lastRetryAfter_ = 0.0;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &lastRetryAfter_);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");

    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);

    lastHttp_ = http;
    lastCurl_ = (int)res;
    if (res != CURLE_OK) {
        error_ = std::string("curl: ") + curl_easy_strerror(res);
        return false;
    }
    if (http != 200) {
        error_ = "HTTP " + std::to_string(http);
        return false;
    }
    return true;
}

bool GloTecAdapter::parseGeoJSON(const std::string& json) {
    const std::string& S = json;
    // Dimensiones aqui (no solo en init): el worker usa instancias sin init().
    grid_.width = gridW_;
    grid_.height = gridH_;
    grid_.values.assign(gridW_ * gridH_, 0.0f);

    // Cada feature: "type":"Feature","geometry":{"type":"Point","coordinates":[lon,lat]},
    //               "properties":{"tec":..,"anomaly":..,"hmF2":..,"NmF2":..,"quality_flag":..}
    // Orden probado: barrido por lat desde el sur (-90), luego lon ascendente.
    size_t latCount[72] = {0};  // numero de celdas rellenadas por banda de lat
    size_t pos = 0;
    bool anyError = false;
    while (true) {
        auto fc = S.find("\"Feature\"", pos);
        if (fc == std::string::npos) break;

        auto cStart = S.find("\"coordinates\"", fc);
        auto arrOpen = S.find('[', cStart);
        if (arrOpen == std::string::npos) { anyError = true; break; }
        size_t a = arrOpen + 1;
        double lon = readNumber(S, a);
        double lat = readNumber(S, a);

        auto fEnd = S.find("},\"properties\"", fc);
        auto closeObj = S.find('}', cStart);
        if (closeObj == std::string::npos || closeObj == 0) { anyError = true; break; }

        // Buscar el 'tec' dentro de properties de esta feature: entre closeObj y el
        // siguiente '"type"' de la siguiente feature (o fin).
        auto nextFeat = S.find("\"Feature\"", fc + 1);
        if (nextFeat == std::string::npos) nextFeat = S.size();
        auto tecK = S.find("\"tec\"", closeObj);
        // El bloque de metadatos "tec":{...} esta al final tras las features;
        // nos detenemos si pasamos de donde empieza el siguiente feature de properties.
        if (tecK == std::string::npos || tecK > nextFeat) { break; }  // sin mas datos de features

        auto tcol = S.find(':', tecK);
        size_t tv = tcol + 1;
        double tec = readNumber(S, tv);

        // Calcular indice de celda. GloTEC: lon -177.5..177.5 (72), lat -88.75..88.75 (72)
        int lonIdx = (int)llround((lon + 177.5) / 5.0);
        int latG = (int)llround((lat + 88.75) / 2.5);
        lonIdx = std::max(0, std::min(gridW_ - 1, lonIdx));
        latG   = std::max(0, std::min(gridH_ - 1, latG));
        int idx = latG * gridW_ + lonIdx;
        if (idx < (int)grid_.values.size()) {
            grid_.values[idx] = (float)tec;
            latCount[latG]++;
        }

        pos = fc + 1;
    }

    // Validar cobertura: cada banda de lat debe tener ~72 celdas (no exigimos 72 exacto
    // porque lon -180 cae fuera del rango GloTEC -177.5..177.5).
    size_t filled = 0;
    for (int i = 0; i < gridH_; i++) filled += std::min<size_t>(latCount[i], (size_t)gridW_);
    return !anyError && filled > 0;
}

static double gloIsoToEpoch(const std::string& t) {
    if (t.size() < 19) return 0.0;
    int Y  = atoi(t.substr(0, 4).c_str());
    int Mo = atoi(t.substr(5, 2).c_str());
    int D  = atoi(t.substr(8, 2).c_str());
    int H  = atoi(t.substr(11, 2).c_str());
    int Mi = atoi(t.substr(14, 2).c_str());
    int S  = atoi(t.substr(17, 2).c_str());
    auto daysFromCivil = [](int y, int m, int dd) -> long long {
        y -= m <= 2;
        long long era = (y >= 0 ? y : y - 399) / 400;
        unsigned yoe = (unsigned)(y - era * 400);
        unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
        unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + (long long)doe - 719468;
    };
    return (double)daysFromCivil(Y, Mo, D) * 86400.0 + H * 3600.0 + Mi * 60.0 + S;
}

// Extrae "url" y "time_tag" de cada objeto del indice:
// [{"url":"...","time_tag":"..."}, ...] (orden cronologico).
bool GloTecAdapter::fetchIndex(std::vector<GloTecFrame>& out) {
    out.clear();
    std::string index;
    if (!fetchURL(GLOTEC_INDEX_URL, index)) {
        fprintf(stderr, "[GloTecAdapter] Index failed: %s\n", error_.c_str());
        return false;
    }
    size_t pos = 0;
    while (true) {
        size_t open = findOpenBrace(index, pos);
        if (open == std::string::npos) break;
        // fin del objeto: buscar cierre simple (objetos planos, sin anidar)
        size_t close = index.find('}', open);
        if (close == std::string::npos) break;
        // leer pares "clave":"valor" dentro de [open, close]
        std::string url, ts;
        size_t i = open + 1;
        while (i < close) {
            std::string k;
            if (!readString(index, i, k)) { i++; continue; }
            size_t v = i;
            while (v < close && index[v] != ':') v++;
            v++;
            std::string val;
            if (!readString(index, v, val)) { i = v; continue; }
            if (k == "url") url = val;
            else if (k == "time_tag") ts = val;
            i = v;
        }
        if (!url.empty()) {
            GloTecFrame f;
            f.url = url;
            f.timeUtc = gloIsoToEpoch(ts);
            out.push_back(f);
        }
        pos = close + 1;
    }
    std::sort(out.begin(), out.end(),
              [](const GloTecFrame& a, const GloTecFrame& b) { return a.timeUtc < b.timeUtc; });
    printf("[GloTecAdapter] Index OK: %d frames\n", (int)out.size());
    return !out.empty();
}

bool GloTecAdapter::fetchFrame(const std::string& url, double timeUtc, GridData& out) {
    std::string full = (!url.empty() && url[0] == '/') ? "https://services.swpc.noaa.gov" + url : url;
    std::string js;
    if (!fetchURL(full, js)) return false;
    if (!parseGeoJSON(js)) return false;
    out = grid_;
    out.timestamp = (timeUtc > 0.0) ? timeUtc : grid_.timestamp;
    out.valid = true;
    return true;
}

bool GloTecAdapter::backfillFrom(const std::vector<GloTecFrame>& index,
                                 const std::vector<double>& cachedEpochs,
                                 double utc, int depthSlots,
                                 const std::function<void(const GridData&, double)>& onFrame,
                                 GridData* newestOut, BackfillStats* stats) {
    std::vector<glotbf::IndexFrame> pure;
    pure.reserve(index.size());
    for (const GloTecFrame& f : index) pure.push_back({f.url, f.timeUtc});

    glotbf::Options opt;
    opt.depthSlots = depthSlots > 0 ? depthSlots : glotbf::kDepth6h;

    bool haveNewest = false;
    auto transport = [this](const std::string& url) {
        glotbf::Response r;
        std::string body;
        fetchURL(url, body);
        r.http = lastHttp_;
        r.transportOk = (lastCurl_ == 0);
        r.retryAfterSec = lastRetryAfter_;
        r.body = std::move(body);
        // C1b/medido: bytes de red reales de ESTA peticion, en vivo (si App
        // provee un sumidero). Cuerpo vacio por fallo de transporte = 0.
        if (netBytesSink_) netBytesSink_->fetch_add((long long)r.body.size());
        return r;
    };
    auto store = [this, &onFrame, newestOut, &haveNewest](
                     const glotbf::IndexFrame& f, const std::string& body) {
        if (!parseGeoJSON(body)) return false;
        GridData g = grid_;
        g.timestamp = f.timeUtc;
        g.valid = true;
        if (onFrame) {
            onFrame(g, f.timeUtc);
        } else {
            teccache::Frame tf;
            tf.epoch = f.timeUtc;
            tf.w = g.width;
            tf.h = g.height;
            tf.v = g.values;
            teccache::saveFrame(cacheDir_, tf);  // best-effort
            if (!haveNewest) {
                if (newestOut) *newestOut = g;
                haveNewest = true;
            }
        }
        return true;
    };
    auto sleepFn = [](double seconds) {
        if (seconds <= 0.0) return;
        std::this_thread::sleep_for(
            std::chrono::milliseconds((long long)(seconds * 1000.0)));
    };
    auto stopFn = [this]() { return stop_ && stop_->load(); };

    const glotbf::Result res =
        glotbf::run(pure, cachedEpochs, utc, opt, transport, store, sleepFn, stopFn);

    if (stats) {
        stats->planned = (int)res.requested.size();
        stats->cached = res.skippedCached;
        stats->stored = (int)res.stored.size();
        stats->netBytes = res.netBytes;
    }
    if (res.stoppedOn404) error_ = "backfill: 3 x 404 seguidos";
    else if (res.stoppedOn5xx) error_ = "backfill: 5xx del servidor";
    else if (res.stoppedOn429) error_ = "backfill: 3 x 429 seguidos";
    else if (res.stoppedOnRetryAfter) error_ = "backfill: Retry-After > tope";
    else if (res.stoppedOnTransport) error_ = "backfill: 3 fallos de transporte";
    else if (res.stoppedByCaller) error_ = "backfill interrumpido";
    else error_.clear();

    return !res.stored.empty();
}

bool GloTecAdapter::fetchTEC(double utc, int depthSlots /*=36*/) {
    // C1b: el worker pide una profundidad pequena (huecos recientes); la
    // precarga de App hace el backfill largo en su propio hilo.
    std::vector<GloTecFrame> raw;
    if (!fetchIndex(raw)) return false;
    if (raw.empty()) { error_ = "indice vacio"; return false; }

    teccache::ScanResult scan = teccache::loadCached(cacheDir_);
    std::vector<double> cached;
    cached.reserve(scan.frames.size());
    for (const teccache::Frame& f : scan.frames) cached.push_back(f.epoch);

    GridData newest;
    if (backfillFrom(raw, cached, utc, depthSlots, {}, &newest, nullptr)) {
        grid_ = newest;
        grid_.valid = true;
        lastFetchUtc_ = utc;
        return true;
    }
    // Todo estaba en cache: se sirve el frame mas nuevo del disco.
    if (!scan.frames.empty()) {
        const teccache::Frame& nf = scan.frames.back();
        grid_.width = nf.w;
        grid_.height = nf.h;
        grid_.values = nf.v;
        grid_.timestamp = nf.epoch;
        grid_.valid = true;
        lastFetchUtc_ = utc;
        return true;
    }
    return false;
}