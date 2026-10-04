#include "GiroAdapter.h"
#include "Data/LgdcPacing.h"  // M-IRTAM-F2 (P3, C1): registra lanzamientos; pacing M4 intacto
#include "Data/LgdcTrace.h"  // Ciclo traza: log stderr con timestamp (solo-log)
#include <curl/curl.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <ctime>
#include <regex>
#include <algorithm>
#include <chrono>
#include <set>

#define GIRO_GETBEST_URL "https://lgdc.uml.edu/fastchar/getbest"
#define GIRO_CATALOG_URL "https://lgdc.uml.edu/common/DIDBFastStationList?orderByField=ursiCode"

// M4 (7.1): GET getbest por estacion con lista CSV FIJA de caracteres. El
// orden del CSV es el contrato del parser: en cada fila de la respuesta el
// valor del caracter k ocupa el token 2+2k (toks[0]=Time, toks[1]=CS); el
// servidor anade M(D) al final (token 22, se ignora). Verificado en vivo
// (EB040, 2026-09-08): filas de 24 tokens, "---" = ausente, h'xxx solo
// genera WARNING (P2: la tabla h' del panel lee EbroAdapter, intacta).
static const char* GETBEST_CHARS[] = {
    "foF2", "foF1", "foE", "foEs", "fxI", "hmF2", "B0", "B1", "fmin", "MUF(D)"
};
enum { GB_NCHARS = 10, GB_MIN_TOKENS = 2 + 2 * GB_NCHARS };
enum { GB_CS = 1, GB_FOF2 = 2, GB_FOF1 = 4, GB_FOE = 6, GB_FOES = 8,
       GB_FXI = 10, GB_HMF2 = 12, GB_B0 = 14, GB_B1 = 16, GB_FMIN = 18,
       GB_MUF = 20 };

// M4 (7.2, P4): subconjunto prioritario verificado en vivo (plan v2.1,
// Apendice B). Polling SOLO a estas estaciones; el resto del catalogo se
// mantiene para el dropdown/puntos (sin datos). EB040 (Roquetes) primera
// siempre (aceptacion 4). NOTA: el Apendice B lista 50 codigos aunque el
// texto del plan diga "51" - la lista manda.
static const char* PRIORITY_STATIONS[] = {
    "EB040", "EA036", "PQ052", "JR055", "KL154", "KI167", "FF051", "GM037",
    "AT138", "VT139", "OL246", "LD160", "MZ152", "RV149", "TR170", "RL052",
    "SO148", "TR169", "DB049", "EG931", "CS839", "BC840", "AC843", "EI764",
    "PF765", "AU930", "BVJ03", "SMK29", "TNJ20", "CAJ2M", "CXM9B", "BLJ03",
    "LA42Q", "DW41K", "CB53N", "TO535", "IC437", "DH224", "NI135", "XI434",
    "CS31K", "TV51R", "MA155", "MO155", "ML10L", "GR13L", "MU12K", "RM041",
    "NDA81", "TR0P2"
};
static const std::set<std::string>& prioritySet() {
    static const std::set<std::string> s(
        PRIORITY_STATIONS,
        PRIORITY_STATIONS + sizeof(PRIORITY_STATIONS) / sizeof(PRIORITY_STATIONS[0]));
    return s;
}

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

// M4 (7.1): GET getbest con UA identificable y timeout 12 s por estacion.
// charName = CSV con el orden FIJO de GETBEST_CHARS (contrato del parser).
bool GiroAdapter::getbestGet(const std::string& ursi, std::string& response) {
    std::string url = std::string(GIRO_GETBEST_URL) + "?ursiCode=" + ursi + "&charName=";
    for (int k = 0; k < GB_NCHARS; k++) {
        if (k) url += ',';
        url += GETBEST_CHARS[k];
    }
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);   // 7.1: 12 s por estacion
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ionosphere-live-3d/1.0 (educational)");
    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return false;
    return http == 200;
}

