#include "RadioPropagationAdapter.h"
#include "GiroAdapter.h"
#include <curl/curl.h>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <algorithm>

// NOAA SWPC: no hay JSON publico directo para AE/S4/Faraday/TAI. Se intenta el
// Kp planetario (existe) como proxy de actividad para AE/TAI/Spread-F, y el
// resto cae a mock plausible con variacion diurna. MUF/LUF/FOT/Spread-F usan
// las muestras GIRO reales (getbest via GiroAdapter, desde M4).
#define RADIO_KP_URL "https://services.swpc.noaa.gov/json/planetary_k_index_1m.json"

void RadioPropagationAdapter::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    latest_ = RadioPropSample{};
    printf("[OK] RadioPropagationAdapter initialized\n");
}

size_t RadioPropagationAdapter::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

bool RadioPropagationAdapter::fetchURL(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IonosphereLive3D/1.0");
    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { error_ = std::string("curl: ") + curl_easy_strerror(res); return false; }
    if (http != 200) { error_ = "HTTP " + std::to_string(http); return false; }
    return true;
}

// Extrae el ultimo "kp_index" numerico del JSON planetario (array de objetos).
float RadioPropagationAdapter::tryFetchKpProxy() {
    std::string js;
    if (!fetchURL(RADIO_KP_URL, js)) return -1.0f;
    // Buscar ultima ocurrencia de "kp_index":<num>
    float last = -1.0f;
    size_t pos = 0;
    while (true) {
        pos = js.find("kp_index", pos);
        if (pos == std::string::npos) break;
        size_t c = js.find(':', pos);
        if (c == std::string::npos) break;
        c++;
        while (c < js.size() && (js[c] == ' ' || js[c] == '"' || js[c] == '\'')) c++;
        size_t e = c;
        while (e < js.size() && (isdigit((unsigned char)js[e]) || js[e] == '.' || js[e] == '-')) e++;
        if (e > c) {
            float v = (float)atof(js.substr(c, e - c).c_str());
            if (v >= 0.0f && v <= 9.0f) last = v;
        }
        pos = e;
    }
    return last;
}

float RadioPropagationAdapter::deriveFOT(float muf) {
    if (muf <= 0.0f) return 0.0f;
    return 0.85f * muf;  // definicion operacional ITU
}

float RadioPropagationAdapter::flareAbsorption(float xrayFlux, float dayFactor) {
    // M7 (plan 10): A ~ F^0.75 en banda 0.1-0.8 nm; F<=1e-7 (fondo A) -> 0.
    // Fuente unica del termino (P4): deriveLUF y el trigger del panel.
    return (xrayFlux > 1e-7f)
        ? 2.5f * powf(xrayFlux / 1e-6f, 0.75f) * (0.3f + 0.7f * dayFactor)
        : 0.0f;
}

float RadioPropagationAdapter::dRegionAbsorptionDb(float cosChi, float xrayFlux, float kp) {
    // M7 (plan 10): absorcion D mostrable (dB) = SID diurno + auroral polar.
    float dayFactor = fmaxf(0.0f, cosChi);
    float sid = (xrayFlux >= 1e-7f)
        ? 4.5f * powf(fmaxf(xrayFlux, 1e-7f) / 1e-6f, 0.75f) * dayFactor : 0.0f;
    float auroral = 0.35f * powf(fmaxf(kp, 0.0f), 2.1f);   // capa D polar
    return sid + auroral;
}

float RadioPropagationAdapter::deriveLUF(float muf, float foF2, float latDeg, float lonDeg, double utc,
                                         float xrayFlux, float kp) {
    // M'-1: firma intacta (sellada M7); latDeg/kp hoy no se usan en el cuerpo.
    // Casts para -Wall -Wextra (el TU igrf compila este fichero con warnings
    // estrictos): cero comportamiento.
    (void)latDeg;
    (void)kp;
    if (muf <= 0.0f) return 0.0f;
    // Absorcion de capa D: maxima de dia en el subsolar. Aproximacion simple:
    // hora local solar a partir de lon + UTC.
    std::time_t t = (std::time_t)utc;
    std::tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    float utcH = (float)tmv.tm_hour + (float)tmv.tm_min / 60.0f;
    float lstH = fmodf(utcH + lonDeg / 15.0f + 24.0f, 24.0f);
    float dayF = fmaxf(0.0f, cosf((lstH - 12.0f) * 3.14159265f / 12.0f));  // 1 al mediodia
    // M7 (plan 10, P2 sellado): base diurna con 0.7 fijo — el circuito
    // medio-latitud de la demo absorbe el cos(lat) anterior. latDeg queda
    // en la firma por compatibilidad de callers; kp por simetria con
    // deriveSpreadF (el modelo LUF del plan 10 no lo usa: la auroral vive
    // en dRegionAbsorptionDb para el aviso).
    float absorb = 1.2f + 2.8f * dayF * 0.7f;               // base diurna
    float flareAbsorb = flareAbsorption(xrayFlux, dayF);    // F<=1e-7 -> 0
    float luf = 0.35f * muf + 0.5f * (absorb + flareAbsorb);
    luf = fminf(fmaxf(luf, 1.5f), 20.0f);                   // clamp literal (plan 10)
    // Cap 0.9*foF2 SOLO en calma (P3 sellado): con flare activo el cap
    // mataria la senal del SID (aceptacion 2: LUF sube 4-5 MHz con F=1e-5).
    if (foF2 > 0.0f && flareAbsorb <= 0.0f)
        luf = fminf(luf, fmaxf(2.0f, 0.9f * foF2));
    return fmaxf(2.0f, luf);                                // suelo historico
}

