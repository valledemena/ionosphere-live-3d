#pragma once
#include <string>
#include <mutex>
#include <atomic>
#include <limits>

// Perfil ionosferico completo de EB040 (Roquetas) via FTP directo del
// Observatori de l'Ebre. Formato SAO4 (DPS-4D ARTIST 5), verificado en
// fof2-monitor/fof2_monitor.py::parse_sao cruzando SAO + Ion2Png:
//   L6 (1a linea tras FF), campos de 8 chars:
//     [0] foF2  [1] foF1  [2] foE  [3] MUF(D)  [4] foEsL  [5] foEs
//     [6] foEsH [7] fmin [9] fxI  [10] h'F [11] h'F2 [12] h'E [13] h'Es
//   L8 (3a linea tras FF): [1] hmF2  [10] B0  [11] B1
// 9999 = N/A -> NaN. M(D) = MUF(D)/foF2 (calculado).
//
// Fuente primaria (sin intermediarios): ftp://ftpoeb.obsebre.es/ionospheric/
//   ficheros EB040_YYYYDDDHHMM01.SAO cada 5 min (MM redondeado a 5).
// GIRO DIDBase scaled.php queda como fallback (ver GiroAdapter).
namespace ebro {
inline float nan() { return std::numeric_limits<float>::quiet_NaN(); }
}

struct EbroSample {
    float foF2 = ebro::nan();
    float foF1 = ebro::nan();
    float foE = ebro::nan();
    float muf = ebro::nan();    // MUF(D) directo del SAO
    float foEs = ebro::nan();
    float fmin = ebro::nan();
    float fxI = ebro::nan();
    float hF = ebro::nan();
    float hF2 = ebro::nan();
    float hE = ebro::nan();
    float hEs = ebro::nan();
    float hmF2 = ebro::nan();
    float B0 = ebro::nan();
    float B1 = ebro::nan();
    float mD = ebro::nan();     // M(D) = MUF/foF2
    double timeUtc = 0.0;       // del nombre de fichero
    bool valid = false;
    std::string sourceFile;
};

class EbroAdapter {
public:
    void init();
    // Prueba los ultimos 12 candidatos de 5 min (mas reciente primero).
    // Devuelve true con el primer SAO valido. Bloqueante (hasta ~12x8 s si
    // el servidor no responde: preferir fetchAsync desde la app).
    bool fetch(double utc);
    // Igual pero en hilo background (no bloquea el render). Safe concurrente.
    void fetchAsync(double utc);
    bool isFetching() const { return fetching_; }
    EbroSample getLatest() const;
    double getLastFetchTime() const { return lastFetchUtc_; }
    bool isSimulated() const { return !getLatest().valid; }
    const std::string& getErrorMessage() const { return error_; }

    static std::string saoFilename(double utcMinusMin);
    static bool parseSAO(const std::string& text, const std::string& fname, EbroSample& out);

private:
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    bool fetchFile(const std::string& fname, std::string& response);

    mutable std::mutex mutex_;
    EbroSample latest_;
    std::atomic<bool> fetching_{false};
    double lastFetchUtc_ = 0.0;
    std::string error_;
};
