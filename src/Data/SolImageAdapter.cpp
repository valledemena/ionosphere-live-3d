// Lote 4 / modulo B: imagenes solares reales (implementacion; ver
// SolImageAdapter.h para fuentes verificadas y formato del cache).
#include "Data/SolImageAdapter.h"

#include <curl/curl.h>

// La implementacion de stb_image la aporta src/main.cpp (unica TU con
// STB_IMAGE_IMPLEMENTATION); aqui solo se declaran las funciones.
#include <stb_image.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include <utility>

namespace {

// Nombres de fichero del cache, en el orden de SolProduct.
const char* const kCacheNames[2] = {"sol_lasco_c2.img", "sol_drap_global.img"};

// Epochs plausibles para la cabecera del cache: 2000-01-01 .. 2100-01-01.
constexpr double kMinEpoch = 946684800.0;
constexpr double kMaxEpoch = 4102444800.0;

// Limites de decodificacion (defensa ante bodies corruptos).
constexpr int kMaxDim = 4096;

void putU64le(unsigned char* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (unsigned char)((v >> (8 * i)) & 0xFFu);
}

uint64_t getU64le(const unsigned char* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

bool isPng(const unsigned char* b, size_t n) {
    static const unsigned char kSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return n >= 8 && std::memcmp(b, kSig, 8) == 0;
}

bool isJpeg(const unsigned char* b, size_t n) {
    return n >= 4 && b[0] == 0xFF && b[1] == 0xD8;
}

// Acumulador de curl con techo duro de bytes (el servidor puede mentir en
// Content-Length: el corte real lo hace el callback).
struct Sink {
    std::vector<unsigned char>* body = nullptr;
    bool overflow = false;
};

}  // namespace

void SolImageAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (int i = 0; i < 2; ++i) {
            images_[i] = SolImage{};
            images_[i].product = i;
            lastFetchUtc_[i] = 0.0;
            err_[i].clear();
        }
    }
    restoreCache();
    SolImage a = getImage(SolProduct::LascoC2);
    SolImage b = getImage(SolProduct::DrapGlobal);
    printf("[Sol] ready: lasco=%s drap=%s (cache dir '%s')\n",
           a.valid ? (a.fromCache ? "cached" : "live") : "none",
           b.valid ? (b.fromCache ? "cached" : "live") : "none", effectiveCacheDir().c_str());
}

const char* SolImageAdapter::urlFor(SolProduct product) {
    return product == SolProduct::DrapGlobal
               ? "https://services.swpc.noaa.gov/images/d-rap/global.png"
               : "https://soho.nascom.nasa.gov/data/realtime/c2/1024/latest.jpg";
}

const char* SolImageAdapter::nameFor(SolProduct product) {
    return product == SolProduct::DrapGlobal ? "D-RAP global" : "LASCO C2";
}

const char* SolImageAdapter::sourceNameFor(SolProduct product) {
    return product == SolProduct::DrapGlobal ? "NOAA SWPC D-RAP (model output)"
                                             : "SOHO/LASCO C2 (NASA/ESA)";
}

std::string SolImageAdapter::cacheFileName(SolProduct product) {
    return kCacheNames[product == SolProduct::DrapGlobal ? 1 : 0];
}

const char* SolImageAdapter::sourceTag(const SolImage& img) {
    if (!img.valid) return "none";
    return img.fromCache ? "cached" : "live";
}

size_t SolImageAdapter::writeCallback(char* ptr, size_t size, size_t nmemb,
                                      void* userdata) {
    Sink* sink = static_cast<Sink*>(userdata);
    const size_t total = size * nmemb;
    if (!sink || !sink->body) return 0;
    if (sink->body->size() + total > kMaxBodyBytes) {
        sink->overflow = true;
        return 0;  // aborta la transferencia (curl: CURLE_WRITE_ERROR)
    }
    sink->body->insert(sink->body->end(), (const unsigned char*)ptr,
                       (const unsigned char*)ptr + total);
    return total;
}