bool GiroAdapter::fetchCatalog() {
    std::string html;
    const bool catalogOk = fetchURL(GIRO_CATALOG_URL, html);
    // P3 C1: registra el lanzamiento en el gate (exito o fallo: hubo trafico).
    // Ciclo traza (solo-log): mismo instante para gate y traza.
    const long long catMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    lgdcpacing::recordLaunch(catMs);
    lgdctrace::trace("launch", "catalog");
    if (!catalogOk) {
        fprintf(stderr, "[GiroAdapter] Could not fetch station catalog\n");
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
    printf("[GiroAdapter] Catalog loaded: %d stations\n", (int)samples_.size());
    return !samples_.empty();
}

// ---- Nucleo de parseo (heredado del parser de scaled.php; M4 lo reutiliza) ----

static std::vector<std::string> tokenizeLine(const std::string& l) {
    // tokenizar por espacios
    std::vector<std::string> toks;
    std::string cur;
    for (char c : l) {
        if (c == ' ' || c == '\t') { if (!cur.empty()) { toks.push_back(cur); cur.clear(); } }
        else cur.push_back(c);
    }
    if (!cur.empty()) toks.push_back(cur);
    return toks;
}

static float numRow(const std::vector<std::string>& row, int idx) {
    if (idx >= (int)row.size()) return 0.0f;
    const std::string& v = row[idx];
    if (v.empty() || v == "---" || v == "//" || v == "__") return 0.0f;
    if (v.find_first_not_of("0123456789.-") != std::string::npos) return 0.0f;
    float f = (float)atof(v.c_str());
    if (f <= 0.0f) return 0.0f;   // descartar invalidos no positivos
    return f;
}

static double rowTime(const std::vector<std::string>& row) {
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
}

bool GiroAdapter::parseGetbest(const std::string& text, StationSample& out) {
    // Estacion invalida esta ronda (p.ej. ZZ999): HTTP 200 con
    // "# STATUS: ERROR (...)" -> silencio, no error global (7.1).
    if (text.find("STATUS: ERROR") != std::string::npos) return false;

    // Filas de datos: mismo nucleo que el parser de scaled.php (tokenizer,
    // rowTime, numRow). Las lineas "# STATUS: WARNING (...)" son comentarios:
    // el servidor ignora h'xxx y eso NO invalida el cuerpo.
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
        std::vector<std::string> toks = tokenizeLine(l);
        // Fila multichar: 2 + 2*10 tokens (con el M(D) auto del servidor = 24).
        // Una respuesta single-char (4 tokens/fila) se rechaza ENTERA (7.1).
        if (toks.size() >= GB_MIN_TOKENS) rows.push_back(std::move(toks));
    }
    if (rows.empty()) return false;

    // Historia (7.3): todas las filas, cap 72 mas recientes (6 h @ 5 min; la
    // respuesta trae 24 h de sobra). P1: la historia lleva foF1/fxI para que
    // el replay no mezcle foF1 vivo con foF2 historico.
    {
        std::vector<GiroHistoryPoint> hist;
        size_t hstart = rows.size() > 72 ? rows.size() - 72 : 0;
        for (size_t k = hstart; k < rows.size(); k++) {
            const auto& rw = rows[k];
            GiroHistoryPoint p;
            p.timeUtc = rowTime(rw);
            p.foF2 = numRow(rw, GB_FOF2);
            p.foF1 = numRow(rw, GB_FOF1);
            p.muf  = numRow(rw, GB_MUF);
            p.hmF2 = numRow(rw, GB_HMF2);
            p.foE  = numRow(rw, GB_FOE);
            p.foEs = numRow(rw, GB_FOES);
            p.fxI  = numRow(rw, GB_FXI);
            p.B0   = numRow(rw, GB_B0);
            p.B1   = numRow(rw, GB_B1);
            p.fmin = numRow(rw, GB_FMIN);
            hist.push_back(p);
        }
        out.history = std::move(hist);
    }

    const auto& r = rows.back();   // la fila mas reciente
    auto num = [&](int idx) -> float { return numRow(r, idx); };

    float foF2 = num(GB_FOF2);
    float muf  = num(GB_MUF);
    float hmF2 = num(GB_HMF2);

    if (foF2 <= 0.0f && muf <= 0.0f && hmF2 <= 0.0f) return false;

    out.timeUtc = out.history.empty() ? 0.0 : out.history.back().timeUtc;
    out.foF2 = foF2;
    out.muf  = muf;
    out.hmF2 = hmF2;
    out.cs   = (int)num(GB_CS);
    // Columnas del CSV (0 si ausentes esa hora, p.ej. foF1/foE de noche).
    out.foF1 = num(GB_FOF1);
    out.foE  = num(GB_FOE);
    out.foEs = num(GB_FOES);
    out.fxI  = num(GB_FXI);
    out.B0   = num(GB_B0);
    out.B1   = num(GB_B1);
    out.fmin = num(GB_FMIN);
    // h'F2/h'F/h'E/h'Es: getbest los ignora (WARNING) y la tabla h' del panel
    // lee EbroAdapter directo -> quedan 0 para GIRO (P2, aceptado).
    // NmF2 [m^-3] = 1.24e10 * (foF2[MHz])^2  (foF2=10 MHz -> 1.24e12).
    out.nmF2 = (foF2 > 0.0f) ? (float)(1.24e10 * (double)foF2 * (double)foF2) : 0.0f;
    out.valid = true;
    return true;
}