float RadioPropagationAdapter::deriveSpreadF(float latDeg, float lonDeg, double utc, float kpProxy) {
    std::time_t t = (std::time_t)utc;
    std::tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    float utcH = (float)tmv.tm_hour + (float)tmv.tm_min / 60.0f;
    float lstH = fmodf(utcH + lonDeg / 15.0f + 24.0f, 24.0f);
    bool night = (lstH < 5.5f || lstH > 18.5f);
    float eq = expf(-powf(latDeg / 22.0f, 2.0f));       // pico ecuatorial
    float aur = expf(-powf((fabsf(latDeg) - 65.0f) / 8.0f, 2.0f));  // ovalo auroral
    float base = night ? (55.0f * eq + 35.0f * aur + 4.0f) : (6.0f * eq + 2.0f);
    if (kpProxy >= 0.0f) base *= (1.0f + 0.12f * kpProxy);  // mas Spread-F con actividad
    return fminf(100.0f, fmaxf(0.0f, base));
}

float RadioPropagationAdapter::deriveS4(float latDeg, float lonDeg, double utc) {
    std::time_t t = (std::time_t)utc;
    std::tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    float utcH = (float)tmv.tm_hour + (float)tmv.tm_min / 60.0f;
    float lstH = fmodf(utcH + lonDeg / 15.0f + 24.0f, 24.0f);
    // S4 post-puesta de sol ecuatorial (~19-01 LT), mas pico auroral leve.
    float post = expf(-powf((lstH - 21.5f) / 2.6f, 2.0f));
    if (lstH < 12.0f) post = expf(-powf((lstH + 24.0f - 21.5f) / 2.6f, 2.0f));
    float eq = expf(-powf(latDeg / 18.0f, 2.0f));
    float aur = 0.25f * expf(-powf((fabsf(latDeg) - 67.0f) / 7.0f, 2.0f));
    return fminf(1.0f, fmaxf(0.02f, 0.55f * post * eq + aur * post + 0.03f));
}

#include "Utils/Igrf.h"

// M'-1: Faraday REAL (camino vertical v1). B vertical IGRF-14 a 300 km;
// juguete 2.2*TEC*(0.4+0.6|..|) extinguido (censo 142 sec.4).
double RadioPropagationAdapter::stationOmegaRad(double latDeg, double lonDeg,
                                                double tecM2, double freqHz) {
    const igrf::Ned b = igrf::fieldNed(latDeg, lonDeg, igrf::FARADAY_EVAL_ALT_KM);
    return igrf::omegaRad((double)b.d * 1e-9, tecM2, freqHz);
}

float RadioPropagationAdapter::resolveTecSI(bool gridOk, float gridTecu,
                                             bool hasNmF2, float nmF2, int& rung) {
    if (gridOk && gridTecu > 0.0f) {
        rung = 0;
        return gridTecu * 1e16f;
    }
    if (hasNmF2 && nmF2 > 0.0f) {
        rung = 1;
        return nmF2 * 2e5f;  // slab ~200 km (patron App.cpp historico)
    }
    rung = 2;
    return 15.0f * 1e16f;  // mock declarado
}

float RadioPropagationAdapter::medianOf(std::vector<float> v) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

float RadioPropagationAdapter::stationValue(const StationSample& s, int var, double utc, float kpProxy,
                                            float xrayFlux) {
    switch (var) {
        case RADIO_LUF:      return deriveLUF(s.muf, s.foF2, s.latDeg, s.lonDeg, utc, xrayFlux, kpProxy);
        case RADIO_FOT:      return deriveFOT(s.muf);
        case RADIO_SPREAD_F: return deriveSpreadF(s.latDeg, s.lonDeg, utc, kpProxy);
        case RADIO_MUF:
        default:             return s.muf;
    }
}

void RadioPropagationAdapter::fillSimulated(double utc) {
    simulated_ = true;
    usedGiroReal_ = false;
    latest_.timeUtc = utc;
    latest_.muf = 18.0f + 4.0f * sinf((float)(utc * 0.00002));
    latest_.fot = deriveFOT(latest_.muf);
    latest_.luf = 7.5f;
    latest_.s4 = 0.15f;
    latest_.ae = 120.0f;
    latest_.tai = 0.8f;
    // Mock declarado SOBRE EL CAMINO REAL: lat 10 / 15 TECU / FOT del mock.
    latest_.faradayDeg =
        (float)(stationOmegaRad(10.0, 0.0, 15.0e16, (double)latest_.fot * 1e6) *
                57.29577951308232);
    latest_.spreadF = 12.0f;
    latest_.valid = true;
    aeHistory_.clear();
    s4History_.clear();
    for (int i = 0; i < 48; i++) {
        double tt = utc - (47 - i) * 1800.0;
        aeHistory_.push_back({tt, 90.0f + 60.0f * fabsf(sinf(i * 0.31f))});
        s4History_.push_back({tt, 0.08f + 0.10f * fabsf(sinf(i * 0.43f))});
    }
}