bool SolImageAdapter::download(const std::string& url, std::vector<unsigned char>& body,
                               std::string& err) const {
    body.clear();
    err.clear();
    CURL* curl = curl_easy_init();
    if (!curl) {
        err = "curl: init failed";
        return false;
    }
    Sink sink;
    sink.body = &body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)kMaxBodyBytes);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &SolImageAdapter::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    const CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);

    if (sink.overflow) {
        err = "body over 6 MB";
        body.clear();
        return false;
    }
    if (res == CURLE_FILESIZE_EXCEEDED) {
        err = "body over 6 MB";
        body.clear();
        return false;
    }
    if (res != CURLE_OK) {
        err = std::string("http: ") + curl_easy_strerror(res);
        body.clear();
        return false;
    }
    if (http != 200) {
        err = "http: status " + std::to_string((int)http);
        body.clear();
        return false;
    }
    if (body.empty()) {
        err = "http: empty body";
        return false;
    }
    return true;
}

bool SolImageAdapter::decodeImage(const std::vector<unsigned char>& bytes,
                                  SolImage& out) {
    out = SolImage{};
    if (bytes.size() < 8) return false;
    const unsigned char* b = bytes.data();
    // Solo formatos de las dos fuentes: nada de basura/HTML al decodificador.
    if (!isJpeg(b, bytes.size()) && !isPng(b, bytes.size())) return false;

    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load_from_memory(b, (int)bytes.size(), &w, &h, &ch, 3);
    if (!px) return false;
    if (w <= 0 || h <= 0 || w > kMaxDim || h > kMaxDim) {
        stbi_image_free(px);
        return false;
    }
    const size_t n = (size_t)w * (size_t)h * 3u;
    out.w = w;
    out.h = h;
    out.rgb.assign(px, px + n);
    stbi_image_free(px);
    out.valid = true;
    return true;
}

bool SolImageAdapter::writeCacheFile(const std::string& path,
                                     const std::vector<unsigned char>& body,
                                     double epoch, std::string* err) {
    if (err) err->clear();
    if (path.empty() || body.empty() || body.size() > kMaxBodyBytes) {
        if (err) *err = "cache: bad body";
        return false;
    }
    if (!std::isfinite(epoch) || epoch < kMinEpoch || epoch > kMaxEpoch) {
        if (err) *err = "cache: bad epoch";
        return false;
    }
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (err) *err = "cache: open for write failed";
        return false;
    }
    unsigned char hdr[kCacheHeaderBytes];
    putU64le(hdr, (uint64_t)std::llround(epoch));
    bool ok = std::fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    if (ok) ok = std::fwrite(body.data(), 1, body.size(), f) == body.size();
    if (std::fclose(f) != 0) ok = false;
    if (!ok) {
        std::filesystem::remove(path, ec);
        if (err) *err = "cache: write failed";
        return false;
    }
    return true;
}

bool SolImageAdapter::readCacheFile(const std::string& path,
                                    std::vector<unsigned char>& body, double& epoch) {
    body.clear();
    epoch = 0.0;
    if (path.empty()) return false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = std::fseek(f, 0, SEEK_END) == 0;
    long n = ok ? std::ftell(f) : -1;
    if (n < 0 || (size_t)n <= kCacheHeaderBytes ||
        (size_t)n > kCacheHeaderBytes + kMaxBodyBytes) {
        std::fclose(f);
        return false;
    }
    ok = std::fseek(f, 0, SEEK_SET) == 0;
    std::vector<unsigned char> raw;
    if (ok) {
        raw.resize((size_t)n);
        ok = std::fread(raw.data(), 1, raw.size(), f) == raw.size();
    }
    std::fclose(f);
    if (!ok) return false;

    const double ts = (double)getU64le(raw.data());
    if (!std::isfinite(ts) || ts < kMinEpoch || ts > kMaxEpoch) return false;

    epoch = ts;
    body.assign(raw.begin() + (std::ptrdiff_t)kCacheHeaderBytes, raw.end());
    return !body.empty();
}