// M4-tuning (T1, sellado): decision pura de cortesia de lanzamiento (ver
// .h). -1 = aun no vencida (skip/wait); 12 = nunca intentada (arranque
// escalonado: la rafaga fria sale a ~5 GETs/min); 0.25 = vencida (cortesia
// 7.2). El borde de vencimiento preserva la formula historica de pollRound:
// vencida <=> nowUtc - lastAttemptUtc >= intervalSec.
double GiroAdapter::launchGapSec(double lastAttemptUtc, double nowUtc,
                                 double intervalSec) {
    if (lastAttemptUtc <= 0.0) return 12.0;   // nunca intentada: fria
    if (nowUtc - lastAttemptUtc < intervalSec) return -1.0;   // esperar
    return 0.25;   // vencida: cortesia 7.2 (250 ms)
}

// M4 (7.2): una ronda de polling getbest. Cola: SOLO prioritarias en orden
// fijo (EB040 primera, aceptacion 4), filtradas por cache 10 min / backoff
// por estacion (via launchGapSec). Cortesia: concurrencia max 6 y gap
// global entre lanzamientos de GET: 12 s para estaciones nunca intentadas
// (M4-tuning T1, arranque escalonado) y 250 ms en regimen; T2: el reloj
// de cache/backoff de cada estacion arranca en el instante real de su
// intento, no en el de la ronda. Devuelve el numero de estaciones
// actualizadas.
int GiroAdapter::pollRound(double nowUtc) {
    std::vector<StationSample> todo;
    std::vector<double> gapSec;   // M4-tuning T1: 12 s fria / 0.25 vencida
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (const char* code : PRIORITY_STATIONS) {
            for (const auto& s : samples_) {
                if (s.code != code) continue;
                auto it = fetchState_.find(code);
                double interval = (it != fetchState_.end()) ? it->second.intervalSec : 600.0;
                double lastTry  = (it != fetchState_.end()) ? it->second.lastAttemptUtc : 0.0;
                double gap = launchGapSec(lastTry, nowUtc, interval);
                if (gap >= 0.0) { todo.push_back(s); gapSec.push_back(gap); }
                break;   // solo la primera coincidencia del catalogo
            }
        }
    }
    if (todo.empty()) return 0;

    // M4R-A: telemetria en vivo de la ronda (reset por ronda; el worker
    // la actualiza intento a intento — getNetStatus la publica para el
    // failover kc2g: >=3 intentos con 0 aciertos = lgdc fallando).
    roundAttempts_.store(0);
    roundOk_.store(0);

    const int total = (int)todo.size();
    std::atomic<int> next{0};
    std::atomic<int> ok{0};
    std::atomic<long long> lastLaunchMs{0};   // >= gapMs entre GET (global, T1)
    auto msNow = []() -> long long {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();
    };
    auto worker = [&]() {
        for (;;) {
            int i = next.fetch_add(1);
            if (i >= total) break;
            // M4-tuning T1: ventana por estacion — 12 s si nunca se intento
            // (la rafaga fria sale espaciada a ~5 GETs/min), 250 ms en
            // regimen. Autolimitante: tras la ronda fria todas tienen
            // timestamp y el gap vuelve a 250 ms solo.
            const long long gapMs = (long long)(gapSec[(size_t)i] * 1000.0);
            for (;;) {   // esperar ventana de lanzamiento (cortesia, T1)
                long long now = msNow();
                long long last = lastLaunchMs.load();
                if (now - last >= gapMs && lastLaunchMs.compare_exchange_weak(last, now)) {
                    // M-IRTAM-F2 (P3, C1): instrumenta el lanzamiento getbest
                    // en el gate lgdc. El pacing M4 no cambia (mismo CAS).
                    // Ciclo traza (solo-log): mismo `now` para gate y traza.
                    lgdcpacing::recordLaunch(now);
                    lgdctrace::trace("launch",
                                     "getbest " + todo[(size_t)i].code);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            StationSample s = todo[(size_t)i];
            const double attemptUtc = (double)std::time(nullptr);   // T2
            std::string resp;
            // Fallo (HTTP/timeout/429 o STATUS: ERROR) -> backoff de ESA
            // estacion; su ultimo dato bueno se conserva (aceptacion 5).
            bool good = getbestGet(s.code, resp) && parseGetbest(resp, s);
            roundAttempts_.fetch_add(1);   // M4R-A: intento completado (vivo)
            std::lock_guard<std::mutex> lk(mutex_);
            FetchState& st = fetchState_[s.code];
            st.lastAttemptUtc = attemptUtc;   // T2: instante real del intento
            if (good) {
                st.intervalSec = 600.0;   // reset al exito (cache 10 min)
                st.degraded = false;
                for (auto& e : samples_) if (e.code == s.code) e = s;
                ok++;
                roundOk_.fetch_add(1);   // M4R-A
            } else {
                st.intervalSec = std::min(st.intervalSec * 2.0, 1800.0);   // x2, cap 30 min
                st.degraded = true;
            }
            int pct = (int)(100.0f * next.load() / total);  // Release-prep: clamp (overshoot del pool)
            progress_ = pct > 100 ? 100 : (pct < 0 ? 0 : pct);
        }
    };
    unsigned nth = std::thread::hardware_concurrency();
    if (nth == 0) nth = 4;
    if (nth > 6) nth = 6;   // concurrencia maxima 6 (7.2)
    std::vector<std::thread> pool;
    pool.reserve(nth);
    for (unsigned t = 0; t < nth; t++) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    return ok.load();
}

bool GiroAdapter::fetchStationsSync(double utc) {
    if (copySamples().empty() && !fetchCatalog()) return false;
    double now = (utc > 0) ? utc : (double)std::time(nullptr);
    int ok = pollRound(now);
    lastFetchUtc_ = now;
    printf("[GiroAdapter] Sync getbest: %d stations updated\n", ok);
    return ok > 0;
}

void GiroAdapter::fetchStationsAsync() {
    if (fetching_.exchange(true)) return;   // ya en curso
    std::thread([this]() {
        if (copySamples().empty() && !fetchCatalog()) { fetching_.store(false); return; }
        double now = (double)std::time(nullptr);
        int ok = pollRound(now);
        lastFetchUtc_ = now;
        printf("[GiroAdapter] getbest async: %d stations updated\n", ok);
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

GiroAdapter::GiroNetStatus GiroAdapter::getNetStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    GiroNetStatus st;
    st.roundAttempts = roundAttempts_.load();   // M4R-A: ronda en curso/ultima
    st.roundOk = roundOk_.load();
    double now = (double)std::time(nullptr);
    for (const auto& s : samples_) {
        if (prioritySet().count(s.code) == 0) continue;   // catalogo: sin polling
        st.polled++;
        if (s.valid) st.withData++;
        if (!s.history.empty()) {
            double span = now - s.history.front().timeUtc;   // alcance hacia atras
            if (span > 0.0 && (st.histHours <= 0.0 || span < st.histHours))
                st.histHours = span / 3600.0;   // la peor estacion limita el replay
        }
    }
    for (const auto& kv : fetchState_) {
        if (!kv.second.degraded) continue;
        st.degraded++;
        double remainMin = (kv.second.intervalSec - (now - kv.second.lastAttemptUtc)) / 60.0;
        if (remainMin < 0.0) remainMin = 0.0;
        if (st.degraded == 1 || remainMin < st.retryInMin) st.retryInMin = remainMin;
    }
    return st;
}

bool GiroAdapter::isPolled(const std::string& code) const {
    return prioritySet().count(code) > 0;   // set estatico: sin lock
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
        c.foF1 = p.foF1; c.fxI = p.fxI;   // M4/P1: replay sin mezclar
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
                p.foF1 = lerp(h[i].foF1, h[i + 1].foF1, f);   // M4/P1
                p.fxI  = lerp(h[i].fxI,  h[i + 1].fxI,  f);   // M4/P1
                fromHist(c, p);
            }
        }
        out.push_back(c);
    }
    return out;
}
