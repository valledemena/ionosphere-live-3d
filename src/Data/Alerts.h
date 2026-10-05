#pragma once
// S-5 alertas de propagacion (ruling 130 sec.6.5): motor de reglas puro.
// Sin GL, sin red, sin hilos, sin persistencia, sin sonido. Se evalua al
// publicar cada bundle (hilo principal) sobre los ultimos valores; la cola
// acotada de eventos ES la cola que un futuro hook de audio consumiria
// (modo demo). Todo literal en ASCII; umbrales del catalogo con nombre.
#include <algorithm>
#include <string>
#include <vector>

namespace alerts {

enum Level { GREEN = 0, AMBER = 1, RED = 2, OFF = 3 };  // OFF = deshabilitada
enum Rule { KP = 0, BZ = 1, XRAY = 2, MUF = 3, NRULES = 4 };

// Umbrales del catalogo (constantes nombradas).
constexpr float KP_AMBER = 4.0f, KP_RED = 5.0f;
constexpr float BZ_AMBER = -5.0f, BZ_RED = -10.0f;  // Bz sur negativo
constexpr int XRAY_AMBER_RANK = 2;                 // C
constexpr int XRAY_RED_RANK = 3;                   // M
constexpr float MUF_AMBER_DROP = 0.10f;            // vs mediana 24 h
constexpr float MUF_RED_DROP = 0.20f;              // vs mediana 24 h
constexpr int DEBOUNCE_N = 2;                      // muestras seguidas para rojo
constexpr size_t MUF_HIST_MAX = 288;               // 24 h a 5 min/muestra
constexpr size_t MUF_MIN_SAMPLES = 6;
constexpr double MUF_MIN_SPAN_SEC = 3.0 * 3600.0;
constexpr size_t LOG_MAX = 32;  // cola acotada de eventos en memoria

inline int xrayRank(char c) {
    if (c == 'X') return 4;
    if (c == 'M') return 3;
    if (c == 'C') return 2;
    if (c == 'B') return 1;
    return 0;  // 'A' y resto
}

struct Input {
    bool kpOk = false;
    float kp = 0.0f;
    bool bzOk = false;
    float bz = 0.0f;
    bool xrayOk = false;
    char xrayClass = 'A';
    bool mufOk = false;
    float muf = 0.0f;
    double utc = 0.0;  // tiempo de la muestra (s)
};

struct Event {
    double utc = 0.0;
    int rule = 0;
    int from = OFF;
    int to = OFF;
};

class Engine {
public:
    // Evalua una muestra (una publicacion de bundles). Barata y pura.
    void evaluate(const Input& in) {
        if (in.utc > lastUtc_) lastUtc_ = in.utc;
        setRule(KP, in.kpOk ? rawKp(in.kp) : OFF, in.utc);
        setRule(BZ, in.bzOk ? rawBz(in.bz) : OFF, in.utc);
        setRule(XRAY, in.xrayOk ? rawXray(in.xrayClass) : OFF, in.utc);
        setRule(MUF, mufLevel(in), in.utc);
    }

    Level overall() const {
        int w = GREEN;
        for (int r = 0; r < NRULES; ++r)
            if (levels_[r] != OFF && levels_[r] > w) w = levels_[r];
        return (Level)w;
    }
    Level level(int rule) const { return (Level)levels_[rule]; }
    const std::vector<Event>& events() const { return log_; }
    size_t mufSamples() const { return mufHist_.size(); }

    static const char* ruleName(int r) {
        return (r == KP) ? "Kp" : (r == BZ) ? "Bz" : (r == XRAY) ? "X-ray" : "MUF drop";
    }

private:
    static int rawKp(float kp) { return kp >= KP_RED ? RED : (kp >= KP_AMBER ? AMBER : GREEN); }
    static int rawBz(float bz) { return bz <= BZ_RED ? RED : (bz <= BZ_AMBER ? AMBER : GREEN); }
    static int rawXray(char c) {
        const int r = xrayRank(c);
        return r >= XRAY_RED_RANK ? RED : (r >= XRAY_AMBER_RANK ? AMBER : GREEN);
    }

    void setRule(int r, int raw, double utc) {
        int cur = levels_[r];
        int want;
        if (raw == OFF) {
            redCount_[r] = 0;
            want = OFF;
        } else if (raw == RED) {
            // Antiparpadeo: el rojo exige DEBOUNCE_N muestras seguidas;
            // mientras cuenta se muestra ambar sin latchear rojo.
            if (cur == RED || ++redCount_[r] >= DEBOUNCE_N)
                want = RED;
            else
                want = AMBER;
        } else {
            redCount_[r] = 0;
            want = raw;
        }
        if (want != cur) {
            levels_[r] = want;
            log_.push_back(Event{utc, r, cur, want});
            if (log_.size() > LOG_MAX) log_.erase(log_.begin());
        }
    }

    int mufLevel(const Input& in) {
        if (in.mufOk && in.utc > lastMufUtc_ && in.muf > 0.0f) {
            mufHist_.push_back(MufPt{in.utc, in.muf});
            lastMufUtc_ = in.utc;
            while (mufHist_.size() > MUF_HIST_MAX) mufHist_.erase(mufHist_.begin());
            while (!mufHist_.empty() && in.utc - mufHist_.front().utc > 24.0 * 3600.0)
                mufHist_.erase(mufHist_.begin());
        }
        // Historia insuficiente -> deshabilitada y declarado (no alerta).
        if (mufHist_.size() < MUF_MIN_SAMPLES) return OFF;
        if (mufHist_.back().utc - mufHist_.front().utc < MUF_MIN_SPAN_SEC) return OFF;
        std::vector<float> vs;
        vs.reserve(mufHist_.size());
        for (const MufPt& p : mufHist_) vs.push_back(p.v);
        const size_t mid = vs.size() / 2;
        std::nth_element(vs.begin(), vs.begin() + mid, vs.end());
        const float med = vs[mid];
        if (med <= 0.0f || !in.mufOk || in.muf <= 0.0f) return OFF;
        const float drop = (med - in.muf) / med;
        if (drop >= MUF_RED_DROP) return RED;
        if (drop >= MUF_AMBER_DROP) return AMBER;
        return GREEN;
    }

    struct MufPt {
        double utc = 0.0;
        float v = 0.0f;
    };

    int levels_[NRULES] = {OFF, OFF, OFF, OFF};
    int redCount_[NRULES] = {0, 0, 0, 0};
    std::vector<MufPt> mufHist_;
    double lastMufUtc_ = 0.0;
    double lastUtc_ = 0.0;
    std::vector<Event> log_;
};

}  // namespace alerts