void SolImageAdapter::restoreCache() {
    const std::string dir = effectiveCacheDir();
    if (dir.empty()) return;   // defensa: effectiveCacheDir nunca vacio hoy
    for (int i = 0; i < 2; ++i) {
        const SolProduct prod = (SolProduct)i;
        const std::string path = dir + "/" + cacheFileName(prod);
        std::vector<unsigned char> body;
        double epoch = 0.0;
        if (!readCacheFile(path, body, epoch)) continue;
        SolImage img;
        if (!decodeImage(body, img)) {
            fprintf(stderr, "[Sol] cache %s: cuerpo no decodificable, se ignora\n",
                    cacheFileName(prod).c_str());
            continue;
        }
        img.product = i;
        img.timeUtc = epoch;
        img.fromCache = true;
        const int w = img.w, h = img.h;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            images_[i] = std::move(img);
            lastFetchUtc_[i] = epoch;
            err_[i].clear();
        }
        printf("[Sol] cache %s %dx%d (epoch %.0f)\n", nameFor(prod), w, h, epoch);
    }
}

bool SolImageAdapter::saveCache(SolProduct product, const std::vector<unsigned char>& body,
                                double epoch) {
    const std::string dir = effectiveCacheDir();
    if (dir.empty()) return false;   // defensa: effectiveCacheDir nunca vacio hoy
    const std::string path = dir + "/" + cacheFileName(product);
    std::string err;
    if (!writeCacheFile(path, body, epoch, &err)) {
        fprintf(stderr, "[Sol] cache %s: %s\n", cacheFileName(product).c_str(),
                err.c_str());
        return false;
    }
    return true;
}

bool SolImageAdapter::fetch(double utc) {
    bool anyOk = false;
    for (int i = 0; i < 2; ++i) {
        const SolProduct prod = (SolProduct)i;
        std::vector<unsigned char> body;
        std::string err;
        if (!download(urlFor(prod), body, err)) {
            // Se conserva la imagen anterior (si habia): su hora real no se
            // toca, asi que la edad del caption sigue creciendo.
            std::lock_guard<std::mutex> lk(mutex_);
            err_[i] = err;
            fprintf(stderr, "[Sol] %s: %s\n", nameFor(prod), err.c_str());
            continue;
        }
        SolImage img;
        if (!decodeImage(body, img)) {
            std::lock_guard<std::mutex> lk(mutex_);
            err_[i] = "decode failed";
            fprintf(stderr, "[Sol] %s: decode failed (%d B)\n", nameFor(prod),
                    (int)body.size());
            continue;
        }
        img.product = i;
        img.timeUtc = utc;
        img.fromCache = false;
        const int w = img.w, h = img.h;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            images_[i] = std::move(img);
            lastFetchUtc_[i] = utc;
            err_[i].clear();
        }
        saveCache(prod, body, utc);
        anyOk = true;
        printf("[Sol] %s %dx%d live (%d B)\n", nameFor(prod), w, h, (int)body.size());
    }
    return anyOk;
}

void SolImageAdapter::fetchAsync(double utc) {
    if (fetching_.exchange(true)) return;  // ya hay una en curso
    std::thread([this, utc]() {
        fetch(utc);
        fetching_.store(false);
    }).detach();
}

SolImage SolImageAdapter::getImage(SolProduct product) const {
    std::lock_guard<std::mutex> lk(mutex_);
    return images_[product == SolProduct::DrapGlobal ? 1 : 0];
}

double SolImageAdapter::getLastFetchTime(SolProduct product) const {
    std::lock_guard<std::mutex> lk(mutex_);
    return lastFetchUtc_[product == SolProduct::DrapGlobal ? 1 : 0];
}

std::string SolImageAdapter::getErrorMessage() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::string out;
    for (int i = 0; i < 2; ++i) {
        if (err_[i].empty()) continue;
        if (!out.empty()) out += "; ";
        out += nameFor((SolProduct)i);
        out += ": ";
        out += err_[i];
    }
    return out;
}

std::string SolImageAdapter::getErrorMessage(SolProduct product) const {
    std::lock_guard<std::mutex> lk(mutex_);
    return err_[product == SolProduct::DrapGlobal ? 1 : 0];
}