bool RadioPropagationAdapter::fetch(double utc, const std::vector<StationSample>& giroSamples, float xrayFlux) {
    // 1) Mediana MUF/foF2 de estaciones GIRO validas (datos reales getbest).
    std::vector<float> mufs, fofs;
    for (const auto& s : giroSamples) {
        if (!s.valid) continue;
        if (s.muf > 0.0f) mufs.push_back(s.muf);
        if (s.foF2 > 0.0f) fofs.push_back(s.foF2);
    }
    usedGiroReal_ = !mufs.empty();

    float mufMed = 0.0f;
    if (!mufs.empty()) {
        std::sort(mufs.begin(), mufs.end());
        mufMed = mufs[mufs.size() / 2];
    }

    // 2) Kp proxy NOAA (para AE/TAI/Spread-F). Si falla, kp=-1 y todo mock.
    float kp = tryFetchKpProxy();
    bool kpOk = (kp >= 0.0f);

    if (!usedGiroReal_ && !kpOk) {
        fprintf(stderr, "[RadioProp] Sin GIRO ni NOAA -> mock.\n");
        fillSimulated(utc);
        lastFetchUtc_ = utc;
        return false;
    }

    simulated_ = false;
    if (!usedGiroReal_) mufMed = 18.0f;  // fallback si solo hay Kp
    if (!kpOk) kp = 2.0f;

    latest_.timeUtc = utc;
    latest_.muf = mufMed;
    latest_.fot = deriveFOT(mufMed);
    // M7: LUF global con el termino SID del flujo del pase anterior y el kp
    // interno ya resuelto (kp mock 2.0 si NOAA fallo, linea ~203).
    latest_.luf = deriveLUF(mufMed, fofs.empty() ? mufMed * 0.4f : fofs[fofs.size()/2], 0.0f, 0.0f, utc,
                            xrayFlux, kp);

    // S4 global: mediana ecuatorial nocturna estimada
    latest_.s4 = 0.06f + 0.05f * kp + 0.04f * sinf((float)(utc * 0.00001));
    if (latest_.s4 < 0.03f) latest_.s4 = 0.03f;
    if (latest_.s4 > 0.9f) latest_.s4 = 0.9f;
    // AE ~ proxy lineal de Kp (quieto ~50, tormenta G3 ~800+)
    latest_.ae = 40.0f + 90.0f * kp + 12.0f * kp * kp * 0.2f;
    // TAI 0..3 (actividad TEC): escala con Kp + Spread-F diurno
    latest_.tai = fminf(3.0f, 0.3f + 0.32f * kp);
    // Faraday mediana: Omega REAL por estacion valida (su lat/lon + su cadena
    // TEC + su FOT) -> mediana, mismo patron que mufMed. En el worker la
    // cadena es nmF2->15 (el grid vive en el hilo principal, no se toca aqui).
    {
        std::vector<float> oms;
        for (const StationSample& st : giroSamples) {
            if (!st.valid || st.muf <= 0.0f) continue;
            int rung = -1;
            const float tecSI =
                resolveTecSI(false, 0.0f, st.nmF2 > 0.0f, st.nmF2, rung);
            const double fotHz = (double)deriveFOT(st.muf) * 1e6;
            if (fotHz <= 0.0) continue;
            oms.push_back((float)(stationOmegaRad(st.latDeg, st.lonDeg, tecSI, fotHz) *
                                  57.29577951308232));
        }
        if (!oms.empty()) latest_.faradayDeg = medianOf(oms);
        // Sin estaciones validas se conserva el valor previo (declarado).
    }
    latest_.spreadF = 8.0f + 4.0f * kp;
    if (latest_.spreadF > 60.0f) latest_.spreadF = 60.0f;
    latest_.valid = true;

    aeHistory_.clear();
    s4History_.clear();
    for (int i = 0; i < 48; i++) {
        double tt = utc - (47 - i) * 1800.0;
        float wob = 0.85f + 0.3f * fabsf(sinf(i * 0.37f));
        aeHistory_.push_back({tt, latest_.ae * wob});
        s4History_.push_back({tt, fmaxf(0.02f, latest_.s4 * wob)});
    }

    lastFetchUtc_ = utc;
    error_.clear();
    printf("[RadioProp] MUF=%.1f FOT=%.1f LUF=%.1f S4=%.2f AE=%.0f TAI=%.1f FR=%.0fdeg SpF=%.0f%% %s%s\n",
           latest_.muf, latest_.fot, latest_.luf, latest_.s4, latest_.ae,
           latest_.tai, latest_.faradayDeg, latest_.spreadF,
           usedGiroReal_ ? "(GIRO real) " : "(GIRO mock) ",
           kpOk ? "(Kp NOAA)" : "(Kp mock)");
    return usedGiroReal_;
}
