#include "App.h"

#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cmath>
#include <ctime>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <memory>
#include <chrono>

#include "Render/Shader.h"
#include "Render/Camera.h"
#include "Render/Framebuffer.h"
#include "Globe/Earth.h"
#include "Globe/Stars.h"
#include "Globe/Atmosphere.h"
#include "Globe/Terminator.h"
#include "Ionosphere/GridLayer.h"
#include "Ionosphere/IonosondeLayer.h"
#include "Ionosphere/SolarWindLayer.h"
#include "Data/DataManager.h"
#include "Data/EsaAdapter.h"
#include "Data/GloTecAdapter.h"
#include "Data/GiroAdapter.h"
#include "Data/TecCache.h"  // M10: cache TEC en disco
#include "Data/SdoImageAdapter.h"  // M11: disco solar
#include "UI/SunPanel.h"  // M11: ventana Sun
#include "Data/Kc2gAdapter.h"   // M4R-A: failover kc2g (FASE A sellada)
#include "Data/SolarIndicesAdapter.h"
#include "Data/SolarWindAdapter.h"
#include "Data/RadioPropagationAdapter.h"
#include "Data/AuroraAdapter.h"
#include "Data/EbroAdapter.h"
#include "Data/XrayAdapter.h"
#include "Data/DiasAdapter.h"
#include "Data/IrtamCoeffAdapter.h"  // M-IRTAM-F2 (P4): backfill gambit + bundle
#include "Data/IrtamCoeffCache.h"  // M-IRTAM-F2 (P1b): flip aditivo (buckets)
#include "Data/IrtamCoeffParse.h"  // M-IRTAM-F2 (P1b): flip aditivo (parse)
#include "Data/IrtamGridEval.h"    // M-IRTAM-F2 (P1b): eval Jones-Gallet 46x45
#include "Data/IrtamState.h"       // M-IRTAM-F2 (P5): estados honestos + replay
#include "Data/ProviderStatus.h"  // M-prov-matrix (048): matriz de estado (puro)
#include "Ionosphere/FaradayLayer.h"
#include "Ionosphere/RefShells.h"
#include "Ionosphere/VolumeRenderer.h"
#include "Ionosphere/SliceLayer.h"
#include "Ionosphere/HFTraceLayer.h"    // M8 (plan 11): capa HF multi-salto
#include "Ionosphere/LayerProfile.h"
#include "Ionosphere/DensityVolume.h"
#include "Utils/SolarPosition.h"
#include "Utils/CoordinateSystem.h"

struct App::Impl {
    // --- Bundles publicados por el hilo worker (inmutables, refcount) ---
    struct TecBundle { GridData grid; bool simulated = false; bool fromCache = false; };  // M10: badge cache
    struct IndicesBundle {
        IndexSnapshot snap;
        std::vector<IndexHistoryPoint> kp, dst, f107, ssn;
        std::vector<sundisk::Sunspot> spots;  // M11: manchas (vienen con sidx.fetch)
    };
    struct WindBundle {
        SolarWindSample latest;
        std::vector<SolarWindHistoryPoint> bzHist;
        bool simulated = true;
        std::string error;
    };
    struct AuroraBundle { GridData grid; std::string error; };
    struct RadioBundle {
        RadioPropSample latest;
        std::vector<RadioPropHistoryPoint> aeHist, s4Hist;
        bool simulated = true;
        bool usedGiro = false;
        std::string error;
    };
    struct XrayBundle { XraySample latest; };
    struct SdoBundle { SdoImage img; };  // M11: frame SDO publicado
    // M-IRTAM-F2 (P4, F2-R2): el bundle VIENE del adapter (estructura final +
    // valid). P4 publica valid=false siempre; P1b hara flip aditivo.
    mutable std::mutex bundleMutex;
    std::shared_ptr<const TecBundle> tec;
    std::shared_ptr<const IndicesBundle> indices;
    std::shared_ptr<const WindBundle> wind;
    std::shared_ptr<const AuroraBundle> auroraB;
    std::shared_ptr<const RadioBundle> radio;
    std::shared_ptr<const XrayBundle> xray;
    std::shared_ptr<const SdoBundle> sdoB;  // M11
    std::shared_ptr<const irtamfetch::IrtamBundle> irtam;  // M-IRTAM-F2 (P4)
    // M-prov-matrix (048): snapshots de estado por proveedor (solo lectura
    // del estado ya computado; se escriben junto a cada publicacion de
    // bundle y se leen en el gather del panel). provCopy = copia para
    // construir filas fuera del lock.
    provstatus::Snapshot provBoard[provstatus::PROV_COUNT];
    provstatus::Snapshot provCopy[provstatus::PROV_COUNT];
    std::shared_ptr<const TecBundle> getTec() const { std::lock_guard<std::mutex> lk(bundleMutex); return tec; }
    std::shared_ptr<const IndicesBundle> getIndices() const { std::lock_guard<std::mutex> lk(bundleMutex); return indices; }
    std::shared_ptr<const WindBundle> getWind() const { std::lock_guard<std::mutex> lk(bundleMutex); return wind; }
    std::shared_ptr<const AuroraBundle> getAurora() const { std::lock_guard<std::mutex> lk(bundleMutex); return auroraB; }
    std::shared_ptr<const RadioBundle> getRadio() const { std::lock_guard<std::mutex> lk(bundleMutex); return radio; }
    std::shared_ptr<const XrayBundle> getXray() const { std::lock_guard<std::mutex> lk(bundleMutex); return xray; }
    std::shared_ptr<const SdoBundle> getSdo() const { std::lock_guard<std::mutex> lk(bundleMutex); return sdoB; }  // M11
    std::shared_ptr<const irtamfetch::IrtamBundle> getIrtam() const { std::lock_guard<std::mutex> lk(bundleMutex); return irtam; }  // M-IRTAM-F2 (P4)
    // Hilo worker de red + progreso de carga inicial.
    std::thread fetchThread;
    // M0.2: las precargas en background pasan de detach() a miembros joinables
    // (cerrar la ventana durante una precarga podia usar impl tras liberarlo).
    std::thread histPreloadThread;
    std::thread catalogPreloadThread;
    std::thread irtamBackfillThread;  // M-IRTAM-F2 (P4): backfill gambit (joinable, M0.2)
    std::atomic<bool> shutdownRequested{false};
    std::atomic<int> loadTotal{6}, loadDone{0};
    std::atomic<bool> initialLoadDone{false};

    Camera camera;
    Shader earthShader;
    Shader starShader;
    Shader atmosphereShader;
    Shader bloomShader;
    Shader compositeShader;
    Shader terminatorShader;
    Earth earth;
    Stars stars;
    Atmosphere atmosphere;
    Terminator terminator;
    GridLayer tecLayer;
    GridLayer shellLayer;   // outer shell (isosuperficie anidada, imagen 1)
    IonosondeLayer ionosondeLayer;
    DataManager dataManager;
    GiroAdapter giro;
    DiasAdapter dias;   // complemento europeo (misma StationSample)
    // M4R-A (FASE A): failover-only de GIRO via prop.kc2g.com. lgdc
    // sigue primario escalonado; kc2g entra SOLO si lgdc falla.
    Kc2gAdapter kc2g;
    // Fusion catalogo-GIRO + kc2g del modo fallback (solo hilo de
    // render; se reconstruye al llegar batch nuevo — kc2gMergedStamp).
    std::vector<StationSample> kc2gMerged;
    double kc2gMergedStamp = -1.0;
    // Fuente ionosferica activa (badge UI GIRO-live/kc2g-fallback/model).
    enum IonSource { SRC_MODEL = 0, SRC_GIRO_LIVE = 1, SRC_KC2G_FALLBACK = 2 };
    std::atomic<int> ionSourceMode{SRC_MODEL};
    std::atomic<bool> catalogDone{false};   // catalogo GIRO intentado (exito o no)
    SolarWindLayer solarWindLayer;
    FaradayLayer faradayLayer;
    RefShells refShells;
    bool refShellsVisible = false;
    GridLayer auroraLayer;
    EbroAdapter ebro;
    Framebuffer sceneFBO;
    Framebuffer bloomFBO[2];

    Shader tecShader;

    GLuint quadVAO = 0;
    GLuint quadVBO = 0;

    float time = 0.0f;
    bool bloomEnabled = true;
    float bloomThreshold = 0.72f;  // bright-pass threshold (scene luminance)
    float bloomIntensity = 1.0f;   // blur pass gain
    float bloomStrength = 0.55f;   // composite add amount
    bool toneMap = true;           // ACES en composite (bypass exacto si off)
    float exposure = 1.12f;        // M1: calibrado para ACES+sRGB (era 1.0 a mano)
    int msaaSamples = 4;           // M1: MSAA de escena 0/2/4 (default 4, persistible)
    bool cineMode = false;         // cinema: auto-rotation (key C)
    float cineSpeed = 0.05f;       // rad/s de auto-rotacion en modo cine
    bool hideUI = false;           // ocultar toda la UI (key H)
    bool cPrev = false, hPrev = false;
    float cineHintUntil = 0.0f;
    char hintMsg[64] = "";
    // Espejos persistentes de UI (antes static locales / locales de panel).
    float altKm = 300.0f;
    int giroMode = 0;
    float giroPointSize = 11.0f;
    float aurOpacity = 0.85f;
    bool tecVisible = true;
    bool giroStationsVisible = true;
    bool auroraVisible = true;
    bool showEarth = true, showAtmosphere = true, showStars = true;
    bool terminatorVisible = true;
    float nightLevel = 0.45f;   // night-side brightness (earth.frag u_night)
    float cloudAmt = 0.55f;     // 0 = clear, 1 = overcast (earth.frag u_cloudAmt)
    // M6 (plan 9.4): 0 = High (raymarch 14x5), 1 = LUT (12 muestras + tabla
    // de transmittance horneada en Atmosphere::init); default High (v2, R2).
    int atmoQuality = 0;
    float atmoIntensity = 1.0f;   // slider 0.25-2 (atmosphere.frag u_intensity)
    float tecOpacity = 1.0f;
    // Variable del mapa de colores esferico (Seccion A del plan + Fase 5):
    // 0=TEC(GloTEC), 1=foF2, 2=MUF, 3=hmF2, 4=NmF2 (1-4 GIRO),
    // 5-7 derivados Radio, 8-9 IRTAM retrospectivo (M-IRTAM-F2 P4).
    // 5=LUF, 6=FOT, 7=Spread-F (5-7 derivados GIRO via RadioPropagationAdapter).
    int mapVariable = 0;
    // Colormap manual (override): si colormapOverride es true, el colormap elegido
    // en el desplegable "Colormap" (manualColormap 0..3) gana sobre el automatico
    // de la variable. Si es false, cada variable fija su colormap por defecto.
    bool colormapOverride = false;
    int manualColormap = 0;   // 0=viridis,1=inferno,2=plasma,3=turbo

    // Relieve 3D estilo volumetric (imagen 1): la cáscara se deforma con el valor.
    bool reliefOn = true;
    float reliefAmp = 0.025f;  // amplitud base (radio planetario = 1.0)
    bool stormSwell = true;    // amp efectiva x (1 + Kp/6): respira con el evento
    bool outerShell = true;    // segunda isosuperficie anidada
    float shellOpacity = 0.45f;
    int shellColormap = 1;     // 0=viridis,1=inferno,2=plasma,3=turbo (como Variable)

    // Viento solar (Fase 4): estado de la capa de flechas 3D.
    bool swArrowsVisible = false;
    int swArrowMode = 0;       // 0=color por velocidad, 1=color por Bz
    float swArrowScale = 1.0f;

    // Propagacion HF + Faraday (Fase 5).
    bool faradayVisible = false;
    int faradayMode = 0;       // 0=longitud por valor, 1=longitud fija + color
    float faradayScale = 1.0f;

    // M8 (plan 11): capa HF multi-salto + panel «Circuit».
    HFTraceLayer hfTrace;
    bool hfTraceVisible = false;   // toggle capa, off por defecto (plan 11.4, persist)
    float hfFreqMHz = 14.0f;       // slider 1-30 MHz (persist; TX/RX no - sello P6)
    std::string hfTxCode;          // "" = sigue al pin; si tampoco, DB049 (default demo)
    std::string hfRxCode;          // "" = EG931 (default demo)
    bool hfPickRx = false;         // «Pick RX...» activo (consume el click, no toca el pin)
    bool hfCircuitDirty = true;    // freq/TX/RX cambiados -> rebuild al proximo update
    bool hfPanelOpen = false;      // panel «Circuit» desplegado (alimenta el update)
    long long hfGridStamp = 0;     // identidad de los grids cacheados del circuito
    GridData hfFoF2Grid, hfHmF2Grid, hfLufGrid;   // caches (giroInterpGrid x2 + LUF M7)

    // Estacion GIRO bajo el cursor (hover) y fijada con click (pin, por codigo
    // URSI para que sobreviva al refresco incremental de muestras).
    int hoveredStation = -1;
    std::string pinnedCode;
    // Hover-solo en leyenda: que entrada aislar (None = todo normal).
    // Se calcula en ImGui y se aplica en el render 3D del frame siguiente.
    enum class SoloTarget { None, Main, Peak, Model, Volume };
    SoloTarget legendSolo = SoloTarget::None;
    bool mouseLeftPrev = false;
    double mouseDownX = 0.0, mouseDownY = 0.0;

    // Replay temporal de TEC: anillo de grids reales de la sesion (cada fetch
    // de 5 min). Live = ultimo grid; Replay = interpolacion entre frames.
    std::vector<GridData> tecHist;
    bool replayMode = false;
    double replayPos = 0.0;      // posicion en frames (fraccionaria)
    bool replayPlaying = false;
    float replaySpeed = 2.0f;    // frames por segundo
    int preReplayVar = 0;
    // M-irtam-replay-A (ruling R1): cursor primario = epoch continuo sobre
    // la ventana de union (96 h IRTAM + 72 h TEC). replayPos se deriva del
    // epoch para el lerp TEC interno (inversion mecanica).
    double replayEpoch = -1.0;   // epoch continuo UTC (s)
    // M-irtam-replay-031 (ruling toggle): bucle por capa (false = union
    // 96 h, defecto R2-023). NO se persiste en settings (lección partición).
    bool replayLayerLoop = false;
    // Muestra IRTAM = BRACKET entregado por el worker (M-irtam-replay-028):
    // endpoints para lerp en render por cursor; pairKind 0 = F/H, 1 = B0B1.
    struct IrtamReplaySample {
        long long epochSlot = -1;
        int pairKind = -1;
        GridData gA0, gA1, gB0, gB1;
        double t0 = 0.0, t1 = 0.0;
        bool have = false;
        bool clamped = false;
    };
    IrtamReplaySample irtamSample_;
    // M-irtam-replay-024 (ruling R2 PERF): worker asincrono dueño del
    // muestreo; el render solo consume la ultima muestra lista (handoff
    // corto). Peticion idempotente {par, slot, epoch}; en miss se sigue
    // pintando la muestra anterior (stale autodeclarado por el badge).
    std::thread irtamSampleThread;  // joinable, M0.2 (join en shutdown)
    std::mutex irtamSampleMutex;    // handoff corto muestra+peticion
    struct IrtamSampleRequest {
        int pairKind = -1;
        long long epochSlot = -1;
        double epoch = 0.0;
        bool pending = false;
    };
    IrtamSampleRequest irtamSampleReq_;
    std::atomic<bool> irtamSampleBusy{false};
    // M-irtam-replay-028: memos App-level SOLO-worker (el render no los toca;
    // sin locks). IrtamCoeffCache INTOCABLE: el memo invalida por mtime y ante
    // cualquier duda corre loadCached completo (heal/prune preservados).
    struct IrtamScanMemo {
        std::filesystem::file_time_type dirMtime{};
        bool valid = false;
        struct Entry {
            std::string param;
            double tovEpoch = 0.0;
            std::string path;
        };
        std::vector<Entry> entries;
    };
    IrtamScanMemo irtamScanMemo_;
    // Rejillas por TOV (param,tov) para endpoints compartidos entre brackets
    // consecutivos (FIFO cap 16 ~ 132 KB). Solo-worker.
    struct IrtamTovGridMemo {
        std::map<std::pair<std::string, double>, std::vector<float>> grids;
        std::vector<std::pair<std::string, double>> order;
        static constexpr std::size_t kCap = 16;
        const std::vector<float>* find(const std::string& p, double tov) const {
            auto it = grids.find({p, tov});
            return it == grids.end() ? nullptr : &it->second;
        }
        void store(const std::string& p, double tov, std::vector<float>&& v) {
            const auto k = std::make_pair(p, tov);
            if (grids.find(k) != grids.end()) return;
            if (order.size() >= kCap) {
                grids.erase(order.front());
                order.erase(order.begin());
            }
            grids[k] = std::move(v);
            order.push_back(k);
        }
    };
    IrtamTovGridMemo irtamTovMemo_;
    // Cache de muestras GIRO interpoladas al tiempo de replay (se reconstruye
    // cuando replayPos avanza; en live se usan las muestras directas).
    std::vector<StationSample> giroReplayCache;
    // Ultimos bundles consumidos (hilo principal; evita re-aplicar).
    std::shared_ptr<const TecBundle> lastTec;
    std::shared_ptr<const IndicesBundle> lastIndices;
    std::shared_ptr<const WindBundle> lastWind;
    std::shared_ptr<const AuroraBundle> lastAurora;
    std::shared_ptr<const RadioBundle> lastRadio;
    std::shared_ptr<const XrayBundle> lastXray;
    std::shared_ptr<const SdoBundle> lastSdo;  // M11
    // M11: Sun (disco SDO + ARs).
    int sunProduct = 0;             // 0=HMIIF 1=A304 (click alterna)
    bool sunVisible = true;         // toggle Layers + gate de trafico
    GLuint sunTex[2] = {0, 0};      // D1: una por producto (toggle = 1 frame)
    int sunTexW[2] = {0, 0}, sunTexH[2] = {0, 0};
    double sunUpTime[2] = {-1.0, -1.0};  // ultimo subido por producto (hilo main)
    std::atomic<bool> sdoKick{false};  // M11: click en Sun despierta al worker ya
    double sunImgTime[2] = {-1.0, -1.0};  // epoch mostrada por producto
    SunPanel sunPanel;              // M11: ventana (UI/SunPanel.h)
    SunGeom sunGeom;                // persistente (settings sunWin*)
    // Volumen + shells de referencia.
    VolumeRenderer volumeLayer;
    bool volumeVisible = false;
    float volumeOpacity = 1.0f;
    int volumeMode = 0;          // 0 = densidad, 1 = capas fijas
    bool pipVisible = false;     // lupa del limbo (magnifier, sin re-render)
    bool labelsVisible = false;  // etiquetas 3D con lider
    float explodeFactor = 1.0f;  // vista explotada x1..x5
    DensityVolume lastVolume;   // ultimo volumen construido (para el slice)
    bool isoBands = false;      // realce de isosuperficies logNe en el volumen
    // Cascaras derivadas del volumen (Fase 2/3, plan volume-derived-shells).
    GridLayer peakShell;        // F2: geometria hm real + color logNe
    bool peakShellVisible = false;
    GridLayer tecColShell;      // TEC modelado (integral) a 300 km planos
    bool tecColVisible = false;
    // Slice QTH-DX: A = pineada, B = hover.
    SliceLayer sliceLayer;
    bool sliceOn = false;
    int sliceSrc = 0;   // 0 = pin+hover, 1 = manual A/B, 2 = punto fijo ±2°
    float sliceLatA = 40.0f, sliceLonA = -4.0f;    // manual A / centro punto
    float sliceLatB = 40.0f, sliceLonB = -100.0f;  // manual B
    std::string sliceA, sliceB;
    float slicePeakLog = 0.0f;  // log10(Ne) maximo del trayecto
    // Anillo protegido (el hilo de precarga escribe mientras el render lee).
    std::mutex tecHistMutex;
    std::atomic<int> histTotal{0}, histDone{0};
    std::atomic<bool> histLoading{false};
    // El worker la activa al publicar el primer TEC real (no mock).
    std::atomic<bool> liveTecArrived{false};
    // M10 (cache TEC en disco): settings + estado de restore.
    bool tecCacheEnabled = true;
    double tecCacheMaxAgeH = 72.0;   // >maxAge puebla replay, no current
    std::atomic<bool> cacheLoaded{false};     // init-read con frames reales
    std::atomic<double> cacheNewestEpoch{-1.0};
    std::atomic<int> cacheRestored{0};
    // Catalogo GIRO en background.
    std::atomic<bool> catalogReady{false};
    bool catalogApplied = false;

    glm::vec3 sunDirection = glm::vec3(1.0f, 0.3f, 0.5f);

    void updateSunDirection(double utc) {
        // OJO: aqui va tiempo Unix REAL (el bug historico pasaba glfwGetTime(),
        // segundos desde el arranque, y el terminador quedaba congelado en 1970);
        // M2: el epoch lo decide el CALLER (wall-clock en LIVE, replayEpoch en
        // replay) - el sol sigue al dato.
        sunDirection = SolarPosition::getDirection(utc);
    }
};

// Fusion GIRO+DIAS para puntos (solo live; en replay solo GIRO con historia).
// Regla: GIRO valido gana (primaria probada); DIAS rellena huecos con coords
// resueltas contra el catalogo GIRO (el pager no trae lat/lon).
static std::vector<StationSample> mergeDias(const std::vector<StationSample>& giro,
                                            const std::vector<StationSample>& dias) {
    std::vector<StationSample> out = giro;
    for (const auto& d : dias) {
        if (!d.valid) continue;
        const StationSample* cat = nullptr;
        bool covered = false;
        for (const auto& g : giro) {
            if (g.code != d.code) continue;
            cat = &g;
            if (g.valid) covered = true;
            break;
        }
        if (covered || !cat) continue;
        StationSample c = d;
        c.latDeg = cat->latDeg;
        c.lonDeg = cat->lonDeg;
        c.name = cat->name;
        if (c.latDeg == 0.0f && c.lonDeg == 0.0f) continue;  // sin posicion
        out.push_back(c);
    }
    return out;
}

// M4R-A (FASE A): fusion GIRO+kc2g (solo en modo fallback). El catalogo
// GIRO conserva posiciones/nombres; kc2g rellena o actualiza datos por
// codigo cuando su fila es mas fresca (o la entrada no tiene dato).
// DIAS se fusiona encima despues (mergeDias: mismo patron de relleno).
static std::vector<StationSample> mergeKc2g(const std::vector<StationSample>& giro,
                                            const std::vector<StationSample>& kc2g) {
    std::vector<StationSample> out = giro;
    for (const auto& k : kc2g) {
        if (!k.valid) continue;
        bool replaced = false;
        for (auto& e : out) {
            if (e.code != k.code) continue;
            if (!e.valid || k.timeUtc >= e.timeUtc) e = k;   // kc2g fresco gana
            replaced = true;
            break;
        }
        if (!replaced) out.push_back(k);   // estacion nueva para el catalogo
    }
    return out;
}

// Convierte muestras GIRO a puntos de render para la capa de ionosondas.
static std::vector<StationPoint> giroPointsFrom(const std::vector<StationSample>& src) {
    std::vector<StationPoint> out;
    for (const auto& s : src) {
        StationPoint p;
        p.latDeg = s.latDeg;
        p.lonDeg = s.lonDeg;
        p.foF2 = s.foF2;
        p.muf = s.muf;
        p.hmF2 = s.hmF2;
        p.nmF2 = s.nmF2;
        p.foF1 = s.foF1;
        p.foE = s.foE;
        p.foEs = s.foEs;
        p.fxI = s.fxI;
        p.hF2 = s.hF2;
        p.hF = s.hF;
        p.hE = s.hE;
        p.hEs = s.hEs;
        p.B0 = s.B0;
        p.B1 = s.B1;
        p.fmin = s.fmin;
        p.code = s.code;
        p.name = s.name;
        p.valid = s.valid;
        p.timeUtc = s.timeUtc;   // M4R-A: edad de la fila (marca en paneles)
        p.stale = s.stale;       // M4R-A: kc2g 6-24 h o EG931 (panel si, grids no)
        out.push_back(p);
    }
    return out;
}

// Valor de una magnitud GIRO por indice de variable (0=foF2,1=MUF,2=hmF2,3=NmF2).
static float giroValueOf(const StationSample& s, int var) {
    switch (var) {
        case 1: return s.muf;
        case 2: return s.hmF2;
        case 3: return s.nmF2;
        default: return s.foF2;
    }
}

// Interpola espacialmente las muestras GIRO (IDW) a una rejilla 72x72 con el
// mismo layout que GloTEC (compatible con GridLayer::updateFromGrid):
//   values[latIdx*72 + lonIdx], fila 0 = lat -90 (sur), col 0 = lon -180,
//   lat de -90..90 (paso 2.5), lon de -180..180 (paso 5).
// var: 0=foF2, 1=MUF, 2=hmF2, 3=NmF2.
static GridData giroInterpGrid(const std::vector<StationSample>& samples, int var, int w, int h) {
    const int W = w, H = h;
    GridData g;
    g.width = W;
    g.height = H;
    g.values.assign(W * H, 0.0f);
    g.valid = false;

    std::vector<StationSample> pts;
    // M4R-A: filas stale (kc2g 6-24 h / EG931-2024) fuera de los grids.
    for (const auto& s : samples) if (s.valid && !s.stale) pts.push_back(s);
    if (pts.empty()) return g;

    const float maxDist = 50.0f;   // radio de influencia en grados
    const float cellLat = 180.0f / H;   // 2.5
    const float cellLon = 360.0f / W;   // 5
    // Fondo = mediana global (en vez de 0): sin acantilados en oceanos.
    // Peso debil (~estacion a 16deg) para no aplanar los picos locales.
    const float wBg = 0.004f;
    std::vector<float> mvals;
    mvals.reserve(pts.size());
    for (const auto& p : pts) {
        float vv = giroValueOf(p, var);
        if (vv > 0.0f) mvals.push_back(vv);
    }
    float med = 0.0f;
    if (!mvals.empty()) {
        std::sort(mvals.begin(), mvals.end());
        med = mvals[mvals.size() / 2];
    }

    for (int latIdx = 0; latIdx < H; latIdx++) {
        float latG = -90.0f + cellLat * latIdx;
        for (int lonIdx = 0; lonIdx < W; lonIdx++) {
            float lonG = -180.0f + cellLon * lonIdx;
            double wSum = wBg, vSum = (double)wBg * med;
            for (const auto& p : pts) {
                float dlat = p.latDeg - latG;
                float dlon = p.lonDeg - lonG;
                if (dlon > 180.0f) dlon -= 360.0f;
                if (dlon < -180.0f) dlon += 360.0f;
                // distancia angular aproximada (grados)
                float d = sqrtf(dlat * dlat + dlon * dlon);
                if (d > maxDist) continue;
                float val = giroValueOf(p, var);
                if (val <= 0.0f) continue;
                float w = 1.0f / (d * d + 0.5f);   // IDW con suavizado
                wSum += w;
                vSum += (double)w * val;
            }
            g.values[latIdx * W + lonIdx] = (float)(vSum / wSum);
        }
    }
    g.valid = true;
    g.timestamp = 0.0;
    return g;
}

// Formatea un campo del perfil Ebro ("---" si NaN).
static std::string ebFmt(float v, int dec) {
    if (std::isnan(v)) return "---";
    char b[32];
    if (dec <= 0) snprintf(b, sizeof(b), "%.0f", v);
    else if (dec == 1) snprintf(b, sizeof(b), "%.1f", v);
    else snprintf(b, sizeof(b), "%.2f", v);
    return std::string(b);
}

// Igual para GIRO (ausente = 0).
static std::string giroFmt(float v, int dec) {
    if (!(v > 0.0f)) return "---";
    return ebFmt(v, dec);
}

// Marcador "(?)" con tooltip de ayuda. Usar tras el item al que documenta.
static void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Interpola LUF/FOT/Spread-F/MUF (Fase 5, via RadioPropagationAdapter) a una
// rejilla 72x72 con el mismo layout que GridLayer::updateFromGrid.
// var: 0=LUF, 1=FOT, 2=Spread-F, 3=MUF. kpProxy modula Spread-F; xrayFlux
// alimenta el termino SID del LUF (M7, plan 10 — Sauer-Wilkinson).
static GridData radioInterpGrid(const std::vector<StationSample>& samples, int var, int w, int h,
                                double utc, float kpProxy, float xrayFlux) {
    const int W = w, H = h;
    GridData g;
    g.width = W;
    g.height = H;
    g.values.assign(W * H, 0.0f);
    g.valid = false;

    std::vector<StationSample> pts;
    // M4R-A: filas stale (kc2g 6-24 h / EG931-2024) fuera de los grids.
    for (const auto& s : samples) if (s.valid && !s.stale) pts.push_back(s);
    if (pts.empty()) return g;

    const float maxDist = 50.0f;
    const float cellLat = 180.0f / H;
    const float cellLon = 360.0f / W;
    // Fondo = mediana global (sin acantilados en oceanos).
    const float wBg = 0.004f;
    std::vector<float> mvals;
    mvals.reserve(pts.size());
    for (const auto& p : pts) {
        float vv = RadioPropagationAdapter::stationValue(p, var, utc, kpProxy, xrayFlux);
        if (vv > 0.0f) mvals.push_back(vv);
    }
    float med = 0.0f;
    if (!mvals.empty()) {
        std::sort(mvals.begin(), mvals.end());
        med = mvals[mvals.size() / 2];
    }

    for (int latIdx = 0; latIdx < H; latIdx++) {
        float latG = -90.0f + cellLat * latIdx;
        for (int lonIdx = 0; lonIdx < W; lonIdx++) {
            float lonG = -180.0f + cellLon * lonIdx;
            double wSum = wBg, vSum = (double)wBg * med;
            for (const auto& p : pts) {
                float dlat = p.latDeg - latG;
                float dlon = p.lonDeg - lonG;
                if (dlon > 180.0f) dlon -= 360.0f;
                if (dlon < -180.0f) dlon += 360.0f;
                float d = sqrtf(dlat * dlat + dlon * dlon);
                if (d > maxDist) continue;
                float val = RadioPropagationAdapter::stationValue(p, var, utc, kpProxy, xrayFlux);
                if (val <= 0.0f) continue;
                float w = 1.0f / (d * d + 0.5f);
                wSum += w;
                vSum += (double)w * val;
            }
            g.values[latIdx * W + lonIdx] = (float)(vSum / wSum);
        }
    }
    g.valid = true;
    g.timestamp = utc;
    return g;
}

// Interpola perfiles de capa (IDW, patron/radio/peso como giroInterpGrid;
// M5: la distancia pasa a gran circulo — el grid 2D conserva la plana,
// fuera del 'Archivos' del plan 8, ver P6 del sello M5). Fondo = mediana
// por campo EXCEPTO foF2/hmF2, que mezclan el fondo climatologico
// modelFoF2/modelHmF2 por cercania (plan 8.2/8.3).
// sunDir: marco del motor, el MISMO vector que usan despues
// applyDayNight y buildDensityVolume (coherencia terminador-modelo).
static std::vector<LayerProfile> interpLayerProfiles(const std::vector<StationSample>& samples, int w, int h,
                                                     const glm::vec3& sunDir, double epoch, double f107) {
    std::vector<LayerProfile> out((size_t)w * h);
    std::vector<StationSample> pts;
    // M4R-A: filas stale (kc2g 6-24 h / EG931-2024) fuera del volumen.
    for (const auto& s : samples) if (s.valid && !s.stale) pts.push_back(s);
    if (pts.empty()) return out;

    const float maxDist = 50.0f;
    const float cellLat = 180.0f / h;
    const float cellLon = 360.0f / w;
    const float wBg = 0.004f;
    auto medOf = [&](float StationSample::*m) {
        std::vector<float> v;
        v.reserve(pts.size());
        for (const auto& p : pts) if (p.*m > 0.0f) v.push_back(p.*m);
        if (v.empty()) return 0.0f;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    float fE = medOf(&StationSample::foE);
    float fF1 = medOf(&StationSample::foF1);
    float fF2 = medOf(&StationSample::foF2);
    const float mNmE  = fE  > 0.0f ? 1.24e10f * fE * fE : 0.0f;
    const float mNmF1 = fF1 > 0.0f ? 1.24e10f * fF1 * fF1 : 0.0f;
    const float mNmF2 = fF2 > 0.0f ? 1.24e10f * fF2 * fF2 : 0.0f;
    const float hmedF2 = medOf(&StationSample::hmF2);
    const float mB0 = sanitizeB0(medOf(&StationSample::B0));
    const float mB1 = sanitizeB1(medOf(&StationSample::B1));
    const int doy = dayOfYear(epoch);   // termino estacional del modelo (8.2)

    for (int latIdx = 0; latIdx < h; latIdx++) {
        float latG = -90.0f + cellLat * latIdx;
        for (int lonIdx = 0; lonIdx < w; lonIdx++) {
            float lonG = -180.0f + cellLon * lonIdx;
            double wNmE = wBg, vNmE = (double)wBg * mNmE;
            double wNmF1 = wBg, vNmF1 = (double)wBg * mNmF1;
            double wNmF2 = wBg, vNmF2 = (double)wBg * mNmF2;
            double wHm = wBg, vHm = (double)wBg * hmedF2;
            double wB0 = wBg, vB0 = (double)wBg * mB0;
            double wB1 = wBg, vB1 = (double)wBg * mB1;
            // M5/8.1: distancia gran circulo (haversine, en grados) en
            // lugar de la plana (error grande en latitudes altas). Radio
            // y peso del IDW intactos. nearDeg (8.3) se mide contra
            // TODAS las estaciones validas, no solo las de radio.
            double nearDeg = 180.0;
            for (const auto& p : pts) {
                double d = havDeg(latG, lonG, (double)p.latDeg, (double)p.lonDeg);
                if (d < nearDeg) nearDeg = d;
                if (d > maxDist) continue;
                double w = 1.0 / (d * d + 0.5);
                if (p.foE > 0.0f)  { float v = 1.24e10f * p.foE * p.foE; wNmE += w; vNmE += w * v; }
                if (p.foF1 > 0.0f) { float v = 1.24e10f * p.foF1 * p.foF1; wNmF1 += w; vNmF1 += w * v; }
                if (p.foF2 > 0.0f) { float v = 1.24e10f * p.foF2 * p.foF2; wNmF2 += w; vNmF2 += w * v; }
                if (p.hmF2 > 0.0f) { wHm += w; vHm += w * p.hmF2; }
                if (p.B0 > 0.0f)   { wB0 += w; vB0 += w * p.B0; }
                if (p.B1 > 0.0f)   { wB1 += w; vB1 += w * p.B1; }
            }
            // M5/8.3: mezcla medicion<->modelo SOLO en foF2 y hmF2 (los
            // que gobiernan volumen y MUF). cosChi con la MISMA
            // expresion que applyDayNight / buildDensityVolume
            // (densityDir, marco del motor — P1 corregido por MUSE).
            double wG = smoothstep(55.0, 25.0, nearDeg);
            glm::vec3 nrm = densityDir(latG, lonG);
            double cosChi = (double)(nrm.x * sunDir.x + nrm.y * sunDir.y + nrm.z * sunDir.z);
            LayerProfile pr;
            pr.NmE = (float)(vNmE / wNmE);
            pr.hasE = pr.NmE > 0.0f;
            pr.NmF1 = (float)(vNmF1 / wNmF1);
            pr.hasF1 = pr.NmF1 > 0.0f;
            // P2 (sello): el IDW de F2 queda intacto (Nm, fondo mediana
            // wBg); la mezcla con el modelo es en MHz: sqrt -> blend ->
            // cuadrar. wG=1 deja la celda como hoy salvo ~1e-7 (V1).
            double nmF2Idw = vNmF2 / wNmF2;
            double hmF2Idw = vHm / wHm;
            double foF2Idw = (nmF2Idw > 0.0) ? std::sqrt(nmF2Idw / 1.24e10) : 0.0;
            double foF2Cell = wG * foF2Idw + (1.0 - wG) * modelFoF2(cosChi, (double)latG, doy, f107);
            pr.NmF2 = (float)(1.24e10 * foF2Cell * foF2Cell);
            double hmF2Cell = (hmF2Idw > 0.0)
                ? (wG * hmF2Idw + (1.0 - wG) * modelHmF2(cosChi, (double)latG))
                : modelHmF2(cosChi, (double)latG);
            pr.hmF2 = (float)hmF2Cell;
            pr.hasF2 = pr.NmF2 > 0.0f && pr.hmF2 > 0.0f;
            pr.hmF1 = pr.hmF2 > 0.0f ? std::max(150.0f, 0.8f * pr.hmF2) : 180.0f;
            pr.B0 = sanitizeB0((float)(vB0 / wB0));
            pr.B1 = sanitizeB1((float)(vB1 / wB1));
            out[(size_t)latIdx * w + lonIdx] = pr;
        }
    }
    return out;
}

// Mezcla lineal de dos grids TEC (mismo tamano) para el replay temporal.
static GridData blendGrids(const GridData& a, const GridData& b, double f) {
    if (!a.valid) return b;
    if (!b.valid) return a;
    if (a.width != b.width || a.height != b.height || a.values.size() != b.values.size()) return a;
    GridData g;
    g.width = a.width;
    g.height = a.height;
    g.values.resize(a.values.size());
    float fb = (float)glm::clamp(f, 0.0, 1.0);
    for (size_t i = 0; i < a.values.size(); i++)
        g.values[i] = a.values[i] * (1.0f - fb) + b.values[i] * fb;
    g.timestamp = a.timestamp * (1.0 - f) + b.timestamp * f;
    g.valid = true;
    return g;
}

// M-irtam-replay-028: entrega del BRACKET del par (endpoints para lerp en
// render por cursor — simetria TEC). Lee del cache (solo-lectura), no toca
// el publish. pA/pB en minusculas ("fof2"/"b0"...). Rellena el bracket
// (GridData 45x46 + timestamp + valid en cada endpoint) + t0/t1/clamped.
// Nunca lanza. Hilo: SOLO worker (ver sampleIrtamBracketWorker).
static bool evalTovGrid(double tov, const std::string& path, double ryear,
                        std::vector<float>& out) {
    irtamccache::Bucket bk;
    if (!irtamccache::loadBucket(path, bk)) return false;
    irtamcoeff::Message m;
    std::string err;
    if (!irtamcoeff::parseMessage(
            std::string(bk.bytes.begin(), bk.bytes.end()), m, err) ||
        !irtamcoeff::isStandardBasis(m.header))
        return false;
    const long long day = (long long)std::floor(tov / 86400.0);
    const float h = (float)((tov - (double)day * 86400.0) / 3600.0);
    return irtameval::evalGrid(m.coeffs, ryear, h, h, out);
}

// M-irtam-replay-028: cuerpo del worker (HILO SAMPLER, nunca render).
// Resuelve el bracket sobre el indice memoizado (mtime; ante cualquier duda
// full loadCached con heal/prune) y evalua endpoints con memo de TOV.
// Guarda el bracket bajo lock; a fallo conserva la muestra anterior.
bool App::sampleIrtamBracketWorker(int pairKind, double epoch,
                                   long long slot) {
    const char* pA = (pairKind == 0) ? "fof2" : "b0";
    const char* pB = (pairKind == 0) ? "hmf2" : "b1";
    auto& memo = impl->irtamScanMemo_;
    auto& tmemo = impl->irtamTovMemo_;
    // 1. Indice (param, tovEpoch, path): memo por mtime o full scan.
    std::error_code ec;
    const auto curMtime =
        std::filesystem::last_write_time("cache", ec);
    bool useMemo = false;
    if (!ec && memo.valid && curMtime == memo.dirMtime &&
        !memo.entries.empty()) {
        useMemo = true;
    }
    if (!useMemo) {
        const irtamccache::ScanResult sc = irtamccache::loadCached("cache");
        if (sc.buckets.empty()) return false;
        memo.entries.clear();
        for (const auto& bk : sc.buckets) {
            if (bk.tovEpoch <= 0.0) continue;
            memo.entries.push_back({bk.param, bk.tovEpoch, bk.path});
        }
        std::error_code ec2;
        memo.dirMtime = std::filesystem::last_write_time("cache", ec2);
        memo.valid = !ec2 && !memo.entries.empty();
        if (!memo.valid) return false;
    }
    std::vector<irtamstate::TovEntry> idx;
    idx.reserve(memo.entries.size());
    for (const auto& e : memo.entries) idx.push_back({e.param, e.tovEpoch});
    const irtamstate::PairBracket pb =
        irtamstate::pairBracketTovs(idx, pA, pB, epoch);
    if (pb.empty) return false;
    // 2. Endpoints (memo TOV o load+parse+eval). t0==t1: un solo juego.
    auto gridFor = [&](const std::string& p,
                       double tov) -> const std::vector<float>* {
        if (const auto* hit = tmemo.find(p, tov)) return hit;
        std::string path;
        for (const auto& e : memo.entries) {
            if (e.param == p && e.tovEpoch == tov) {
                path = e.path;
                break;
            }
        }
        if (path.empty()) return nullptr;
        std::vector<float> v;
        const double ryear = irtameval::decimalYearForTov(
            pb.clamped ? pb.t0 : epoch);
        if (!evalTovGrid(tov, path, ryear, v)) return nullptr;
        tmemo.store(p, tov, std::move(v));
        return tmemo.find(p, tov);
    };
    const std::vector<float>* a0 = gridFor(pA, pb.t0);
    const std::vector<float>* a1 = gridFor(pA, pb.t1);
    const std::vector<float>* b0 = gridFor(pB, pb.t0);
    const std::vector<float>* b1 = gridFor(pB, pb.t1);
    if (!a0 || !a1 || !b0 || !b1) return false;
    // 3. Handoff corto.
    std::lock_guard<std::mutex> lk(impl->irtamSampleMutex);
    auto& s = impl->irtamSample_;
    auto fill = [](GridData& g, const std::vector<float>& v, double tov) {
        g.width = irtameval::kGridW;
        g.height = irtameval::kGridH;
        g.values = v;
        g.timestamp = tov;
        g.valid = true;
    };
    fill(s.gA0, *a0, pb.t0);
    fill(s.gA1, *a1, pb.t1);
    fill(s.gB0, *b0, pb.t0);
    fill(s.gB1, *b1, pb.t1);
    s.t0 = pb.t0;
    s.t1 = pb.t1;
    s.pairKind = pairKind;
    s.epochSlot = slot;
    s.clamped = pb.clamped;
    s.have = true;
    return true;
}

// M-irtam-replay-028: consumo render-path (wait-free salvo handoff corto).
// Postea peticion idempotente si el slot no esta listo; copia el BRACKET
// (el render interpola por cursor cada frame — simetria TEC).
bool App::consumeIrtamBracket(int pairKind, IrtamBracketView& out) {
    if (!impl->replayMode || impl->replayEpoch <= 0.0) return false;
    const long long slot = (long long)std::floor(impl->replayEpoch / 900.0);
    std::lock_guard<std::mutex> lk(impl->irtamSampleMutex);
    auto& s = impl->irtamSample_;
    auto& q = impl->irtamSampleReq_;
    if (!(s.have && s.pairKind == pairKind && s.epochSlot == slot)) {
        if (!q.pending || q.pairKind != pairKind || q.epochSlot != slot) {
            q.pairKind = pairKind;
            q.epochSlot = slot;
            q.epoch = impl->replayEpoch;
            q.pending = true;
        }
    }
    if (s.have && s.pairKind == pairKind) {
        out.a0 = s.gA0.values;
        out.a1 = s.gA1.values;
        out.b0 = s.gB0.values;
        out.b1 = s.gB1.values;
        out.t0 = s.t0;
        out.t1 = s.t1;
        out.have = true;
        out.clamped = s.clamped;
        return true;
    }
    return false;
}

// M-irtam-replay-028: lerp en render por cursor sobre el bracket entregado
// (simetria TEC; microsegundos, wait-free). f clampado a [0,1]; a fallo
// devuelve grid invalido (ausente honesto).
static GridData lerpBracketGrid(const std::vector<float>& v0,
                                const std::vector<float>& v1, double t0,
                                double t1, double epoch) {
    GridData g;
    double f = (t1 > t0) ? (epoch - t0) / (t1 - t0) : 0.0;
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    if (!irtamstate::lerpValues(v0, v1, f, g.values)) return GridData{};
    g.width = irtameval::kGridW;
    g.height = irtameval::kGridH;
    g.timestamp = epoch;
    g.valid = true;
    return g;
}

// M0.1: resize diferido. El callback solo encola el tamaño pedido (puede
// dispararse dentro de glfwPollEvents en mitad de un frame); el bucle de
// frames recrea los FBOs con el contexto actual. Los callbacks corren en el
// hilo principal, pero los atómicos dejan la intención explícita.
static std::atomic<int> g_pendingResizeW{0};
static std::atomic<int> g_pendingResizeH{0};

static void framebufferSizeCallback(GLFWwindow* w, int width, int height) {
    glViewport(0, 0, width, height);
    if (width > 0 && height > 0) {
        g_pendingResizeW.store(width, std::memory_order_relaxed);
        g_pendingResizeH.store(height, std::memory_order_relaxed);
    }
}

static void mouseCallback(GLFWwindow* w, double xpos, double ypos) {
    // handled in processInput
}

App::App() : impl(new Impl) {}
App::~App() { delete impl; }

bool App::init(int width, int height, const char* title) {
    winW = width;
    winH = height;

    if (!glfwInit()) {
        fprintf(stderr, "GLFW init failed\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // (M0/R6) GLFW_SAMPLES eliminado: el backbuffer por defecto no importa
    // (composite = quad fullscreen; ImGui hace su propio AA). El MSAA real
    // vive desde M1 en el sceneFBO (textura multisample + resolve por blit);
    // el glEnable(GL_MULTISAMPLE) de abajo es su interruptor global.

    window = glfwCreateWindow(winW, winH, title, nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Window creation failed\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "GLAD init failed\n");
        return false;
    }

    printf("[OK] OpenGL %s\n", glGetString(GL_VERSION));
    printf("[OK] Renderer: %s\n", glGetString(GL_RENDERER));

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 400");

    // Camera
    impl->camera.position = glm::vec3(0.0f, 0.0f, 3.0f);
    impl->camera.target = glm::vec3(0.0f);
    impl->camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    impl->camera.fov = 45.0f;
    impl->camera.aspect = (float)winW / winH;
    impl->camera.nearP = 0.1f;
    impl->camera.farP = 100.0f;
    impl->camera.distance = 3.0f;
    impl->camera.rotateX = 0.0f;
    impl->camera.rotateY = 0.0f;

    // Shaders
    if (!impl->earthShader.load("shaders/earth.vert", "shaders/earth.frag")) {
        fprintf(stderr, "Earth shader failed, using fallback\n");
        impl->earthShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec3 aPos;\n"
            "layout(location=1) in vec3 aNormal;\n"
            "layout(location=2) in vec2 aTexCoord;\n"
            "uniform mat4 u_model, u_view, u_projection;\n"
            "out vec3 vNormal; out vec2 vTexCoord; out vec3 vFragPos;\n"
            "void main(){\n"
            "  vNormal = mat3(u_model) * aNormal;\n"
            "  vTexCoord = aTexCoord;\n"
            "  vFragPos = vec3(u_model * vec4(aPos, 1.0));\n"
            "  gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);\n"
            "}\n",
            "#version 400 core\n"
            "in vec3 vNormal; in vec2 vTexCoord; in vec3 vFragPos;\n"
            "uniform sampler2D u_texture;\n"
            "uniform vec3 u_sunDir;\n"
            "out vec4 fragColor;\n"
            "void main(){\n"
            "  vec3 n = normalize(vNormal);\n"
            "  float diff = max(dot(n, normalize(u_sunDir)), 0.15);\n"
            "  vec3 col = texture(u_texture, vTexCoord).rgb * diff;\n"
            "  fragColor = vec4(col, 1.0);\n"
            "}\n"
        );
    }

    if (!impl->starShader.load("shaders/star.vert", "shaders/star.frag")) {
        impl->starShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec3 aPos;\n"
            "layout(location=1) in float aMag;\n"
            "uniform mat4 u_view, u_projection;\n"
            "uniform float u_time;\n"
            "out float vAlpha;\n"
            "void main(){\n"
            "  gl_Position = u_projection * u_view * vec4(aPos * 20.0, 1.0);\n"
            "  vAlpha = 0.3 + 0.7 * (1.0 - aMag / 6.0);\n"
            "  vAlpha *= 0.8 + 0.2 * sin(u_time * 0.5 + aPos.x * 10.0);\n"
            "  gl_PointSize = max(1.0, (3.0 - aMag) * 1.5);\n"
            "}\n",
            "#version 400 core\n"
            "in float vAlpha;\n"
            "out vec4 fragColor;\n"
            "void main(){\n"
            "  vec2 c = gl_PointCoord * 2.0 - 1.0;\n"
            "  float d = dot(c, c);\n"
            "  float a = smoothstep(1.0, 0.0, d) * vAlpha;\n"
            "  fragColor = vec4(1.0, 1.0, 0.95, a);\n"
            "}\n"
        );
    }

    if (!impl->atmosphereShader.load("shaders/atmosphere.vert", "shaders/atmosphere.frag")) {
        // M6 (P7, patron M1): ESPEJO de shaders/atmosphere.{vert,frag}.
        // m6_edits.py genera estas cadenas DESDE los ficheros despues de
        // escribirlos — sync por construccion, no copia a mano.
        impl->atmosphereShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec3 aPos;\n"
            "\n"
            "uniform mat4 u_model, u_view, u_projection;\n"
            "\n"
            "// M6 (P1, plan 9.2): posicion en MUNDO post-escala. El frag necesita el\n"
            "// rd exacto: vPos = aPos (pre-escala) dejaba el viewDir torcido ~1% contra\n"
            "// u_cameraPos. Mismo patron que el vert del VolumeRenderer.\n"
            "out vec3 vWorldPos;\n"
            "\n"
            "void main() {\n"
            "    vWorldPos = vec3(u_model * vec4(aPos, 1.0));\n"
            "    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);\n"
            "}\n",
            "#version 400 core\n"
            "// M6 (plan 9.1): single scattering Rayleigh/Mie con sombra dura del\n"
            "// planeta. REEMPLAZO TOTAL de la cascara Fresnel pow(3) (dos colores\n"
            "// constantes por sunAlign, sin profundidad optica) — aceptacion 6: es\n"
            "// reemplazo, no adicion. El fallback loadFromSource de App.cpp es ESPEJO\n"
            "// de este fichero (patron M1): m6_edits.py genera ambas cadenas desde el\n"
            "// mismo contenido (sync por construccion, no copia a mano).\n"
            "// Unidades normalizadas a radio terrestre = 1.0 (constantes del plan 9.1,\n"
            "// sin convertir).\n"
            "in vec3 vWorldPos;   // P1: mundo POST-escala — rd exacto\n"
            "\n"
            "uniform vec3 u_sunDir;\n"
            "uniform vec3 u_cameraPos;\n"
            "uniform float u_intensity;   // slider 0.25-2 (plan 9.4, default 1.0)\n"
            "uniform int u_lutMode;       // 0 = High (raymarch 14x5), 1 = LUT (12 + tabla)\n"
            "uniform sampler2D u_lutTex;  // 256x64 GL_RG32F: (odR, odM) punto->sol (Atmosphere::init)\n"
            "\n"
            "out vec4 fragColor;\n"
            "\n"
            "// Constantes (UNIDADES R_tierra — no convertir). CRUZADAS con\n"
            "// Atmosphere.cpp (bake de la LUT) y earth.frag (aerial perspective):\n"
            "// mantener las tres copias sincronizadas.\n"
            "const float PLANET_R = 1.0;\n"
            "const float ATM_R    = 1.0255;               // tope ~162 km; = escala 1.0255f de App.cpp\n"
            "const float H_R      = 8.5e-3;               // escala Rayleigh (8.5 km)\n"
            "const float H_M      = 1.2e-2;               // escala Mie (12 km)\n"
            "const vec3  BETA_R   = vec3(5.8, 13.5, 33.1) * 0.0105;   // m^-1 -> R_tierra\n"
            "const float BETA_M   = 21.0 * 0.0105;\n"
            "const float G_MIE    = 0.76;\n"
            "\n"
            "const float PI = 3.14159265358979;\n"
            "\n"
            "// P1: AMBOS radicales del rayo-esfera (el plan 9.1 daba el near — t de\n"
            "// entrada; el far — t de salida de la atmosfera — es el que la marcha\n"
            "// necesita).\n"
            "float raySphereNear(vec3 ro, vec3 rd, float r) {\n"
            "    float b = dot(ro, rd);\n"
            "    float c = dot(ro, ro) - r * r;\n"
            "    float h = b * b - c;\n"
            "    if (h < 0.0) return -1.0;\n"
            "    return -b - sqrt(h);\n"
            "}\n"
            "float raySphereFar(vec3 ro, vec3 rd, float r) {\n"
            "    float b = dot(ro, rd);\n"
            "    float c = dot(ro, ro) - r * r;\n"
            "    float h = b * b - c;\n"
            "    if (h < 0.0) return -1.0;\n"
            "    return -b + sqrt(h);\n"
            "}\n"
            "\n"
            "// Extincion (plan 9.1): el 1.1 del Mie es calibracion de la demo. Misma\n"
            "// formula en ambos tramos (sol->muestra y muestra->camara) y tambien tras\n"
            "// el lookup de la LUT (que guarda od puros).\n"
            "vec3 extinct(float odR, float odM) {\n"
            "    return exp(-(BETA_R * odR + vec3(BETA_M * 1.1 * odM)));\n"
            "}\n"
            "\n"
            "// Fase Rayleigh (3/16pi)(1 + cos^2) y Mie Henyey-Greenstein (plan 9.1).\n"
            "float phaseR(float c) { return (3.0 / (16.0 * PI)) * (1.0 + c * c); }\n"
            "float phaseM(float c) {\n"
            "    float g2 = G_MIE * G_MIE;\n"
            "    return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * G_MIE * c, 1.5));\n"
            "}\n"
            "\n"
            "void main() {\n"
            "    vec3 sun = normalize(u_sunDir);\n"
            "    vec3 ro = u_cameraPos;\n"
            "    vec3 rd = normalize(vWorldPos - u_cameraPos);   // exacto (P1)\n"
            "\n"
            "    // La camara orbita SIEMPRE fuera de ATM_R (clamp [1.5, 20] R_tierra):\n"
            "    // tEnter > 0 para todo rayo que toca la cascara (hemisferio trasero,\n"
            "    // ver Atmosphere::render).\n"
            "    float tEnter = max(raySphereNear(ro, rd, ATM_R), 0.0);\n"
            "    float tExit  = raySphereFar(ro, rd, ATM_R);\n"
            "    if (tExit <= tEnter) { fragColor = vec4(0.0); return; }\n"
            "    // El disco del planeta lo mata el depth test (Tierra dibujada antes con\n"
            "    // depth write ON); el clip analitico es defensivo (plan 9.2).\n"
            "    float tPlanet = raySphereNear(ro, rd, PLANET_R);\n"
            "    if (tPlanet >= 0.0) tExit = min(tExit, tPlanet);\n"
            "    if (tExit <= tEnter) { fragColor = vec4(0.0); return; }\n"
            "\n"
            "    int steps = (u_lutMode == 1) ? 12 : 14;   // plan 9.4: LUT = 12, High = 14\n"
            "    float dt = (tExit - tEnter) / float(steps);\n"
            "\n"
            "    vec3 sumR = vec3(0.0);\n"
            "    vec3 sumM = vec3(0.0);\n"
            "    float odR = 0.0, odM = 0.0;   // profundidad optica camara->muestra\n"
            "\n"
            "    for (int i = 0; i < steps; i++) {\n"
            "        vec3 p = ro + rd * (tEnter + (float(i) + 0.5) * dt);\n"
            "        float h = max(length(p) - PLANET_R, 0.0);\n"
            "        float dR = exp(-h / H_R);\n"
            "        float dM = exp(-h / H_M);\n"
            "        odR += dR * dt;\n"
            "        odM += dM * dt;\n"
            "\n"
            "        // Sombra dura del planeta (el detalle que vende el atardecer): sin\n"
            "        // luz directa si el rayo muestra->sol toca el planeta.\n"
            "        if (raySphereNear(p, sun, PLANET_R) < 0.0) {\n"
            "            float sunOdR = 0.0, sunOdM = 0.0;\n"
            "            if (u_lutMode == 1) {\n"
            "                // LUT: mismos ejes que el bake de Atmosphere::init\n"
            "                // (u = cos del cenit solar, v = altura normalizada).\n"
            "                vec2 uv = vec2(dot(normalize(p), sun) * 0.5 + 0.5,\n"
            "                               h / (ATM_R - PLANET_R));\n"
            "                vec2 od = texture(u_lutTex, uv).rg;\n"
            "                sunOdR = od.r;\n"
            "                sunOdM = od.g;\n"
            "            } else {\n"
            "                // Marcha al sol: 5 muestras (plan 9.1) hasta salir de ATM_R.\n"
            "                float tSun = max(raySphereFar(p, sun, ATM_R), 0.0);\n"
            "                float ds = tSun / 5.0;\n"
            "                for (int j = 0; j < 5; j++) {\n"
            "                    vec3 q = p + sun * ((float(j) + 0.5) * ds);\n"
            "                    float hq = max(length(q) - PLANET_R, 0.0);\n"
            "                    sunOdR += exp(-hq / H_R) * ds;\n"
            "                    sunOdM += exp(-hq / H_M) * ds;\n"
            "                }\n"
            "            }\n"
            "            vec3 T = extinct(sunOdR, sunOdM) * extinct(odR, odM);\n"
            "            sumR += T * (dR * dt);\n"
            "            sumM += T * (dM * dt);\n"
            "        }\n"
            "    }\n"
            "\n"
            "    float mu = dot(rd, sun);   // coseno del angulo de dispersion\n"
            "    vec3 col = (sumR * BETA_R * phaseR(mu) + sumM * BETA_M * phaseM(mu))\n"
            "               * u_intensity * 20.0;   // factor 20 calibrado en la demo (plan 9.1)\n"
            "    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));\n"
            "    fragColor = vec4(col, lum);   // alpha = luminancia (plan 9.1)\n"
            "}\n"
        );
    }

    if (!impl->bloomShader.load("shaders/bloom.vert", "shaders/bloom.frag")) {
        // M1: fallback en paralelo con shaders/bloom.frag (u_pass + soft-knee).
        impl->bloomShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec2 aPos;\n"
            "out vec2 vUV;\n"
            "void main(){ vUV = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n",
            "#version 400 core\n"
            "in vec2 vUV;\n"
            "uniform sampler2D u_texture;\n"
            "uniform vec2 u_direction;\n"
            "uniform vec2 u_resolution;\n"
            "uniform float u_threshold;\n"
            "uniform float u_intensity;\n"
            "uniform int u_pass;\n"
            "out vec4 fragColor;\n"
            "vec3 brightPass(vec3 c){\n"
            "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
            "  return c * smoothstep(u_threshold, u_threshold + 0.25, l);\n"
            "}\n"
            "void main(){\n"
            "  float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
            "  vec3 result = brightPass(texture(u_texture, vUV).rgb) * weight[0];\n"
            "  vec2 texel = u_direction / u_resolution;\n"
            "  for(int i=1;i<5;i++){\n"
            "    result += brightPass(texture(u_texture, vUV + texel * float(i)).rgb) * weight[i];\n"
            "    result += brightPass(texture(u_texture, vUV - texel * float(i)).rgb) * weight[i];\n"
            "  }\n"
            "  vec2 nt1 = texel * 0.35;\n"
            "  vec2 nt2 = texel * 0.70;\n"
            "  result += brightPass(texture(u_texture, vUV + nt1).rgb) * 0.10;\n"
            "  result += brightPass(texture(u_texture, vUV - nt1).rgb) * 0.10;\n"
            "  result += brightPass(texture(u_texture, vUV + nt2).rgb) * 0.06;\n"
            "  result += brightPass(texture(u_texture, vUV - nt2).rgb) * 0.06;\n"
            "  if(u_pass == 1) result *= u_intensity;\n"
            "  fragColor = vec4(result, 1.0);\n"
            "}\n"
        );
    }

    if (!impl->compositeShader.load("shaders/composite.vert", "shaders/composite.frag")) {
        // M1: fallback en paralelo con shaders/composite.frag (ACES + sRGB).
        impl->compositeShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec2 aPos;\n"
            "out vec2 vUV;\n"
            "void main(){ vUV = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n",
            "#version 400 core\n"
            "in vec2 vUV;\n"
            "uniform sampler2D u_scene;\n"
            "uniform sampler2D u_bloom;\n"
            "uniform bool u_bloomEnabled;\n"
            "uniform float u_bloomStrength;\n"
            "uniform bool u_toneMap;\n"
            "uniform float u_exposure;\n"
            "out vec4 fragColor;\n"
            "vec3 acesFilm(vec3 x){\n"
            "  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);\n"
            "}\n"
            "void main(){\n"
            "  vec3 col = texture(u_scene, vUV).rgb;\n"
            "  if(u_bloomEnabled) col += texture(u_bloom, vUV).rgb * u_bloomStrength;\n"
            "  if(u_toneMap){\n"
            "    col = acesFilm(col * u_exposure);\n"
            "    col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));\n"
            "  }\n"
            "  fragColor = vec4(col, 1.0);\n"
            "}\n"
        );
    }

    // Globe
    impl->earth.init();
    impl->stars.init();
    impl->atmosphere.init();
    impl->terminator.init();
    // FIX aro blanco: el shader del terminador nunca se cargaba (programa 0 ->
    // el driver dibujaba la linea en fijo = blanca). Cargar siempre + fallback.
    if (!impl->terminatorShader.load("shaders/terminator.vert", "shaders/terminator.frag")) {
        fprintf(stderr, "Terminator shader failed, using fallback\n");
        impl->terminatorShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec3 aPos;\n"
            "uniform mat4 u_model, u_view, u_projection;\n"
            "void main(){ gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0); }\n",
            "#version 400 core\n"
            "out vec4 fragColor;\n"
            "void main(){ fragColor = vec4(0.80, 0.38, 0.12, 0.18); }\n"
        );
    }

    // Ionosphere (capas vacias: el hilo worker las rellena al llegar datos).
    impl->tecLayer.init();
    impl->dataManager.init();
    // Outer shell: misma rejilla, +0.025 de radio, inferno tenue (isosuperficie anidada).
    impl->shellLayer.init();
    impl->shellLayer.setColormap("inferno");
    impl->shellLayer.setAltitude(300.0f / 6371.0f + 0.025f);
    impl->shellLayer.setOpacity(0.45f);
    impl->shellLayer.setVisible(true);
    impl->tecLayer.setVisible(true);           // sincronizar el toggle con el checkbox inicial (App.cpp L524)
    impl->tecLayer.setAltitude(300.0f / 6371.0f);  // arrancar a 300 km (radio ~1.047), no pegado a 1.02
    applyRelief();
    // Precarga del historico GloTEC (indice recuperado): ventana 72 h en
    // background para que el replay tenga pasado real al instante.
    {
        App* self = this;
        // M0.2: hilo joinable (shutdown lo espera) + chequeos contra
        // shutdownRequested: cerrar durante la espera nunca bloquea >~1 s aqui.
        impl->histPreloadThread = std::thread([self]() {
            // Esperar al primer TEC vivo (max 180 s) para no competir con el
            // arranque; sin red se omite la precarga (el replay queda en sesion).
            for (int i = 0; i < 180 && !self->impl->liveTecArrived.load()
                             && !self->impl->shutdownRequested.load(); i++)
                std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!self->impl->liveTecArrived.load() || self->impl->shutdownRequested.load())
                return;
            self->impl->histLoading.store(true);
            GloTecAdapter loader;   // instancia propia: sin estado compartido
            loader.setGridSize(72, 72);
            std::vector<GloTecFrame> idx;
            if (loader.fetchIndex(idx)) {
                // TEC-ext 034: ventana [now-72 h, now] ausente en anillo/disco
                // (el take clasico solo pedia mas-nuevo-que-cache: en cache
                // calida no habria backfill de viejos).
                std::vector<double> cachedEp;
                {
                    std::lock_guard<std::mutex> tlk(self->impl->tecHistMutex);
                    for (const auto& f : self->impl->tecHist)
                        cachedEp.push_back(f.timestamp);
                }
                const int nCached = (int)cachedEp.size();
                std::vector<double> idxEp;
                idxEp.reserve(idx.size());
                for (const auto& e : idx) idxEp.push_back(e.timeUtc);
                const double nowPre =
                    (double)std::time(nullptr);
                const std::vector<std::size_t> want =
                    teccache::missingInWindow(cachedEp, idxEp, nowPre,
                                              (std::size_t)teccache::kCapFrames);
                self->impl->histTotal.store((int)want.size());
                for (std::size_t k : want) {
                    if (self->impl->shutdownRequested.load()) break;   // M0.2
                    GridData fr;
                    if (loader.fetchFrame(idx[k].url, idx[k].timeUtc, fr))
                        self->pushHistoryFrame(fr, idx[k].timeUtc, false);
                    self->impl->histDone.store(self->impl->histDone.load() + 1);
                }
                printf("[App] History preload: %d cached + %d fetched\n", nCached, (int)want.size());
            }
            self->impl->histLoading.store(false);
        });
    }

    // GIRO ionosondas (Fase 3): el catalogo tambien va en background para no
    // congelar el arranque (30 s de timeout en el peor caso).
    impl->giro.init();   // solo curl_global_init + printf (sin red)
    impl->ionosondeLayer.init();
    impl->hfTrace.init();   // M8: shader hftrace (archivo + fallback) + VAO/VBO
    impl->ionosondeLayer.setVisible(true);
    impl->ionosondeLayer.setHeightOffset(320.0f / 6371.0f);
    impl->ionosondeLayer.setPointSize(11.0f);
    {
        App* self = this;
        // M0.2: hilo joinable (shutdown lo espera); el pre-chequeo evita
        // EMPEZAR un fetch durante el cierre (un fetch en vuelo se espera,
        // acotado por el timeout interno de curl).
        impl->catalogPreloadThread = std::thread([self]() {
            // Release-prep: reintento visible (antes 1x silencioso al arranque).
            for (int a = 0; a < 3 && !self->impl->shutdownRequested.load(); a++) {
                if (self->impl->giro.fetchCatalog()) {
                    self->impl->catalogReady.store(true);
                    break;
                }
                printf("[GiroAdapter] catalogo intento %d/3 fallido\n", a + 1);
                for (int s = 0; s < 5 && !self->impl->shutdownRequested.load(); s++)
                    std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            self->impl->catalogDone.store(true);   // M4R-A: catalogo intentado (exito o fallo)
        });
    }

    // M-IRTAM-F2 (P4, F2-R2): backfill gambit-coeffs en background (foF2+hmF2).
    // Hilo propio joinable (molde M0.2): el pase worker va a 300 s y el
    // backfill frio necesita ritmo de gate (1 req/15 s ~= 48 min UNA vez).
    // Steady: rechequeo cada 300 s (nace ~1 slot/ciclo, natural <= 2).
    // P1b (flip aditivo, F2-R2 cond. 1-2): si el cache P2 tiene buckets
    // frescos foF2+hmF2 con TOV COMUN, se evalua a malla 46x45 y se publica
    // valid=true; si no, valid=false (comportamiento P4 honesto intacto).
    // Sin cirugia de tipos ni wiring: solo relleno + valid.
    {
        App* self = this;
        impl->irtamBackfillThread = std::thread([self]() {
            irtamfetch::IrtamCoeffAdapter irt;  // instancia propia del hilo
            irt.init();  // solo curl_global_init + printf (sin red)
            irt.setCacheDir("cache");
            auto publish = [&]() {
                auto b = std::make_shared<irtamfetch::IrtamBundle>();
                b->dataEpoch = irt.freshestTov();
                b->valid = false;  // P4: sin dato no se dibuja (F2-R2)
                b->error = irt.getErrorMessage();
                // P1b: intento de flip aditivo (mismo TOV en ambos params).
                do {
                    const irtamccache::ScanResult sc =
                        irtamccache::loadCached("cache");
                    if (sc.buckets.empty()) break;
                    // TOV comun mas fresca: interseccion foF2 x hmF2.
                    double tovF = 0.0, tovH = 0.0;
                    const irtamccache::Bucket* bF = nullptr;
                    const irtamccache::Bucket* bH = nullptr;
                    for (const auto& bk : sc.buckets) {
                        if (bk.param == "fof2" && bk.tovEpoch > tovF) {
                            tovF = bk.tovEpoch;
                            bF = &bk;
                        } else if (bk.param == "hmf2" && bk.tovEpoch > tovH) {
                            tovH = bk.tovEpoch;
                            bH = &bk;
                        }
                    }
                    if (!bF || !bH || tovF <= 0.0 || tovF != tovH) break;
                    irtamcoeff::Message mf, mh;
                    std::string err;
                    if (!irtamcoeff::parseMessage(
                            std::string(bF->bytes.begin(), bF->bytes.end()),
                            mf, err) ||
                        !irtamcoeff::isStandardBasis(mf.header))
                        break;
                    if (!irtamcoeff::parseMessage(
                            std::string(bH->bytes.begin(), bH->bytes.end()),
                            mh, err) ||
                        !irtamcoeff::isStandardBasis(mh.header))
                        break;
                    const double tov = mf.header.tovEpoch;
                    const long long day = (long long)std::floor(tov / 86400.0);
                    const double sod = tov - (double)day * 86400.0;
                    const float tovHour = (float)(sod / 3600.0);
                    const double ryear = irtameval::decimalYearForTov(tov);
                    std::vector<float> gF, gH;
                    if (!irtameval::evalGrid(mf.coeffs, ryear, tovHour,
                                             tovHour, gF))
                        break;
                    if (!irtameval::evalGrid(mh.coeffs, ryear, tovHour,
                                             tovHour, gH))
                        break;
                    b->foF2.width = irtameval::kGridW;
                    b->foF2.height = irtameval::kGridH;
                    b->foF2.values = std::move(gF);
                    b->foF2.timestamp = tov;
                    b->foF2.valid = true;
                    b->hmF2.width = irtameval::kGridW;
                    b->hmF2.height = irtameval::kGridH;
                    b->hmF2.values = std::move(gH);
                    b->hmF2.timestamp = tov;
                    b->hmF2.valid = true;
                    b->dataEpoch = tov;
                    b->valid = true;
                    b->error.clear();
                } while (false);
                // F2-B0B1-R1 Q2 (gate por parejas, aditivo): par B0xB1 con el
                // mismo shape. TOV-comun propio (tovB0 == tovB1); si falla o
                // desincroniza, b0/b1 quedan ausentes y el bundle PUBLICA
                // igual con el par F/H (valid/dataEpoch: solo F/H, Q1).
                // El bloque F/H de arriba ni se toca.
                do {
                    const double tovBB = irtamccache::commonTovEpoch(
                        irtamccache::loadCached("cache").buckets, "b0", "b1");
                    if (tovBB <= 0.0) break;
                    const irtamccache::ScanResult scB =
                        irtamccache::loadCached("cache");
                    const irtamccache::Bucket* b0 = nullptr;
                    const irtamccache::Bucket* b1 = nullptr;
                    for (const auto& bk : scB.buckets) {
                        if (bk.param == "b0" && bk.tovEpoch == tovBB) b0 = &bk;
                        if (bk.param == "b1" && bk.tovEpoch == tovBB) b1 = &bk;
                    }
                    if (!b0 || !b1) break;
                    irtamcoeff::Message m0, m1;
                    std::string errB;
                    if (!irtamcoeff::parseMessage(
                            std::string(b0->bytes.begin(), b0->bytes.end()),
                            m0, errB) ||
                        !irtamcoeff::parseMessage(
                            std::string(b1->bytes.begin(), b1->bytes.end()),
                            m1, errB))
                        break;
                    const double ryearB =
                        irtameval::decimalYearForTov(tovBB);
                    const long long dayB =
                        (long long)std::floor(tovBB / 86400.0);
                    const float tovHourB =
                        (float)((tovBB - (double)dayB * 86400.0) / 3600.0);
                    GridData g0, g1;
                    // evalPairGrids exige basis standard + mismo TOV (Q2).
                    if (!irtameval::evalPairGrids(m0, m1, ryearB, tovHourB,
                                                 tovHourB, g0, g1))
                        break;
                    b->b0 = std::move(g0);
                    b->b1 = std::move(g1);
                } while (false);
                { std::lock_guard<std::mutex> lk(self->impl->bundleMutex); self->impl->irtam = b; }
                // M-prov-matrix: fila IRTAM gambit (1 fila del hilo irtam).
                provstatus::Snapshot sn;
                sn.id = provstatus::PROV_IRTAM;
                sn.hasData = b->valid;
                sn.dataEpoch = b->dataEpoch;
                sn.data = b->valid ? irtamstate::formatTovShort(b->dataEpoch) : "";
                sn.error = b->error;
                { std::lock_guard<std::mutex> lk(self->impl->bundleMutex); self->impl->provBoard[provstatus::PROV_IRTAM] = sn; }
            };
            auto sleepIntr = [&](int ms100) {
                for (int i = 0; i < ms100 && !self->impl->shutdownRequested.load(); i++)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            };
            publish();
            for (;;) {
                if (self->impl->shutdownRequested.load()) break;
                const double now = (double)std::time(nullptr);
                const std::vector<irtamfetch::FetchTarget> targets =
                    irt.planBackfill(now);
                if (targets.empty()) { sleepIntr(3000); continue; }  // steady: 300 s
                if (irt.fetchOnce(targets.front())) publish();
                else {
                    publish();
                    sleepIntr(50);  // gate o fallo: reintento en ~5 s (sin burst, C1)
                }
            }
        });
    }
    // M-irtam-replay-024 (ruling R2 PERF): worker asincrono dueño del
    // muestreo IRTAM al epoch (precedentes irtamBackfillThread P4 / sdoKick
    // M11). El render NUNCA muestrea: postea peticion idempotente y consume
    // la ultima muestra lista (handoff corto). En miss se pinta lo anterior.
    {
        App* self = this;
        impl->irtamSampleThread = std::thread([self]() {
            for (;;) {
                int pk = -1;
                long long slot = -1;
                double epoch = 0.0;
                {
                    std::lock_guard<std::mutex> lk(self->impl->irtamSampleMutex);
                    if (self->impl->shutdownRequested.load()) break;
                    if (!self->impl->irtamSampleReq_.pending) {
                        self->impl->irtamSampleBusy.store(false);
                    } else {
                        pk = self->impl->irtamSampleReq_.pairKind;
                        slot = self->impl->irtamSampleReq_.epochSlot;
                        epoch = self->impl->irtamSampleReq_.epoch;
                        self->impl->irtamSampleReq_.pending = false;
                        self->impl->irtamSampleBusy.store(true);
                    }
                }
                if (pk < 0) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(50));
                    continue;
                }
                // M-irtam-replay-028: el worker entrega el BRACKET (endpoints);
                // el render interpola por cursor (simetria TEC).
                if (self->sampleIrtamBracketWorker(pk, epoch, slot)) {
                    std::lock_guard<std::mutex> lk(self->impl->irtamSampleMutex);
                    self->impl->irtamSampleBusy.store(false);
                } else {
                    // Fallo del par: se conserva la muestra anterior
                    // (ausente honesto, heredado Q2-B0B1).
                    std::lock_guard<std::mutex> lk(self->impl->irtamSampleMutex);
                    self->impl->irtamSampleBusy.store(false);
                }
            }
        });
    }
    impl->dias.init();
    impl->dias.fetchAsync((double)std::time(nullptr));  // complemento EU en background
    impl->kc2g.init();   // M4R-A: failover (solo curl_global_init + printf)

    // Viento solar (Fase 4): capa de flechas 3D (datos via worker).
    impl->solarWindLayer.init();
    impl->solarWindLayer.setHeightOffset(0.02f);     // justo sobre la superficie
    impl->solarWindLayer.setArrowScale(1.0f);

    // Propagacion HF + Faraday (Fase 5): capa de vectores 3D (datos via worker).
    impl->faradayLayer.init();
    impl->faradayLayer.setHeightOffset(0.03f);
    impl->faradayLayer.setScale(1.0f);

    // Volumen Ne(h) real (raymarching; se rellena en update cada ~5 s).
    impl->volumeLayer.init();
    impl->volumeLayer.setColormap("viridis");
    impl->volumeLayer.setOpacityScale(1.0f);
    impl->volumeLayer.setVisible(false);
    // Cascaras derivadas (Fase 2/3): arrancan ocultas, se rellenan con el volumen.
    impl->peakShell.init();
    impl->peakShell.setColormap("plasma");
    impl->peakShell.setAltitude(0.0f);   // manda la geometria (hm real)
    impl->peakShell.setOpacity(0.9f);
    impl->peakShell.setVisible(false);
    impl->tecColShell.init();
    impl->tecColShell.setColormap("viridis");
    impl->tecColShell.setAltitude(0.0f);   // geometria plana a 300 km
    impl->tecColShell.setOpacity(0.7f);
    impl->tecColShell.setVisible(false);
    // Cortina QTH-DX (se rellena al haber volumen + pin + hover).
    impl->sliceLayer.init();
    impl->sliceLayer.setVisible(false);

    // Anillos de referencia E/F1/F2 (ocultos por defecto).
    impl->refShells.init();
    impl->refShells.setVisible(false);

    // Ovalo auroral (NOAA Ovation): overlay verde (datos via worker).
    impl->auroraLayer.init();
    impl->auroraLayer.setColormap("aurora");
    impl->auroraLayer.setAltitude(110.0f / 6371.0f);  // ~110 km, bajo la capa TEC
    impl->auroraLayer.setOpacity(0.85f);
    impl->auroraLayer.setVisible(true);

    // Perfil directo del Ebre (FTP SAO, EB040) en background: no bloquea el
    // arranque aunque el firewall corte el canal de datos FTP.
    impl->ebro.init();
    impl->ebro.fetchAsync((double)std::time(nullptr));

    // Hilo worker de red: indices + TEC + viento + aurora + propagacion.
    // El arranque ya no espera a la red (pantalla de carga con progreso).
    impl->fetchThread = std::thread(&App::fetchWorker, this);

    // Framebuffers (M1: la escena con MSAA persistible; bloom sin MSAA:
    // post-proceso sobre quads, no hay geometria que se aliese).
    const int msaaUsedAtInit = impl->msaaSamples;
    impl->sceneFBO.init(winW, winH, impl->msaaSamples);
    impl->bloomFBO[0].init(winW / 2, winH / 2);
    impl->bloomFBO[1].init(winW / 2, winH / 2);

    // Fullscreen quad for post-processing (bloom, composite)
    float quadVerts[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
    };
    glGenVertexArrays(1, &impl->quadVAO);
    glGenBuffers(1, &impl->quadVBO);
    glBindVertexArray(impl->quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, impl->quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Set scroll callback once (respeta la UI: sobre paneles, la rueda es de ImGui).
    glfwSetWindowUserPointer(window, this);
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double sy) {
        auto* self = static_cast<App*>(glfwGetWindowUserPointer(w));
        if (!self) return;
        if (ImGui::GetIO().WantCaptureMouse) return;  // encima de la UI: no zoom
        self->impl->camera.distance -= (float)sy * 0.2f;
    });

    impl->updateSunDirection((double)std::time(nullptr));

    // Persistencia GUI: aplicar settings.cfg si existe (visibilidades,
    // variables, sliders, camara) y refrescar las capas en consecuencia.
    loadSettings();
    restoreTecCache();  // M10: anillo + bundle desde disco antes del worker
    restoreKc2gCache();  // FASE B (P2): historia kc2g desde disco antes del worker
    // M1: settings.cfg puede pedir otro MSAA (la carga corre tras el primer
    // init de FBOs): un recreate unico si difiere del usado arriba.
    if (impl->msaaSamples != msaaUsedAtInit)
        recreateRenderTargets(winW, winH);
    impl->tecLayer.setVisible(impl->tecVisible);
    impl->tecLayer.setOpacity(impl->tecOpacity);
    applyAltitudes();
    impl->shellLayer.setOpacity(impl->shellOpacity);
    {
        static const char* autoCM[4] = { "viridis", "inferno", "plasma", "turbo" };
        impl->shellLayer.setColormap(autoCM[impl->shellColormap & 3]);
    }    impl->ionosondeLayer.setVisible(impl->giroStationsVisible);
    impl->ionosondeLayer.setMode(impl->giroMode);
    impl->ionosondeLayer.setPointSize(impl->giroPointSize);
    impl->solarWindLayer.setVisible(impl->swArrowsVisible);
    impl->solarWindLayer.setArrowScale(impl->swArrowScale);
    impl->faradayLayer.setVisible(impl->faradayVisible);
    impl->faradayLayer.setScale(impl->faradayScale);
    impl->auroraLayer.setVisible(impl->auroraVisible);
    impl->auroraLayer.setOpacity(impl->aurOpacity);
    impl->volumeLayer.setVisible(impl->volumeVisible);
    impl->volumeLayer.setOpacityScale(impl->volumeOpacity);
    impl->volumeLayer.setColorMode(impl->volumeMode);
    impl->volumeLayer.setIsoBands(impl->isoBands);
    impl->volumeLayer.setExplode(impl->explodeFactor);
    impl->refShells.setExplode(impl->explodeFactor);
    impl->peakShell.setVisible(impl->peakShellVisible);
    impl->tecColShell.setVisible(impl->tecColVisible);
    impl->sliceLayer.setVisible(impl->sliceOn);
    impl->refShells.setVisible(impl->refShellsVisible);
    applyColorLayer();
    applyRelief();
    applyAltitudes();
    impl->volumeLayer.setExplode(impl->explodeFactor);
    impl->refShells.setExplode(impl->explodeFactor);

    printf("[OK] All initialized\n");
    return true;
}

// M0.1: recrear los recursos GL dependientes del tamaño. Se llama desde el
// bucle de frames (resize diferido) y desde la UI de render (cambio de MSAA):
// siempre en hilo principal con contexto actual. M1: recrea tambien los
// targets MSAA/resolve del sceneFBO — sigue siendo el UNICO punto de recreacion.
void App::recreateRenderTargets(int w, int h) {
    winW = w;
    winH = h;
    // destroy() antes de init(): Framebuffer::init genera ids nuevos sin
    // liberar los viejos (evita un leak de texturas/FBO en cada resize).
    impl->sceneFBO.destroy();
    impl->bloomFBO[0].destroy();
    impl->bloomFBO[1].destroy();
    if (!impl->sceneFBO.init(w, h, impl->msaaSamples))
        fprintf(stderr, "[App] sceneFBO recreate failed at %dx%d\n", w, h);
    impl->bloomFBO[0].init(w / 2, h / 2);
    impl->bloomFBO[1].init(w / 2, h / 2);
    impl->camera.aspect = (h > 0) ? (float)w / (float)h : 1.0f;
    glViewport(0, 0, w, h);
}

void App::run() {
    while (!glfwWindowShouldClose(window)) {
        // M0.1: resize diferido (el callback solo encola; aqui hay contexto).
        const int pendingW = g_pendingResizeW.load(std::memory_order_relaxed);
        const int pendingH = g_pendingResizeH.load(std::memory_order_relaxed);
        if (pendingW > 0 && pendingH > 0 && (pendingW != winW || pendingH != winH))
            recreateRenderTargets(pendingW, pendingH);

        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        impl->time = currentFrame;

        glfwPollEvents();
        processInput();
        update(deltaTime);
        render();
        glfwSwapBuffers(window);
    }
}

void App::processInput() {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // Cinema (C): solo auto-rotacion. Hide UI (H): oculta todo (opcion aparte).
    bool cDown = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
    if (cDown && !impl->cPrev) {
        impl->cineMode = !impl->cineMode;
        snprintf(impl->hintMsg, sizeof(impl->hintMsg), "CINEMA %s - press C to exit",
                 impl->cineMode ? "ON" : "OFF");
        impl->cineHintUntil = (float)glfwGetTime() + 4.0f;
    }
    impl->cPrev = cDown;
    bool hDown = glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS;
    if (hDown && !impl->hPrev) {
        impl->hideUI = !impl->hideUI;
        if (impl->hideUI) {
            snprintf(impl->hintMsg, sizeof(impl->hintMsg), "UI HIDDEN - press H to restore");
            impl->cineHintUntil = (float)glfwGetTime() + 4.0f;
        }
    }
    impl->hPrev = hDown;
    if (impl->cineMode) {
        impl->camera.rotateY += deltaTime * impl->cineSpeed;
    }

    // Mouse orbit
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    // No orbitar el globo cuando el ratón está sobre la interfaz (sliders, checkboxes),
    // para que arrastrar un slider no gire la cámara.
    ImGuiIO& io = ImGui::GetIO();
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !io.WantCaptureMouse) {
        float dx = (float)(xpos - lastMouseX);
        float dy = (float)(ypos - lastMouseY);
        impl->camera.rotateY += dx * 0.005f;
        impl->camera.rotateX += dy * 0.005f;
        impl->camera.rotateX = glm::clamp(impl->camera.rotateX, -1.5f, 1.5f);
    }

    lastMouseX = (float)xpos;
    lastMouseY = (float)ypos;

    // Zoom por rueda: solo cuando el cursor NO esta sobre la UI (si esta sobre
    // un slider, la rueda lo ajusta a el; el callback de scroll lo respeta).
    // (El callback real se instala una vez en init; aqui no se toca.)
    // Simple zoom with keys
    if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS)
        impl->camera.distance -= 0.02f;
    if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS)
        impl->camera.distance += 0.02f;

    impl->camera.distance = glm::clamp(impl->camera.distance, 1.5f, 20.0f);
}

void App::update(float dt) {

    // Catalogo GIRO listo (hilo background) -> aplicar puntos y arrancar datos.
    if (impl->catalogReady.load() && !impl->catalogApplied) {
        impl->ionosondeLayer.updateSamples(giroPointsFrom(impl->giro.getSamples()));
        impl->giro.fetchStationsAsync();
        impl->catalogApplied = true;
    }

    // Consumir bundles del hilo worker (red+parse fuera del hilo de render).
    consumeBundles();

    // GIRO: refresco periodico de las estaciones en background (5 min; M4:
    // el adapter filtra por estacion con cache 10 min/backoff y solo hace
    // polling a las prioritarias). Ebro directo: idem. DIAS complemento: idem.
    // La red NOAA la hace el worker.
    static double lastIndexFetch = -1.0;
    double now = glfwGetTime();
    if (lastIndexFetch < 0 || (now - lastIndexFetch) > 300.0) {
        if (impl->catalogApplied && !impl->giro.isFetching()) {
            impl->giro.fetchStationsAsync();
        }
        impl->ebro.fetchAsync((double)std::time(nullptr));
        if (!impl->dias.isFetching()) {
            impl->dias.fetchAsync((double)std::time(nullptr));
        }
        lastIndexFetch = now;
    }

    // ---- M4R-A (FASE A sellada): failover kc2g — lgdc primario escalonado,
    // kc2g entra SOLO cuando lgdc falla (conmutacion automatica en ambos
    // sentidos; always-on moveria el riesgo de baneo al redistribuidor
    // voluntario — fuera por sello). Evidencia NEGATIVA de fallo de lgdc:
    // ronda con >=3 intentos y 0 aciertos (429 duro en curso) o catalogo
    // caido (sin estaciones: sin ronda posible). Evidencia POSITIVA de
    // recuperacion: aciertos en la ronda Y datos. Histeresis anti-flap:
    // una vez en fallback se sale SOLO con evidencia positiva — la
    // ventana breve de ronda recien reseteada (attempted<3) no cuenta
    // como recuperacion. La ronda escalonada sigue su curso en
    // background — primaria sigue siendo primaria. Si kc2g tambien cae
    // (>30 min sin batch bueno), la red final es el modelo (M5). ----
    const bool kc2gMode = [&]() -> bool {
        GiroAdapter::GiroNetStatus gsF = impl->giro.getNetStatus();
        const bool catalogFailed = impl->catalogDone.load() && !impl->catalogReady.load();
        const bool failing = (gsF.roundAttempts >= 3 && gsF.roundOk == 0) || catalogFailed;
        const bool recovering = (gsF.roundOk > 0 && gsF.withData > 0);
        const int prevMode = impl->ionSourceMode.load();
        const bool wantFailover = failing ||
            (prevMode == Impl::SRC_KC2G_FALLBACK && !recovering);
        const double wallNow = (double)std::time(nullptr);
        Kc2gAdapter::Kc2gStatus ks = impl->kc2g.getStatus();
        const bool kc2gActive = ks.panelUsable > 0 && ks.lastOkUtc > 0.0 &&
                                (wallNow - ks.lastOkUtc) <= 1800.0;
        int mode = Impl::SRC_MODEL;
        if (wantFailover && kc2gActive) mode = Impl::SRC_KC2G_FALLBACK;
        else if (gsF.withData > 0 && !failing) mode = Impl::SRC_GIRO_LIVE;
        impl->ionSourceMode.store(mode);
        // Fetch del failover: 1 GET por ciclo (600 s, el ritmo GIRO) SOLO
        // mientras se quiera failover — nunca en GIRO-live estable.
        if (wantFailover && !impl->kc2g.isFetching() &&
            (ks.lastFetchUtc <= 0.0 || wallNow - ks.lastFetchUtc >= 600.0)) {
            impl->kc2g.fetchAsync(wallNow);
        }
        // Fusion cacheada del modo fallback (antes de replay/gsLive).
        if (mode == Impl::SRC_KC2G_FALLBACK && ks.lastOkUtc != impl->kc2gMergedStamp) {
            impl->kc2gMerged = mergeKc2g(impl->giro.getSamples(), impl->kc2g.copySamples());
            impl->kc2gMergedStamp = ks.lastOkUtc;
        }
        return mode == Impl::SRC_KC2G_FALLBACK;
    }();

    // Replay temporal: TEC interpolado + red GIRO muestreada al mismo instante.
    // En replay, puntos, grids derivados (foF2/MUF/hmF2/NmF2/LUF/FOT/Spread-F)
    // y panel de estacion siguen al tiempo de replay, no al directo.
    // M-irtam-replay-A (ruling R1 Q5): el cursor primario es el EPOCH
    // continuo sobre la ventana de union (96 h); replayPos se DERIVA del
    // epoch para el lerp TEC interno (inversion mecanica del bloque previo).
    double replayEpoch = -1.0;
    static bool kc2gReplayLogged = false;  // FASE B (P3): log una vez por entrada
    {
        // Copia minima bajo lock (el hilo de precarga puede estar escribiendo).
        GridData fa, fb;
        double f = 0.0;
        bool havePair = false;
        if (impl->replayMode) {
            const double nowUtc = (double)std::time(nullptr);
            const double winStart =
                nowUtc - irtamstate::kIrtamReplayWindowSec;
            // M-irtam-replay-031 (toggle): un solo camino con bounds; en modo
            // por capa los bounds son el rango con dato de la activa, con
            // fallback declarado a la union (jamas cursor congelado).
            double loBound = winStart;
            double hiBound = nowUtc;
            if (impl->replayLayerLoop) {
                irtamstate::LayerRanges lr;
                {
                    std::lock_guard<std::mutex> tlk(impl->tecHistMutex);
                    if (impl->tecHist.size() >= 2) {
                        lr.tecOldest = impl->tecHist.front().timestamp;
                        lr.tecNewest = impl->tecHist.back().timestamp;
                    }
                }
                const double hh = impl->giro.getNetStatus().histHours;
                if (hh > 0.0) {
                    lr.giroNewest = nowUtc;
                    lr.giroOldest = nowUtc - hh * 3600.0;
                }
                const irtamstate::LayerLoopRange llr =
                    irtamstate::layerLoopRange(impl->mapVariable, nowUtc, lr);
                if (llr.valid) {
                    loBound = llr.oldest;
                    hiBound = llr.newest;
                }
            }
            if (impl->replayPlaying) {
                if (impl->replayEpoch <= 0.0) impl->replayEpoch = loBound;
                // 1x = 1 frame TEC equiv/s (1200 s); loop en [loBound,hiBound]:
                // union 96 h por defecto, rango con dato en modo por capa.
                // M-irtam-replay-024 (ruling R2 PERF): dt acotado a <=100 ms
                // SOLO para este avance (rompe la amplificacion miss->dt
                // gigante->salto multi-slot->miss); el dt global intacto.
                const double advDt = dt > 0.1 ? 0.1 : (double)dt;
                impl->replayEpoch +=
                    advDt * impl->replaySpeed * 1200.0;
                if (impl->replayEpoch > hiBound) impl->replayEpoch = loBound;
            }
            if (impl->replayEpoch <= 0.0) impl->replayEpoch = loBound;
            if (impl->replayEpoch < loBound) impl->replayEpoch = loBound;
            if (impl->replayEpoch > hiBound) impl->replayEpoch = hiBound;
            replayEpoch = impl->replayEpoch;
            std::lock_guard<std::mutex> lk(impl->tecHistMutex);
            if (impl->tecHist.size() >= 2) {
                double pos = 0.0;
                if (replayEpoch <= impl->tecHist.front().timestamp) {
                    pos = 0.0;
                } else if (replayEpoch >= impl->tecHist.back().timestamp) {
                    pos = (double)impl->tecHist.size() - 1.0;
                } else {
                    for (size_t k = 0; k + 1 < impl->tecHist.size(); k++) {
                        const double t0 = impl->tecHist[k].timestamp;
                        const double t1 = impl->tecHist[k + 1].timestamp;
                        if (replayEpoch >= t0 && replayEpoch <= t1) {
                            pos = (double)k +
                                  (t1 > t0 ? (replayEpoch - t0) / (t1 - t0)
                                           : 0.0);
                            break;
                        }
                    }
                }
                impl->replayPos = pos;
                size_t i0 = (size_t)pos;
                if (i0 >= impl->tecHist.size() - 1) i0 = impl->tecHist.size() - 2;
                f = pos - (double)i0;
                fa = impl->tecHist[i0];
                fb = impl->tecHist[i0 + 1];
                havePair = true;
            }
        } else {
            impl->giroReplayCache.clear();
        }
        if (havePair) {
            if (impl->mapVariable == 0) {
                updateMainGrid(blendGrids(fa, fb, f));
            }
            replayEpoch = fa.timestamp * (1.0 - f) + fb.timestamp * f;
            // FASE B (P3): en fallback se fusionan HISTORIAS muestreadas al
            // replayEpoch (GIRO pre-caida + kc2g acumulada P1). Fuera del
            // rango acumulado el clamp es freeze honesto (una linea de log).
            if (kc2gMode) {
                impl->giroReplayCache = mergeKc2g(impl->giro.sampleHistoryAt(replayEpoch),
                                                  impl->kc2g.sampleHistoryAt(replayEpoch));
                if (!kc2gReplayLogged) {
                    kc2gReplayLogged = true;
                    int nSt = 0;
                    double t0 = 0.0, t1 = 0.0;
                    if (impl->kc2g.historyStats(nSt, t0, t1) && nSt > 0) {
                        char c0[16] = "--:--", c1[16] = "--:--";
                        { std::time_t tt = (std::time_t)t0; std::tm gg;
#ifdef _WIN32
                          gmtime_s(&gg, &tt);
#else
                          gmtime_r(&tt, &gg);
#endif
                          strftime(c0, sizeof(c0), "%H:%M", &gg); }
                        { std::time_t tt = (std::time_t)t1; std::tm gg;
#ifdef _WIN32
                          gmtime_s(&gg, &tt);
#else
                          gmtime_r(&tt, &gg);
#endif
                          strftime(c1, sizeof(c1), "%H:%M", &gg); }
                        printf("[kc2g] replay: %d st con historia, rango [%s..%s UTC]\n",
                               nSt, c0, c1);
                    } else {
                        printf("[kc2g] replay: sin historia acumulada (freeze honesto FASE A)\n");
                    }
                }
            } else {
                impl->giroReplayCache = impl->giro.sampleHistoryAt(replayEpoch);
                kc2gReplayLogged = false;
            }
        } else {
            kc2gReplayLogged = false;
        }
    }

    // M2: el sol sigue al dato - epoch efectivo = replay si hay par, wall-clock
    // si no. Snap puro al conmutar live<->replay: el dato salta en ese mismo
    // frame y un ease dejaria el terminador incoherente con el grid durante la
    // transicion (enmienda MUSE al sello del scope: coherencia > suavidad).
    // El wrap del loop de replay tambien hace snap (el dato salta ahi igual).
    // M-irtam-replay-027 (opcion A del veredicto 026): el sol sigue al CURSOR
    // (impl->replayEpoch), no al local TEC-clamped — si no, queda clavado en
    // T-24h el 75% del bucle de 96 h. En live, wall-clock como siempre.
    double effEpoch = (impl->replayMode && impl->replayEpoch > 0.0)
                          ? impl->replayEpoch
                          : (double)std::time(nullptr);
    impl->updateSunDirection(effEpoch);
    impl->terminator.update(impl->sunDirection);  // anillo dia/noche real (~1 KB/frame)

    // M4R-A: fuente viva por modo (kc2g-fallback -> fusion cacheada).
    const std::vector<StationSample>& gsLive =
        kc2gMode ? impl->kc2gMerged : impl->giro.getSamples();
    const std::vector<StationSample>& gsAct =
        (impl->replayMode && !impl->giroReplayCache.empty()) ? impl->giroReplayCache : gsLive;

    // Reflejar en la capa de ionosondas el estado de las muestras GIRO (streaming
    // incremental: el hilo background va completando estaciones poco a poco).
    // En replay se muestran las muestras interpoladas al tiempo de replay.
    // En live se fusiona DIAS (rellena huecos GIRO con coords del catalogo).
    impl->ionosondeLayer.setFetching(impl->giro.isFetching() && !impl->replayMode);
    impl->ionosondeLayer.setProgress(impl->giro.getProgress());
    impl->ionosondeLayer.setDataIndicator(
        kc2gMode ? impl->kc2g.getDataIndicator() : impl->giro.getDataIndicator());
    static double lastSamplesApply = -1.0;
    if (lastSamplesApply < 0 || (now - lastSamplesApply) > 2.0) {
        if (impl->replayMode) {
            impl->ionosondeLayer.updateSamples(giroPointsFrom(gsAct));
        } else {
            impl->ionosondeLayer.updateSamples(
                giroPointsFrom(mergeDias(gsLive, impl->dias.copySamples())));
        }
        lastSamplesApply = now;
    }

    // Si la variable de mapa es una magnitud GIRO (1-4), re-agregar su grid interpolado
    // cuando las muestras gyro hayan cambiado (streaming incremental).
    // Fase 5: variables 5-7 (LUF/FOT/Spread-F) via RadioPropagationAdapter.
    // En replay, los grids derivados tambien siguen al tiempo de replay (gsAct).
    // M-irtam-replay-024 (ruling R2 PERF): el render NUNCA muestrea en
    // caliente — consumeIrtamBracket postea al worker y pinta lo listo.
    if (impl->mapVariable >= 1 && impl->mapVariable <= 4) {
        bool centered = (impl->mapVariable == 3);  // hmF2: relieve de alturas reales
        impl->tecLayer.setDisplaceCentered(centered);
        impl->shellLayer.setDisplaceCentered(centered);
        updateMainGrid(giroInterpGrid(gsAct, impl->mapVariable - 1, 72, 72));
    } else if (impl->mapVariable >= 5 && impl->mapVariable <= 7) {
        float kpProxy = currentKp();
        double epoch = (impl->replayMode && impl->replayEpoch > 0.0) ? impl->replayEpoch
                       : (replayEpoch > 0.0)                         ? replayEpoch
                                                                     : (double)std::time(nullptr);
        // M7 (plan 10): flujo X-ray del ultimo bundle para el termino SID
        // del LUF en la rejilla (mismo guard que el flare del volumen).
        float xrFlux = (impl->lastXray && impl->lastXray->latest.valid)
            ? impl->lastXray->latest.flux : 0.0f;
        updateMainGrid(radioInterpGrid(gsAct,
            impl->mapVariable - 5, 72, 72, epoch, kpProxy, xrFlux));
    } else if (impl->mapVariable == 8 || impl->mapVariable == 9) {
        // M-IRTAM-F2 (P4, F2-R2): streaming honesto — sin valid no se dibuja.
        // M-irtam-replay-A + TEC-ext 034: ventana de union (96 h) =
        // [T-96,T-72] solo-IRTAM (muestreo lerp al epoch) · [T-72,T]
        // solo-TEC (TEC-ext 034: ventana 72 h con backfill de precarga) ·
        // solape TEC-IRTAM = borde T-72. En live: bundle fresco.
        // En replay: ultima muestra lista del worker (024: jamas en caliente).
        // M-irtam-replay-028: lerp por cursor sobre el bracket (simetria TEC).
        if (impl->replayMode) {
            IrtamBracketView v;
            if (consumeIrtamBracket(0, v)) {
                const std::vector<float>& v0 =
                    (impl->mapVariable == 8) ? v.a0 : v.b0;
                const std::vector<float>& v1 =
                    (impl->mapVariable == 8) ? v.a1 : v.b1;
                GridData g = lerpBracketGrid(v0, v1, v.t0, v.t1,
                                             impl->replayEpoch);
                if (g.valid) updateMainGrid(g);
            }
        } else {
            auto ib = impl->getIrtam();
            if (ib && ib->valid)
                updateMainGrid(impl->mapVariable == 8 ? ib->foF2 : ib->hmF2);
        }
    } else if (impl->mapVariable == 10 || impl->mapVariable == 11) {
        // F2-B0B1-R1 Q3: streaming B0/B1 (misma honestidad; par ausente =
        // ausente). B0 con clamp-color via copia display-only.
        // M-irtam-replay-A: en replay, muestra al epoch (mismo cache P7).
        // D2 (veredicto 018): else espejo de 8/9 — sin el, el bundle fresco
        // pisaria la muestra cada frame.
        // M-irtam-replay-024: consume del worker (jamas en caliente).
        // M-irtam-replay-028: lerp por cursor sobre el bracket (simetria TEC).
        if (impl->replayMode) {
            IrtamBracketView v;
            if (consumeIrtamBracket(1, v)) {
                const std::vector<float>& v0 =
                    (impl->mapVariable == 10) ? v.a0 : v.b0;
                const std::vector<float>& v1 =
                    (impl->mapVariable == 10) ? v.a1 : v.b1;
                GridData g = lerpBracketGrid(v0, v1, v.t0, v.t1,
                                             impl->replayEpoch);
                if (g.valid) {
                    if (impl->mapVariable == 10) {
                        for (float& vv : g.values)
                            if (vv < 0.0f) vv = 0.0f;
                    }
                    updateMainGrid(g);
                }
            }
        } else {
            auto ib = impl->getIrtam();
            if (ib && ib->valid) {
                const GridData& src =
                    (impl->mapVariable == 10) ? ib->b0 : ib->b1;
                if (!src.values.empty() && src.valid) {
                    if (impl->mapVariable == 10) {
                        GridData g = src;
                        for (float& v : g.values)
                            if (v < 0.0f) v = 0.0f;
                        updateMainGrid(g);
                    } else {
                        updateMainGrid(src);
                    }
                }
            }
        }
    }

    // M8 (plan 11.5): grids del circuito HF cacheados en impl + rebuild del
    // circuito SOLO cuando cambia el input (P7): cadencia 1 s en live (los
    // datos GIRO mudan cada ~300 s), salto de epoch > 30 s (scrub de replay
    // reactivo - el trazado y la matriz siguen al scrub, aceptacion 4) o
    // dirty del panel (freq/TX/RX). Apagado por defecto: coste cero salvo
    // capa visible o panel abierto.
    // M8fix-2 (re-captura del bucket): el checkbox/persist gobiernan
    // hfTraceVisible, pero visible_ nace false y nadie llamaba setVisible
    // — render() early-returnaba siempre (HFTraceLayer.cpp) => ribbon
    // indibujable. Sync por frame antes del gate: arranque (settings.cfg),
    // toggle y cierre de panel cubiertos sin invarianza de escritores.
    impl->hfTrace.setVisible(impl->hfTraceVisible);
    if (impl->hfTraceVisible || impl->hfPanelOpen) {
        static double lastHfGrid = -1.0;
        static double lastHfEpoch = -1.0;
        const double hfEpoch = (impl->replayMode && impl->replayEpoch > 0.0) ? impl->replayEpoch
                               : (replayEpoch > 0.0)                         ? replayEpoch
                                                                             : (double)std::time(nullptr);
        const bool hfEpochJump = (lastHfEpoch < 0.0) || std::fabs(hfEpoch - lastHfEpoch) > 30.0;
        if (lastHfGrid < 0.0 || (now - lastHfGrid) > 1.0 || hfEpochJump || impl->hfCircuitDirty) {
            lastHfGrid = now;
            lastHfEpoch = hfEpoch;
            impl->hfCircuitDirty = false;
            float hfKp = currentKp();
            float hfFlux = (impl->lastXray && impl->lastXray->latest.valid)
                ? impl->lastXray->latest.flux : 0.0f;
            impl->hfFoF2Grid = giroInterpGrid(gsAct, 0, 72, 72);   // var de mapa 1 = foF2
            impl->hfHmF2Grid = giroInterpGrid(gsAct, 2, 72, 72);   // var de mapa 3 = hmF2
            impl->hfLufGrid  = radioInterpGrid(gsAct, 0, 72, 72, hfEpoch, hfKp, hfFlux);  // LUF M7
            impl->hfGridStamp++;
            // Resolver TX/RX por codigo sobre gsAct (coords del catalogo):
            // TX = override del dropdown o estacion pineada o DB049 (default
            // demo); RX = override (dropdown/pick) o EG931 (plan 11.4, P0).
            HFTraceLayer::Input hfIn;
            std::string hfWantTx = impl->hfTxCode;
            if (hfWantTx.empty()) hfWantTx = impl->pinnedCode;
            if (hfWantTx.empty()) hfWantTx = "DB049";
            const std::string hfWantRx = impl->hfRxCode.empty() ? std::string("EG931") : impl->hfRxCode;
            for (const auto& hs : gsAct) {
                if (!hfIn.txValid && hs.code == hfWantTx && (hs.latDeg != 0.0f || hs.lonDeg != 0.0f)) {
                    hfIn.txValid = true;
                    hfIn.txLatDeg = (double)hs.latDeg;
                    hfIn.txLonDeg = (double)hs.lonDeg;
                }
                if (!hfIn.rxValid && hs.code == hfWantRx && (hs.latDeg != 0.0f || hs.lonDeg != 0.0f)) {
                    hfIn.rxValid = true;
                    hfIn.rxLatDeg = (double)hs.latDeg;
                    hfIn.rxLonDeg = (double)hs.lonDeg;
                }
            }
            hfIn.freqMHz = impl->hfFreqMHz;
            hfIn.heightExag = impl->explodeFactor;   // M9: el arco acompana la altura visual (Layers/Explode)
            hfIn.apexOffsetKm = impl->altKm - 300.0f;   // M9fix: apice = (h' + altKm-300)·exag — sigue al slider de capa (default 300 = M9 byte-exacto)
            hfIn.gridStamp = impl->hfGridStamp;
            if (impl->hfFoF2Grid.valid && !impl->hfFoF2Grid.values.empty())
                hfIn.foF2Grid = impl->hfFoF2Grid.values.data();
            if (impl->hfHmF2Grid.valid && !impl->hfHmF2Grid.values.empty())
                hfIn.hmF2Grid = impl->hfHmF2Grid.values.data();
            if (impl->hfLufGrid.valid && !impl->hfLufGrid.values.empty())
                hfIn.lufGrid = impl->hfLufGrid.values.data();
            hfIn.gridW = 72;
            hfIn.gridH = 72;
            impl->hfTrace.update(hfIn);
        }
    }

    // Volumen Ne(h) + RefShells dinamicos: reconstruir cada ~5 s con las
    // muestras activas (sigue live, replay, sol y flares).
    static double lastVolBuild = -1.0;
    if (now - lastVolBuild > 5.0) {
        lastVolBuild = now;
        glm::vec3 sd = glm::normalize(impl->sunDirection);
        // M5: mismo vector solar y mismo epoch del volumen para el fondo
        // climatologico (el dia del modelo sigue al terminador, M2/effEpoch);
        // f107 del snapshot que lee el HUD (P4: real o mock 105; 120 solo
        // si aun no hay bundle).
        double volEpoch = (replayEpoch > 0.0) ? replayEpoch : (double)std::time(nullptr);
        double f107 = 120.0;
        auto idxB = impl->getIndices();
        if (idxB) f107 = (double)idxB->snap.f107;
        auto profs = interpLayerProfiles(gsAct, 72, 72, sd, volEpoch, f107);
        applyDayNight(profs, 72, 72, sd.x, sd.y, sd.z);
        float flare = 1.0f;
        if (impl->lastXray && impl->lastXray->latest.valid)
            flare = XrayAdapter::flareFactor(impl->lastXray->latest.flux);
        DensityVolume vol = buildDensityVolume(profs, 72, 72, 60, 60.0f, 700.0f,
                                               sd.x, sd.y, sd.z, flare);
        vol.timestamp = volEpoch;
        impl->volumeLayer.updateVolume(vol);
        impl->lastVolume = vol;
        if (impl->sliceOn) rebuildSlice(true);
        // Fases 2/3: cascaras derivadas del volumen (barato, cada 5 s).
        {
            PeakGrids pk = extractPeakGrids(vol);
            if (pk.logNe.valid)
                impl->peakShell.updateFromGrids(pk.logNe, pk.hmKm);
            GridData tecModel, flatH;
            tecModel.width = flatH.width = vol.lonRes;
            tecModel.height = flatH.height = vol.latRes;
            tecModel.values.reserve((size_t)vol.lonRes * vol.latRes);
            flatH.values.reserve((size_t)vol.lonRes * vol.latRes);
            for (int la = 0; la < vol.latRes; la++) {
                for (int lo = 0; lo < vol.lonRes; lo++) {
                    tecModel.values.push_back(integrateTEC(vol, la, lo));
                    flatH.values.push_back(300.0f);
                }
            }
            tecModel.valid = flatH.valid = vol.valid;
            tecModel.timestamp = flatH.timestamp = vol.timestamp;
            impl->tecColShell.updateFromGrids(tecModel, flatH);
        }
        std::vector<float> hv;
        for (const auto& s : gsAct) if (s.valid && s.hmF2 > 0.0f) hv.push_back(s.hmF2);
        if (!hv.empty()) {
            std::sort(hv.begin(), hv.end());
            float mh = hv[hv.size() / 2];
            if (mh > 650.0f)
                printf("[App] WARNING: median hmF2 %.0f km approaches volume ceiling (700 km)\n", mh);
            impl->refShells.updateAltitudes(110.0f, std::max(150.0f, 0.8f * mh), mh);
        }
    }

    // Viento solar: refrescar la orientacion/longitud/color de las flechas cuando
    // cambie el sol o los datos (throttle ~2s, para no reconstruir el mesh cada frame).
    static double lastArrow = -1.0;
    if (impl->swArrowsVisible && (lastArrow < 0 || (now - lastArrow) > 2.0)) {
        static const SolarWindSample kNoWind{};
        const SolarWindSample& sw = impl->lastWind ? impl->lastWind->latest : kNoWind;
        impl->solarWindLayer.updateArrowData(sw.speed, sw.bz, impl->sunDirection, impl->swArrowMode);
        impl->solarWindLayer.setArrowScale(impl->swArrowScale);
        lastArrow = now;
    }

    // Faraday: refrescar vectores con el TEC local (throttle ~2s).
    static double lastFaraday = -1.0;
    if (impl->faradayVisible && (lastFaraday < 0 || (now - lastFaraday) > 2.0)) {
        static const GridData kNoGrid{};
        const GridData& tecg = impl->lastTec ? impl->lastTec->grid : kNoGrid;
        impl->faradayLayer.updateData(tecg, 15.0f,
                                      impl->sunDirection, impl->faradayMode);
        impl->faradayLayer.setScale(impl->faradayScale);
        lastFaraday = now;
    }

    // Slice QTH-DX: seguir cambios de endpoints (el volumen va cada 5 s).
    if (impl->sliceOn) rebuildSlice(false);
}

void App::applyRelief() {
    float amp = 0.0f;
    if (impl->reliefOn) {
        amp = impl->reliefAmp;
        if (impl->stormSwell) amp *= 1.0f + currentKp() / 6.0f;
    }
    impl->tecLayer.setDisplace(amp);
    impl->shellLayer.setDisplace(amp);
}

void App::applyAltitudes() {
    float f = impl->explodeFactor;
    impl->tecLayer.setAltitude(impl->altKm / 6371.0f * f);
    impl->shellLayer.setAltitude((impl->altKm / 6371.0f + 0.025f) * f);
    impl->auroraLayer.setAltitude(110.0f / 6371.0f * f);
}

void App::updateMainGrid(const GridData& grid) {
    impl->tecLayer.updateFromGrid(grid);
    impl->shellLayer.updateFromGrid(grid);
}

void App::saveSettings() {
    FILE* f = fopen("settings.cfg", "w");
    if (!f) return;
    fprintf(f, "ionolive2\n");   // M1: bloomIntensity lineal (v1 = cuadratica)
    fprintf(f, "mapVariable %d\n", impl->mapVariable);
    fprintf(f, "manualColormap %d\n", impl->manualColormap);
    fprintf(f, "colormapOverride %d\n", impl->colormapOverride ? 1 : 0);
    fprintf(f, "tecOpacity %g\n", (double)impl->tecOpacity);
    fprintf(f, "altKm %g\n", (double)impl->altKm);
    fprintf(f, "giroMode %d\n", impl->giroMode);
    fprintf(f, "giroPointSize %g\n", (double)impl->giroPointSize);
    fprintf(f, "aurOpacity %g\n", (double)impl->aurOpacity);
    fprintf(f, "showEarth %d\n", impl->showEarth ? 1 : 0);
    fprintf(f, "showTerminator %d\n", impl->terminatorVisible ? 1 : 0);
    fprintf(f, "showAtmosphere %d\n", impl->showAtmosphere ? 1 : 0);
    fprintf(f, "atmoQuality %d\n", impl->atmoQuality);      // M6 (plan 9.4)
    fprintf(f, "atmoIntensity %g\n", (double)impl->atmoIntensity);
    fprintf(f, "showStars %d\n", impl->showStars ? 1 : 0);
    fprintf(f, "nightLevel %g\n", (double)impl->nightLevel);
    fprintf(f, "cloudAmt %g\n", (double)impl->cloudAmt);
    fprintf(f, "bloomEnabled %d\n", impl->bloomEnabled ? 1 : 0);
    fprintf(f, "bloomThreshold %g\n", (double)impl->bloomThreshold);
    fprintf(f, "bloomIntensity %g\n", (double)impl->bloomIntensity);
    fprintf(f, "bloomStrength %g\n", (double)impl->bloomStrength);
    fprintf(f, "toneMap %d\n", impl->toneMap ? 1 : 0);
    fprintf(f, "exposure %g\n", (double)impl->exposure);
    fprintf(f, "msaaSamples %d\n", impl->msaaSamples);
    fprintf(f, "reliefOn %d\n", impl->reliefOn ? 1 : 0);
    fprintf(f, "reliefAmp %g\n", (double)impl->reliefAmp);
    fprintf(f, "stormSwell %d\n", impl->stormSwell ? 1 : 0);
    fprintf(f, "outerShell %d\n", impl->outerShell ? 1 : 0);
    fprintf(f, "shellOpacity %g\n", (double)impl->shellOpacity);
    fprintf(f, "shellColormap %d\n", impl->shellColormap);
    fprintf(f, "refShellsVisible %d\n", impl->refShellsVisible ? 1 : 0);
    fprintf(f, "swArrowsVisible %d\n", impl->swArrowsVisible ? 1 : 0);
    fprintf(f, "swArrowMode %d\n", impl->swArrowMode);
    fprintf(f, "swArrowScale %g\n", (double)impl->swArrowScale);
    fprintf(f, "faradayVisible %d\n", impl->faradayVisible ? 1 : 0);
    fprintf(f, "faradayMode %d\n", impl->faradayMode);
    fprintf(f, "faradayScale %g\n", (double)impl->faradayScale);
    fprintf(f, "auroraVisible %d\n", impl->auroraVisible ? 1 : 0);
    fprintf(f, "tecVisible %d\n", impl->tecVisible ? 1 : 0);
    fprintf(f, "giroStationsVisible %d\n", impl->giroStationsVisible ? 1 : 0);
    fprintf(f, "cineSpeed %g\n", (double)impl->cineSpeed);
    fprintf(f, "replaySpeed %g\n", (double)impl->replaySpeed);
    fprintf(f, "volumeVisible %d\n", impl->volumeVisible ? 1 : 0);
    fprintf(f, "pipVisible %d\n", impl->pipVisible ? 1 : 0);
    fprintf(f, "volumeMode %d\n", impl->volumeMode);
    fprintf(f, "isoBands %d\n", impl->isoBands ? 1 : 0);
    fprintf(f, "sliceSrc %d\n", impl->sliceSrc);
    fprintf(f, "sliceLatA %g\n", (double)impl->sliceLatA);
    fprintf(f, "sliceLonA %g\n", (double)impl->sliceLonA);
    fprintf(f, "sliceLatB %g\n", (double)impl->sliceLatB);
    fprintf(f, "sliceLonB %g\n", (double)impl->sliceLonB);
    fprintf(f, "labelsVisible %d\n", impl->labelsVisible ? 1 : 0);
    fprintf(f, "explodeFactor %g\n", (double)impl->explodeFactor);
    fprintf(f, "peakShellVisible %d\n", impl->peakShellVisible ? 1 : 0);
    fprintf(f, "tecColVisible %d\n", impl->tecColVisible ? 1 : 0);
    fprintf(f, "volumeOpacity %g\n", (double)impl->volumeOpacity);
    fprintf(f, "sliceOn %d\n", impl->sliceOn ? 1 : 0);
    fprintf(f, "camRX %g\n", (double)impl->camera.rotateX);
    fprintf(f, "camRY %g\n", (double)impl->camera.rotateY);
    fprintf(f, "camDist %g\n", (double)impl->camera.distance);
    fprintf(f, "pinned %s\n", impl->pinnedCode.empty() ? "-" : impl->pinnedCode.c_str());
    fprintf(f, "hfTraceVisible %d\n", impl->hfTraceVisible ? 1 : 0);   // M8 (plan 11.4)
    fprintf(f, "hfFreqMHz %g\n", (double)impl->hfFreqMHz);
    fprintf(f, "sunVisible %d\n", impl->sunVisible ? 1 : 0);   // M11
    fprintf(f, "sunProduct %d\n", impl->sunProduct);   // M11
    fprintf(f, "sunWinX %g\n", (double)impl->sunGeom.x);   // M11
    fprintf(f, "sunWinY %g\n", (double)impl->sunGeom.y);   // M11
    fprintf(f, "sunWinW %g\n", (double)impl->sunGeom.w);   // M11
    fprintf(f, "sunWinH %g\n", (double)impl->sunGeom.h);   // M11
    fprintf(f, "tecCacheEnabled %d\n", impl->tecCacheEnabled ? 1 : 0);   // M10
    fprintf(f, "tecCacheMaxAgeH %g\n", (double)impl->tecCacheMaxAgeH);   // M10
    fclose(f);
}

void App::loadSettings() {
    FILE* f = fopen("settings.cfg", "r");
    if (!f) return;
    char tag[32] = {0};
    if (!fgets(tag, sizeof(tag), f)) { fclose(f); return; }
    // M1: ionolive2 = intensidad de bloom LINEAL. Un ionolive1 se acepta y se
    // marca para la migracion I-cuadrado (al final del parseo: I' = I^2,
    // la efectiva vieja era I^2 al aplicarse en H y V); una tag
    // desconocida se ignora como siempre (binario viejo + cfg v2 -> defaults).
    bool legacyBloom = false;
    if (std::string(tag).find("ionolive2") != std::string::npos) {
        // formato actual (post-M1)
    } else if (std::string(tag).find("ionolive1") != std::string::npos) {
        legacyBloom = true;
    } else { fclose(f); return; }
    char key[64];
    double v;
    auto clB = [](double x) { return x != 0.0; };
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "pinned %63s", key) == 1) {
            if (std::string(key) != "-") impl->pinnedCode = key;
            continue;
        }
        if (sscanf(line, "%63s %lf", key, &v) != 2) continue;
        std::string k = key;
        if (k == "mapVariable") impl->mapVariable = glm::clamp((int)v, 0, 11);  // M-IRTAM-F2 (P4: 8-9 IRTAM; B0B1: 10-11)
        else if (k == "manualColormap") impl->manualColormap = glm::clamp((int)v, 0, 3);
        else if (k == "colormapOverride") impl->colormapOverride = clB(v);
        else if (k == "tecOpacity") impl->tecOpacity = glm::clamp((float)v, 0.0f, 2.0f);
        else if (k == "altKm") impl->altKm = glm::clamp((float)v, 100.0f, 800.0f);
        else if (k == "giroMode") impl->giroMode = glm::clamp((int)v, 0, 3);
        else if (k == "giroPointSize") impl->giroPointSize = glm::clamp((float)v, 3.0f, 30.0f);
        else if (k == "aurOpacity") impl->aurOpacity = glm::clamp((float)v, 0.0f, 1.5f);
        else if (k == "showEarth") impl->showEarth = clB(v);
        else if (k == "showTerminator") impl->terminatorVisible = clB(v);
        else if (k == "showAtmosphere") impl->showAtmosphere = clB(v);
        else if (k == "atmoQuality") impl->atmoQuality = glm::clamp((int)v, 0, 1);   // M6
        else if (k == "atmoIntensity") impl->atmoIntensity = glm::clamp((float)v, 0.25f, 2.0f);
        else if (k == "showStars") impl->showStars = clB(v);
        else if (k == "nightLevel") impl->nightLevel = glm::clamp((float)v, 0.05f, 1.0f);
        else if (k == "cloudAmt") impl->cloudAmt = glm::clamp((float)v, 0.0f, 1.0f);
        else if (k == "bloomEnabled") impl->bloomEnabled = clB(v);
        else if (k == "bloomThreshold") impl->bloomThreshold = glm::clamp((float)v, 0.0f, 1.0f);
        else if (k == "bloomIntensity") impl->bloomIntensity = glm::clamp((float)v, 0.0f, 3.0f);
        else if (k == "bloomStrength") impl->bloomStrength = glm::clamp((float)v, 0.0f, 2.0f);
        else if (k == "toneMap") impl->toneMap = clB(v);
        else if (k == "exposure") impl->exposure = glm::clamp((float)v, 0.2f, 2.5f);
        else if (k == "msaaSamples") {   // M1: solo 0/2/4 validos, resto -> 4
            int s = (int)v;
            impl->msaaSamples = (s == 0 || s == 2) ? s : 4;
        }
        else if (k == "reliefOn") impl->reliefOn = clB(v);
        else if (k == "reliefAmp") impl->reliefAmp = glm::clamp((float)v, 0.0f, 0.10f);
        else if (k == "stormSwell") impl->stormSwell = clB(v);
        else if (k == "outerShell") impl->outerShell = clB(v);
        else if (k == "shellOpacity") impl->shellOpacity = glm::clamp((float)v, 0.0f, 2.0f);
        else if (k == "shellColormap") impl->shellColormap = glm::clamp((int)v, 0, 3);
        else if (k == "refShellsVisible") impl->refShellsVisible = clB(v);
        else if (k == "swArrowsVisible") impl->swArrowsVisible = clB(v);
        else if (k == "swArrowMode") impl->swArrowMode = glm::clamp((int)v, 0, 1);
        else if (k == "swArrowScale") impl->swArrowScale = glm::clamp((float)v, 0.2f, 4.0f);
        else if (k == "faradayVisible") impl->faradayVisible = clB(v);
        else if (k == "faradayMode") impl->faradayMode = glm::clamp((int)v, 0, 1);
        else if (k == "faradayScale") impl->faradayScale = glm::clamp((float)v, 0.2f, 4.0f);
        else if (k == "auroraVisible") impl->auroraVisible = clB(v);
        else if (k == "tecVisible") impl->tecVisible = clB(v);
        else if (k == "sunVisible") impl->sunVisible = clB(v);   // M11
        else if (k == "sunProduct") impl->sunProduct = glm::clamp((int)v, 0, 1);   // M11
        else if (k == "sunWinX") impl->sunGeom.x = glm::clamp((float)v, -100.0f, 3840.0f);   // M11
        else if (k == "sunWinY") impl->sunGeom.y = glm::clamp((float)v, 0.0f, 2160.0f);   // M11
        else if (k == "sunWinW") impl->sunGeom.w = glm::clamp((float)v, 200.0f, 700.0f);   // M11
        else if (k == "sunWinH") impl->sunGeom.h = glm::clamp((float)v, 200.0f, 700.0f);   // M11
        else if (k == "tecCacheEnabled") impl->tecCacheEnabled = clB(v);   // M10
        else if (k == "tecCacheMaxAgeH") impl->tecCacheMaxAgeH = glm::clamp((double)v, 1.0, 168.0);   // M10
        else if (k == "giroStationsVisible") impl->giroStationsVisible = clB(v);
        else if (k == "cineSpeed") impl->cineSpeed = glm::clamp((float)v, 0.005f, 0.30f);
        else if (k == "replaySpeed") impl->replaySpeed = glm::clamp((float)v, 0.5f, 8.0f);
        else if (k == "volumeVisible") impl->volumeVisible = clB(v);
        else if (k == "pipVisible") impl->pipVisible = clB(v);
        else if (k == "volumeMode") impl->volumeMode = glm::clamp((int)v, 0, 1);
        else if (k == "isoBands") impl->isoBands = clB(v);
        else if (k == "sliceSrc") impl->sliceSrc = glm::clamp((int)v, 0, 2);
        else if (k == "sliceLatA") impl->sliceLatA = glm::clamp((float)v, -90.0f, 90.0f);
        else if (k == "sliceLonA") impl->sliceLonA = glm::clamp((float)v, -180.0f, 180.0f);
        else if (k == "sliceLatB") impl->sliceLatB = glm::clamp((float)v, -90.0f, 90.0f);
        else if (k == "sliceLonB") impl->sliceLonB = glm::clamp((float)v, -180.0f, 180.0f);
        else if (k == "labelsVisible") impl->labelsVisible = clB(v);
        else if (k == "explodeFactor") impl->explodeFactor = glm::clamp((float)v, 1.0f, 5.0f);
        else if (k == "peakShellVisible") impl->peakShellVisible = clB(v);
        else if (k == "tecColVisible") impl->tecColVisible = clB(v);
        else if (k == "volumeOpacity") impl->volumeOpacity = glm::clamp((float)v, 0.0f, 2.5f);
        else if (k == "sliceOn") impl->sliceOn = clB(v);
        else if (k == "camRX") impl->camera.rotateX = glm::clamp((float)v, -1.5f, 1.5f);
        else if (k == "camRY") impl->camera.rotateY = (float)v;
        else if (k == "camDist") impl->camera.distance = glm::clamp((float)v, 1.5f, 20.0f);
        else if (k == "hfTraceVisible") impl->hfTraceVisible = clB(v);   // M8 (plan 11.4)
        else if (k == "hfFreqMHz") impl->hfFreqMHz = glm::clamp((float)v, 1.0f, 30.0f);
    }
    if (legacyBloom) {
        // D1 (v2.1, correccion tras la auditoria M1 de MUSE): la intensidad
        // EFECTIVA vieja era bloomIntensity^2 (u_intensity se aplicaba en H
        // y en V); el pipeline nuevo la aplica UNA vez (u_pass==1), luego
        // conservar el look exige I' = I^2 (NO sqrt: la v2 del plan tenia
        // la formula invertida y dejaba ~+74% de glow). El proximo save ya
        // escribe ionolive2 y la migracion no vuelve a correr (u_pass).
        float legacyI = glm::clamp(impl->bloomIntensity, 0.0f, 3.0f);
        impl->bloomIntensity = glm::clamp(legacyI * legacyI, 0.0f, 3.0f);
        printf("[App] settings v1 -> v2: bloomIntensity migrada (I^2) = %.4f\n",
               (double)impl->bloomIntensity);
    }
    // Release-prep: migracion del default viejo 0.35 -> 0.72 (solo el valor
    // exacto +-0.001; gustos preservados). El proximo save escribe 0.72.
    if (std::fabs(impl->bloomThreshold - 0.35f) <= 0.001f) {
        impl->bloomThreshold = 0.72f;
        printf("[App] settings: bloomThreshold 0.35 -> 0.72 (default M9e)\n");
    }
    fclose(f);
    printf("[App] settings.cfg loaded\n");
}

float App::currentKp() const {
    auto idx = impl->getIndices();
    if (idx && idx->snap.valid) return idx->snap.kp;
    return 2.0f;
}

// Hilo worker: toda la red (NOAA/GloTEC/Ovation) con instancias LOCALES.
// Jamas toca OpenGL; publica bundles inmutables que el hilo principal aplica.
void App::fetchWorker() {
    GloTecAdapter glo;
    glo.setGridSize(72, 72);
    SolarIndicesAdapter sidx;
    SolarWindAdapter sw;
    AuroraAdapter au;
    au.setGridSize(72, 72);
    RadioPropagationAdapter rp;
    XrayAdapter xr;
    SdoImageAdapter sdo;   // M11: disco solar (worker-local, patron xr)
    EsaAdapter esa;   // solo mock offline para el TEC
    bool haveTec = false;
    bool haveXray = false;
    double sdoPubT[2] = {-1.0, -1.0};  // M11: ultimo publicado por producto
    int sdoLastWant = -1;  // M11-fix: fuerza republicar al alternar producto
    sdo.init();  // M12/N8
    sdo.setDiskCacheDir("data/cache");  // M12/N3
    // M12/N3: restore instantaneo; el fetch refresca detras (stale-while-revalidate).
    if (sdo.restoreDisk()) {
        for (int p = 0; p < 2; p++) {
            const SdoImage& rim = sdo.getImage((SdoProduct)p);
                if (rim.valid) {
                    sdoPubT[p] = rim.timeUtc;
                    auto rb = std::make_shared<Impl::SdoBundle>();
                    rb->img = rim;
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->sdoB = rb; }
                    // M-prov-matrix: fila SDO (restore).
                    provstatus::Snapshot ss;
                    ss.id = provstatus::PROV_SDO;
                    ss.hasData = rim.valid;
                    ss.dataEpoch = rim.timeUtc;
                    ss.data = SdoImageAdapter::nameFor((SdoProduct)p);
                    ss.error = sdo.getErrorMessage();
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->provBoard[provstatus::PROV_SDO] = ss; }
                }
        }
    }
    bool firstPass = true;

    auto noteDone = [&]() {
        if (firstPass) impl->loadDone.store(impl->loadDone.load() + 1);
    };

    double lastDataPassUtc = -1e18;  // M12/N1: ultimo pase de datos completo
    // M12: carril SDO (N1/D2/D4/N5 + fix2). Publica cambios; silent en steady.
    auto runSdo = [&](double ep) {
        sdo.setVisible(impl->sunVisible);
        const SdoProduct want = impl->sunProduct == 1 ? SdoProduct::A304 : SdoProduct::HMIIF;
        const SdoProduct other = impl->sunProduct == 1 ? SdoProduct::HMIIF : SdoProduct::A304;
        const bool switched = (int)want != sdoLastWant;
        sdoLastWant = (int)want;
        if (switched) {
            // N5: linea de conmutacion (el hit es silencioso por diseno).
            const double ageW = ep - sdo.getLastFetchTime(want);
            printf("[Sdo] switch -> %s (cache %.0f s)%s\n", SdoImageAdapter::nameFor(want), ageW,
                   (ageW < 900.0) ? "" : " (stale, refrescando...)");
        }
        const SdoProduct seq[2] = {want, other};
        for (int i = 0; i < 2; i++) {
            // D4: el oculto solo se calienta si caduco (<15 min = skip).
            if (i == 1 && sdo.getImage(seq[1]).valid &&
                ep - sdo.getLastFetchTime(seq[1]) < 900.0)
                continue;
            if (sdo.fetch(seq[i], ep)) {
                const SdoImage& im = sdo.getImage(seq[i]);
                if (im.valid && (im.timeUtc != sdoPubT[(int)seq[i]] ||
                                 (switched && seq[i] == want))) {
                    sdoPubT[(int)seq[i]] = im.timeUtc;
                    auto b = std::make_shared<Impl::SdoBundle>();
                    b->img = im;
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->sdoB = b; }
                    // M-prov-matrix: fila SDO (fetch).
                    provstatus::Snapshot ss;
                    ss.id = provstatus::PROV_SDO;
                    ss.hasData = im.valid;
                    ss.dataEpoch = im.timeUtc;
                    ss.data = SdoImageAdapter::nameFor(seq[i]);
                    ss.error = sdo.getErrorMessage();
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->provBoard[provstatus::PROV_SDO] = ss; }
                }
            }
        }
    };
    while (!impl->shutdownRequested.load()) {
        double epoch = (double)std::time(nullptr);
        const bool kicked = impl->sdoKick.exchange(false);  // M12/N1
        // N1: kick con datos frescos (<60 s) salta 1-6; primer pase siempre full.
        const bool sdoLane = kicked && !firstPass && (epoch - lastDataPassUtc < 60.0);
        if (firstPass) runSdo(epoch);   // D2: sol primero al arrancar
        if (!sdoLane) {
            // 1) TEC GloTEC primero (es la capa principal; asi aparece en segundos).
            //    Si falla y no hay nada todavia, mock Esa offline marcado simulated.
            {
                GridData g;
                bool ok = glo.fetchTEC(epoch);
                bool sim = false;
                if (ok) {
                    g = glo.getGrid();
                } else if (!haveTec && !impl->cacheLoaded.load()) {  // M10: con cache real, nada de mock
                    esa.fetchTEC(epoch);
                    g.width = esa.getData().width;
                    g.height = esa.getData().height;
                    g.values = esa.getData().values;
                    g.timestamp = epoch;
                    g.valid = esa.getData().valid;
                    sim = true;
                    // M-prov-matrix: fila ESA (mock offline por diseno).
                    provstatus::Snapshot se;
                    se.id = provstatus::PROV_ESA;
                    se.hasData = esa.getData().valid;
                    se.dataEpoch = esa.getData().valid ? esa.getData().timestamp : 0.0;
                    se.data = esa.getData().valid ? "disk cache" : "mock (offline)";
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->provBoard[provstatus::PROV_ESA] = se; }
                }
                if (g.valid) {
                    auto b = std::make_shared<Impl::TecBundle>();
                    b->grid = g;
                    b->grid.timestamp = epoch;
                    b->simulated = sim;
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->tec = b; }
                    // M-prov-matrix: fila GloTEC (solo lectura del fetch ya hecho).
                    provstatus::Snapshot st;
                    st.id = provstatus::PROV_TEC;
                    st.hasData = g.valid && !sim;
                    st.dataEpoch = epoch;
                    char sdb[32];
                    std::snprintf(sdb, sizeof(sdb), "%dx%d", g.width, g.height);
                    st.data = sdb;
                    st.error = glo.getErrorMessage();
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->provBoard[provstatus::PROV_TEC] = st; }
                    haveTec = true;
                    if (!sim) impl->liveTecArrived.store(true);
                }
            }
            noteDone();

            // 2) Indices (Kp/Dst/F10.7/Ap/SSN).
            if (sidx.fetch(epoch) && sidx.getSnapshot().valid) {
                auto b = std::make_shared<Impl::IndicesBundle>();
                b->snap = sidx.getSnapshot();
                b->kp = sidx.getKpHistory();
                b->dst = sidx.getDstHistory();
                b->f107 = sidx.getF107History();
                b->ssn = sidx.getSsnHistory();
                b->spots = sidx.getSunspots();  // M11: manchas viajan con los indices
                { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->indices = b; }
                // M-prov-matrix: fila indices (Kp/Dst/F10.7/Ap/SSN).
                provstatus::Snapshot si;
                si.id = provstatus::PROV_INDICES;
                si.hasData = b->snap.valid;
                si.dataEpoch = b->snap.timeUtc;
                si.data = b->snap.simulated ? ("mock: " + b->snap.error) : "5/5 real";
                si.error = b->snap.error;
                { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->provBoard[provstatus::PROV_INDICES] = si; }
            }
            noteDone();

            // 3) Viento solar.
            if (sw.fetch(epoch) && sw.getLatest().valid) {
                auto b = std::make_shared<Impl::WindBundle>();
                b->latest = sw.getLatest();
                b->bzHist = sw.getBzHistory();
                b->simulated = sw.isSimulated();
                b->error = sw.getErrorMessage();
                { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->wind = b; }
                // M-prov-matrix: fila viento solar.
                provstatus::Snapshot sws;
                sws.id = provstatus::PROV_WIND;
                sws.hasData = b->latest.valid;
                sws.dataEpoch = b->latest.timeUtc;
                sws.data = b->simulated ? "simulated" : "live";
                sws.error = b->error;
                { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->provBoard[provstatus::PROV_WIND] = sws; }
            }
            noteDone();

            // 4) Aurora Ovation.
            {
                GridData g;
                std::string err;
                if (au.fetchAurora(epoch)) {
                    g = au.getGrid();
                } else {
                    err = au.getErrorMessage();
                }
                if (g.valid || !err.empty()) {
                    auto b = std::make_shared<Impl::AuroraBundle>();
                    b->grid = g;
                    b->error = err;
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->auroraB = b; }
                    // M-prov-matrix: fila aurora Ovation.
                    provstatus::Snapshot sa;
                    sa.id = provstatus::PROV_AURORA;
                    sa.hasData = g.valid;
                    sa.dataEpoch = g.valid ? g.timestamp : 0.0;
                    if (g.valid) {
                        char sab[32];
                        std::snprintf(sab, sizeof(sab), "%dx%d", g.width, g.height);
                        sa.data = sab;
                    }
                    sa.error = err;
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->provBoard[provstatus::PROV_AURORA] = sa; }
                }
            }
            noteDone();

            // 5) Propagacion HF (usa muestras GIRO thread-safe).
            {
                // M4R-A: en kc2g-fallback la mediana MUF/foF2 del panel HF viene
                // del batch kc2g (panel window <=24 h); si no, GIRO como siempre.
                auto samples = (impl->ionSourceMode.load() == Impl::SRC_KC2G_FALLBACK)
                    ? impl->kc2g.copySamples()
                    : impl->giro.copySamples();
                // M7 (plan 10): el LUF global reacciona al flare X-ray. rp.fetch
                // corre ANTES que xr.fetch() del mismo pase, asi que recibe el
                // flujo del bundle del pase anterior (xr persiste entre pases de
                // 300 s; stale <= 5 min, irrelevante para fulguraciones — sello).
                float xrFlux = (xr.getLatest().valid ? xr.getLatest().flux : 0.0f);
                rp.fetch(epoch, samples, xrFlux);
                if (rp.getLatest().valid && (rp.usedGiroReal() || !impl->getRadio())) {
                    auto b = std::make_shared<Impl::RadioBundle>();
                    b->latest = rp.getLatest();
                    b->aeHist = rp.getAeHistory();
                    b->s4Hist = rp.getS4History();
                    b->simulated = rp.isSimulated();
                    b->usedGiro = rp.usedGiroReal();
                    b->error = rp.getErrorMessage();
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->radio = b; }
                    // M-prov-matrix: fila Kp/HF.
                    provstatus::Snapshot sr;
                    sr.id = provstatus::PROV_KPHF;
                    sr.hasData = b->latest.valid;
                    sr.dataEpoch = b->latest.timeUtc;
                    sr.data = b->usedGiro ? "GIRO real" : "model";
                    sr.error = b->error;
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->provBoard[provstatus::PROV_KPHF] = sr; }
                }
            }
            noteDone();

            // 6) Rayos X GOES (capa D / fulguraciones).
            {
                bool real = xr.fetch(epoch);
                if (xr.getLatest().valid && (real || !haveXray)) {
                    auto b = std::make_shared<Impl::XrayBundle>();
                    b->latest = xr.getLatest();
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->xray = b; }
                    // M-prov-matrix: fila GOES X-ray.
                    provstatus::Snapshot sx;
                    sx.id = provstatus::PROV_XRAY;
                    sx.hasData = b->latest.valid;
                    sx.dataEpoch = b->latest.timeUtc;
                    sx.data = "GOES";
                    sx.error = xr.getErrorMessage();
                    { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->provBoard[provstatus::PROV_XRAY] = sx; }
                    haveXray = true;
                }
            }
            noteDone();

            lastDataPassUtc = epoch;
            if (!firstPass) runSdo(epoch);   // regimen: tras datos
        } else {
            runSdo(epoch);                   // N1: solo SDO tras kick fresco
        }
        noteDone();

        if (firstPass) {
            firstPass = false;
            impl->initialLoadDone.store(true);
            printf("[App] Initial fetch pass done\n");
        }

        // Espera interrumpible 300 s con granularidad 100 ms (D3: kick en <=0.1 s).
        for (int i = 0; i < 3000 && !impl->shutdownRequested.load() && !impl->sdoKick.load(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // (el flag se consume al arrancar el pase: exchange en cabecera)
    }
}

// Aplica en el hilo principal los bundles frescos del worker (llamadas GL
// y UI van aqui; el worker nunca toca OpenGL).
void App::consumeBundles() {
    auto t = impl->getTec();
    if (t && t != impl->lastTec) {
        impl->lastTec = t;
        if (t->grid.valid) {
            pushHistoryFrame(t->grid, t->grid.timestamp, t->simulated);
            if (!impl->replayMode) updateMainGrid(t->grid);
        }
    }
    auto ix = impl->getIndices();
    if (ix && ix != impl->lastIndices) {
        impl->lastIndices = ix;
        applyRelief();   // Kp nuevo -> storm swell actualizado
    }
    auto w = impl->getWind();
    if (w && w != impl->lastWind) {
        impl->lastWind = w;
        if (w->latest.valid)
            impl->solarWindLayer.updateArrowData(w->latest.speed, w->latest.bz,
                                                 impl->sunDirection, impl->swArrowMode);
    }
    auto au = impl->getAurora();
    if (au && au != impl->lastAurora) {
        impl->lastAurora = au;
        if (au->grid.valid) impl->auroraLayer.updateFromGrid(au->grid);
    }
    auto rp = impl->getRadio();
    if (rp && rp != impl->lastRadio) {
        impl->lastRadio = rp;
    }
    auto xr = impl->getXray();
    if (xr && xr != impl->lastXray) {
        impl->lastXray = xr;
    }
    auto sd = impl->getSdo();
    if (sd && sd != impl->lastSdo) {
        impl->lastSdo = sd;
        const int p = sd->img.product;
        if (p >= 0 && p <= 1 && sd->img.timeUtc != impl->sunUpTime[p])
            uploadSunTexture(sd->img);  // D1: ambas texturas (toggle = 1 frame)
    }
}

// Cortina vertical muestreada de lastVolume. Fuentes:
//   0 = A pineada -> B hover (estaciones); 1 = A/B manual; 2 = punto fijo ±2°.
void App::rebuildSlice(bool force) {
    glm::vec3 da(0), db(0);
    std::string na, nb;
    char coord[48];
    if (impl->sliceSrc == 1) {
        da = densityDir(impl->sliceLatA, impl->sliceLonA);
        db = densityDir(impl->sliceLatB, impl->sliceLonB);
        snprintf(coord, sizeof(coord), "%.1f,%.0f", (double)impl->sliceLatA, (double)impl->sliceLonA);
        na = coord;
        snprintf(coord, sizeof(coord), "%.1f,%.0f", (double)impl->sliceLatB, (double)impl->sliceLonB);
        nb = coord;
    } else if (impl->sliceSrc == 2) {
        da = densityDir(impl->sliceLatA, impl->sliceLonA - 2.0f);
        db = densityDir(impl->sliceLatA, impl->sliceLonA + 2.0f);
        snprintf(coord, sizeof(coord), "%.1f,%.0f", (double)impl->sliceLatA, (double)impl->sliceLonA);
        na = nb = coord;
    } else {
        const StationPoint* A = nullptr;
        const StationPoint* B = nullptr;
        int n = impl->ionosondeLayer.getStationCount();
        if (!impl->pinnedCode.empty()) {
            for (int i = 0; i < n; i++) {
                const StationPoint* c = impl->ionosondeLayer.getSample(i);
                if (c && c->code == impl->pinnedCode) { A = c; break; }
            }
        }
        if (impl->hoveredStation >= 0 && impl->hoveredStation < n)
            B = impl->ionosondeLayer.getSample(impl->hoveredStation);
        if (!A || !B || A->code == B->code) return;
        if (!force && A->code == impl->sliceA && B->code == impl->sliceB) return;
        na = A->code; nb = B->code;
        da = densityDir(A->latDeg, A->lonDeg);
        db = densityDir(B->latDeg, B->lonDeg);
    }
    if (!impl->lastVolume.valid) return;
    if (!force && na == impl->sliceA && nb == impl->sliceB) return;
    impl->sliceA = na;
    impl->sliceB = nb;
    std::vector<float> vals((size_t)SliceLayer::SEG * SliceLayer::LVL);
    float peak = 0.0f;
    float dot = glm::clamp(glm::dot(da, db), -1.0f, 1.0f);
    float omega = acosf(dot);
    bool useSlerp = omega > 1e-4f && omega < 3.14159265f - 1e-4f;
    const float kPi = 3.14159265f;
    const float sAltMin = impl->lastVolume.altMinKm;
    const float sAltMax = impl->lastVolume.altMaxKm;
    for (int j = 0; j < SliceLayer::LVL; j++) {
        float f = (float)j / (float)(SliceLayer::LVL - 1);
        float altKm = sAltMin + (sAltMax - sAltMin) * powf(f, 1.5f);
        for (int i = 0; i < SliceLayer::SEG; i++) {
            float s = (float)i / (float)(SliceLayer::SEG - 1);
            glm::vec3 dir = useSlerp
                ? (da * sinf((1.0f - s) * omega) + db * sinf(s * omega)) / sinf(omega)
                : glm::normalize(da * (1.0f - s) + db * s);
            float lat = asinf(glm::clamp(dir.y, -1.0f, 1.0f)) * 180.0f / kPi;
            float lon = (kPi - atan2f(dir.z, dir.x)) * 180.0f / kPi;
            if (lon > 180.0f) lon -= 360.0f;
            if (lon < -180.0f) lon += 360.0f;
            float v = sampleVolume(impl->lastVolume, lat, lon, altKm);
            vals[(size_t)j * SliceLayer::SEG + i] = v;
            if (v > peak) peak = v;
        }
    }
    impl->sliceLayer.updateSlice(da, db, vals, "viridis", sAltMin, sAltMax);
    impl->slicePeakLog = 8.0f + 4.5f * peak;  // misma normalizacion que el volumen
}

// M10: restaura anillo + bundle actual desde cache/ (init, pre-worker).
// Solo dato real (el writer jamas cachea simulated); >maxAge puebla
// replay pero no current. Badge "cache" hasta que llegue live.
void App::restoreTecCache() {
    if (!impl->tecCacheEnabled) return;
    teccache::ScanResult scan = teccache::loadCached("cache");
    if (scan.frames.empty()) return;
    {
        std::lock_guard<std::mutex> lk(impl->tecHistMutex);
        for (const auto& f : scan.frames) {
            GridData g;
            g.width = f.w;
            g.height = f.h;
            g.values = f.v;
            g.timestamp = f.epoch;
            g.valid = true;
            impl->tecHist.push_back(g);  // scan ordenado asc
        }
        while (impl->tecHist.size() > 432) impl->tecHist.erase(impl->tecHist.begin());   // TEC-ext 034: cap 432 = 72 h @ 10 min
    }
    const double newest = scan.frames.back().epoch;
    const double now = (double)std::time(nullptr);
    impl->cacheNewestEpoch.store(newest);
    impl->cacheRestored.store((int)scan.frames.size());
    impl->cacheLoaded.store(true);
    if (teccache::isFresh(newest, now, impl->tecCacheMaxAgeH)) {
        auto b = std::make_shared<Impl::TecBundle>();
        b->grid.width = scan.frames.back().w;
        b->grid.height = scan.frames.back().h;
        b->grid.values = scan.frames.back().v;
        b->grid.timestamp = newest;
        b->grid.valid = true;
        b->simulated = false;
        b->fromCache = true;
        { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->tec = b; }
    }
    char cbuf[16] = "--:--";
    { std::time_t tt = (std::time_t)newest; std::tm gg;
#ifdef _WIN32
      gmtime_s(&gg, &tt);
#else
      gmtime_r(&tt, &gg);
#endif
      strftime(cbuf, sizeof(cbuf), "%H:%M", &gg); }
    printf("[App] TEC cache: %d restored, newest %s UTC (pruned %d, dropped %d)\n",
           (int)scan.frames.size(), cbuf, scan.pruned, scan.dropped);
}

// FASE B (P2): restaura historia kc2g desde cache/ (init, pre-worker).
// El barrido KCC1 re-acumula por epoch ascendente (dedup: idempotente).
// Sin cache: freeze honesto FASE A (primera sesion de fallback sin historia).
void App::restoreKc2gCache() {
    const double now = (double)std::time(nullptr);
    const int n = impl->kc2g.restoreFromCache("cache", now);
    if (n <= 0) return;
    int nSt = 0;
    double t0 = 0.0, t1 = 0.0;
    if (!impl->kc2g.historyStats(nSt, t0, t1) || nSt <= 0) return;
    char c0[16] = "--:--", c1[16] = "--:--";
    { std::time_t tt = (std::time_t)t0; std::tm gg;
#ifdef _WIN32
      gmtime_s(&gg, &tt);
#else
      gmtime_r(&tt, &gg);
#endif
      strftime(c0, sizeof(c0), "%H:%M", &gg); }
    { std::time_t tt = (std::time_t)t1; std::tm gg;
#ifdef _WIN32
      gmtime_s(&gg, &tt);
#else
      gmtime_r(&tt, &gg);
#endif
      strftime(c1, sizeof(c1), "%H:%M", &gg); }
    printf("[kc2g] cache: %d batches restored, %d st con historia, rango [%s..%s UTC]\n",
           n, nSt, c0, c1);
}

void App::pushHistoryFrame(const GridData& grid, double ts, bool simulated) {    if (!grid.valid || grid.values.empty()) return;
    std::lock_guard<std::mutex> lk(impl->tecHistMutex);
    for (const auto& e : impl->tecHist) {
        if (fabs(e.timestamp - ts) < 240.0) return;  // dedup ~4 min
    }
    GridData fr = grid;
    fr.timestamp = ts;
    size_t pos = 0;
    while (pos < impl->tecHist.size() && impl->tecHist[pos].timestamp < ts) pos++;
    impl->tecHist.insert(impl->tecHist.begin() + pos, fr);
    while (impl->tecHist.size() > 432) impl->tecHist.erase(impl->tecHist.begin());   // TEC-ext 034: cap 432 = 72 h @ 10 min
    // M10: todo frame real que entra al anillo persiste (simulated/ESA excluidos).
    if (teccache::shouldCacheFrame(grid.valid, simulated, impl->tecCacheEnabled)) {
        teccache::Frame f;
        f.epoch = ts;
        f.w = grid.width;
        f.h = grid.height;
        f.v = grid.values;
        teccache::saveFrame("cache", f);
    }
}

// M11: subida GL de la imagen SDO (render thread, patron Earth.cpp:14-31
// adaptado: GL_RGB8 — JPG sin alpha — LINEAR + CLAMP, sin mipmaps).
void App::uploadSunTexture(const SdoImage& im) {
    const int p = im.product;
    if (p < 0 || p > 1 || im.rgb.empty() || im.w <= 0 || im.h <= 0) return;
    if (impl->sunTex[p] == 0) glGenTextures(1, &impl->sunTex[p]);
    glBindTexture(GL_TEXTURE_2D, impl->sunTex[p]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, im.w, im.h, 0, GL_RGB, GL_UNSIGNED_BYTE, im.rgb.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    impl->sunTexW[p] = im.w;
    impl->sunTexH[p] = im.h;
    impl->sunImgTime[p] = im.timeUtc;
}

// M11: ventana Sun (disco SDO + ARs). Primera imagen en la UI del motor.
void App::drawSunPanel() {
    if (!impl->sunVisible) return;
    SunPanelInput in;
    const int dp = impl->sunProduct >= 0 && impl->sunProduct <= 1 ? impl->sunProduct : 0;
    in.texId = impl->sunTex[dp];
    auto ix = impl->getIndices();
    in.spots = ix ? &ix->spots : nullptr;
    in.utc = (double)std::time(nullptr);
    in.pProduct = &impl->sunProduct;
    in.pKick = &impl->sdoKick;
    in.pVisible = &impl->sunVisible;
    const bool a304 = impl->sunProduct == 1;
    in.prodName = a304 ? "SDO - AIA 304 (cromosfera)" : "SDO - HMI (fotosfera)";
    in.prodTag = a304 ? "A304" : "HMIIF";
    char tbuf[16] = "--:--";
    if (impl->sunImgTime[dp] > 0.0) {
        std::time_t tt = (std::time_t)impl->sunImgTime[dp];
        std::tm gg;
#ifdef _WIN32
        gmtime_s(&gg, &tt);
#else
        gmtime_r(&tt, &gg);
#endif
        strftime(tbuf, sizeof(tbuf), "%H:%M", &gg);
    }
    in.imgTime = tbuf;
    char xbuf[16] = "--";
    auto xB = impl->getXray();
    if (xB && xB->latest.valid)
        snprintf(xbuf, sizeof(xbuf), "%c%.1f", xB->latest.fclass, (double)xB->latest.fsub);
    in.xrayText = xbuf;
    impl->sunPanel.draw(in, impl->sunGeom);
}

void App::applyColorLayer() {    // 0 = TEC (GloTEC real). 1-4 = GIRO. 5-7 = derivados Radio. 8-9 = IRTAM retrospectivo (P4: sin dato no se dibuja).
    // Si hay override manual se respeta; si no, automatico por variable.
    static const char* CM_NAMES[4] = { "viridis", "inferno", "plasma", "turbo" };
    static const int VAR_AUTO_CM[12] = { 0, 2, 1, 3, 3, 2, 1, 3, 2, 3, 1, 2 };  // ... + IRTAM foF2=plasma, hmF2=turbo (espejo GIRO) + IRTAM B0=inferno, B1=plasma

    // M4R-A: fuente viva por modo (kc2g-fallback -> fusion cacheada;
    // misma cache/estampado que usa update() — lambda dentro del metodo
    // porque App::Impl es private para funciones libres).
    const std::vector<StationSample>& gsSrc = [&]() -> const std::vector<StationSample>& {
        if (impl->ionSourceMode.load() != Impl::SRC_KC2G_FALLBACK)
            return impl->giro.getSamples();
        Kc2gAdapter::Kc2gStatus ks = impl->kc2g.getStatus();
        if (ks.lastOkUtc != impl->kc2gMergedStamp) {
            impl->kc2gMerged = mergeKc2g(impl->giro.getSamples(), impl->kc2g.copySamples());
            impl->kc2gMergedStamp = ks.lastOkUtc;
        }
        return impl->kc2gMerged;
    }();

    if (impl->mapVariable == 0) {
        impl->tecLayer.setDisplaceCentered(false);
        impl->shellLayer.setDisplaceCentered(false);
        impl->tecLayer.clearRange();
        if (auto t = impl->getTec()) {
            if (t->grid.valid) updateMainGrid(t->grid);
        }
    } else if (impl->mapVariable >= 1 && impl->mapVariable <= 4) {
        int var = impl->mapVariable - 1;   // 0=foF2,1=MUF,2=hmF2,3=NmF2
        GridData g = giroInterpGrid(gsSrc, var, 72, 72);   // M4R-A
        if (!g.valid) return;
        bool centered = (var == 2);   // hmF2: relieve de alturas reales
        impl->tecLayer.setDisplaceCentered(centered);
        impl->shellLayer.setDisplaceCentered(centered);
        impl->tecLayer.clearRange();
        updateMainGrid(g);
    } else if (impl->mapVariable >= 5 && impl->mapVariable <= 7) {
        impl->tecLayer.setDisplaceCentered(false);
        impl->shellLayer.setDisplaceCentered(false);
        float kpProxy = currentKp();
        double epoch = (impl->replayMode && impl->replayEpoch > 0.0) ? impl->replayEpoch
                       : (double)std::time(nullptr);  // derive*() espera Unix UTC
        int var = impl->mapVariable - 5;   // 0=LUF,1=FOT,2=Spread-F
        // M7 (plan 10): flujo X-ray del ultimo bundle para el termino SID.
        float xrFlux = (impl->lastXray && impl->lastXray->latest.valid)
            ? impl->lastXray->latest.flux : 0.0f;
        GridData g = radioInterpGrid(gsSrc, var, 72, 72, epoch, kpProxy, xrFlux);   // M4R-A
        if (!g.valid) return;
        if (var == 2) impl->tecLayer.setRange(0.0f, 100.0f);  // Spread-F % fijo
        else impl->tecLayer.clearRange();
        updateMainGrid(g);
        if (var != 2) { /* rango auto para LUF/FOT */ }
    } else if (impl->mapVariable == 8 || impl->mapVariable == 9) {
        // M-IRTAM-F2 (P4, F2-R2 cond. 1): el draw path EXIGE valid. P4 publica
        // valid=false siempre -> la capa esta AUSENTE (return sin dibujar).
        // P1b rellenara grids 46x45 + valid=true (flip aditivo).
        // M-irtam-replay-A: en replay se dibuja la muestra al epoch.
        // M-irtam-replay-024 (ruling R2 PERF): consume del worker, jamas
        // muestrea en caliente (render-path wait-free).
        if (impl->replayMode && impl->replayEpoch > 0.0) {
            IrtamBracketView v;
            if (!consumeIrtamBracket(0, v)) return;
            const std::vector<float>& v0 =
                (impl->mapVariable == 8) ? v.a0 : v.b0;
            const std::vector<float>& v1 =
                (impl->mapVariable == 8) ? v.a1 : v.b1;
            GridData g = lerpBracketGrid(v0, v1, v.t0, v.t1,
                                         impl->replayEpoch);
            if (!g.valid) return;
            const bool centered = (impl->mapVariable == 9);
            impl->tecLayer.setDisplaceCentered(centered);
            impl->shellLayer.setDisplaceCentered(centered);
            impl->tecLayer.clearRange();
            updateMainGrid(g);
            return;
        }
        auto b = impl->getIrtam();
        if (!b || !b->valid) return;
        const bool centered = (impl->mapVariable == 9);  // IRTAM hmF2: relieve real
        impl->tecLayer.setDisplaceCentered(centered);
        impl->shellLayer.setDisplaceCentered(centered);
        impl->tecLayer.clearRange();
        updateMainGrid(impl->mapVariable == 8 ? b->foF2 : b->hmF2);
    } else if (impl->mapVariable == 10 || impl->mapVariable == 11) {
        // F2-B0B1-R1 Q3: capas B0/B1 (valid lo gobierna F/H (Q1); el par
        // propio ausente = capa ausente, jamas ceros). B0: clamp a 0 SOLO
        // en color (dato crudo intacto; negativos = fisica real nocturna).
        // M-irtam-replay-A: en replay, muestra al epoch.
        // M-irtam-replay-024 (ruling R2 PERF): consume del worker, jamas
        // muestrea en caliente (render-path wait-free).
        if (impl->replayMode && impl->replayEpoch > 0.0) {
            IrtamBracketView v;
            if (!consumeIrtamBracket(1, v)) return;
            const std::vector<float>& v0 =
                (impl->mapVariable == 10) ? v.a0 : v.b0;
            const std::vector<float>& v1 =
                (impl->mapVariable == 10) ? v.a1 : v.b1;
            GridData g = lerpBracketGrid(v0, v1, v.t0, v.t1,
                                         impl->replayEpoch);
            if (!g.valid) return;
            impl->tecLayer.setDisplaceCentered(false);
            impl->shellLayer.setDisplaceCentered(false);
            impl->tecLayer.clearRange();
            if (impl->mapVariable == 10) {
                for (float& vv : g.values)
                    if (vv < 0.0f) vv = 0.0f;
            }
            updateMainGrid(g);
            return;
        }
        auto b = impl->getIrtam();
        if (!b || !b->valid) return;
        const GridData& src =
            (impl->mapVariable == 10) ? b->b0 : b->b1;
        if (src.values.empty() || !src.valid) return;
        impl->tecLayer.setDisplaceCentered(false);
        impl->shellLayer.setDisplaceCentered(false);
        impl->tecLayer.clearRange();
        if (impl->mapVariable == 10) {
            GridData g = src;  // copia display-only para el clamp
            for (float& v : g.values)
                if (v < 0.0f) v = 0.0f;
            updateMainGrid(g);
        } else {
            updateMainGrid(src);  // B1 lineal, unidad "[]"
        }
    } else {
        return;
    }

    if (impl->colormapOverride) {
        impl->tecLayer.setColormap(CM_NAMES[impl->manualColormap & 3]);
    } else {
        int cm = VAR_AUTO_CM[impl->mapVariable];
        impl->tecLayer.setColormap(CM_NAMES[cm]);
        impl->manualColormap = cm;   // resincronizar el desplegable con el automatico
    }

    // Sincronizar el override de colormap tambien a los puntos GIRO (IonosondeLayer):
    // override manual en el desplegable "Colormap" sobreescribe la metric de los puntos;
    // en auto, los puntos usan el colormap asociado a su metric.
    if (impl->colormapOverride) {
        impl->ionosondeLayer.setPointColormapOverride(CM_NAMES[impl->manualColormap & 3]);
    } else {
        impl->ionosondeLayer.setPointColormapOverride("");
    }
}

void App::render() {
    // --- Pass 1: Render scene to FBO ---
    impl->sceneFBO.bind();
    glClearColor(0.01f, 0.01f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view = impl->camera.getViewMatrix();
    glm::mat4 proj = glm::perspective(
        glm::radians(impl->camera.fov),
        impl->camera.aspect,
        impl->camera.nearP,
        impl->camera.farP
    );

    // Stars (first, behind everything)
    if (impl->showStars) {
        glDepthMask(GL_FALSE);
        impl->starShader.use();
        impl->starShader.setMat4("u_view", view);
        impl->starShader.setMat4("u_projection", proj);
        impl->starShader.setFloat("u_time", impl->time);
        impl->stars.render();
        glDepthMask(GL_TRUE);
    }

    // Earth
    glm::mat4 model = glm::mat4(1.0f);
    // Posicion real de la camara (Camera::position no se actualiza al orbitar;
    // se extrae de la matriz de vista para el especular/fresnel).
    glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
    if (impl->showEarth) {
        impl->earthShader.use();
        impl->earthShader.setMat4("u_model", model);
        impl->earthShader.setMat4("u_view", view);
        impl->earthShader.setMat4("u_projection", proj);
        impl->earthShader.setVec3("u_sunDir", impl->sunDirection);
        impl->earthShader.setVec3("u_cameraPos", camPos);
        impl->earthShader.setFloat("u_night", impl->nightLevel);
        impl->earthShader.setFloat("u_time", impl->time);
        impl->earthShader.setFloat("u_cloudAmt", impl->cloudAmt);
        impl->earthShader.setInt("u_texture", 0);
        impl->earthShader.setInt("u_nightTex", 1);
        // M6 (plan 9.3): 1 = aerial perspective + sombra de nubes en earth.frag.
        // 0 = camino pre-M6 exacto (aceptacion 6: Atmosphere OFF = 16b6fa5).
        impl->earthShader.setInt("u_atmoOn", impl->showAtmosphere ? 1 : 0);
        impl->earth.render();
    }

    // (Atmosfera y terminador: se dibujan en el pase aditivo post-bloom para
    // que el halo salga nitido y no se queme a blanco con el bloom.)

    // M9f (paridad demo): el ribbon HF se dibuja AQUI (pase de escena,
    // antes del resolve) para que alimente el bloom como el TubeGeometry
    // de la demo — en overlays post-bloom jamas lo hacia. Estado propio
    // (el bloque fija el suyo despues); depth test de escena intacto.
    if (impl->hfTraceVisible) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        impl->hfTrace.render(view, proj, camPos, impl->time);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    // Release-prep (paridad demo: en la demo florecen): las ionosondas se
    // dibujan AQUI (pase de escena, pre-bloom) en vez del overlay post-bloom.
    // Estado propio como el bloque M9f; IonosondeLayer no fija blend.
    {
        const bool showPtsScene = impl->ionosondeLayer.isVisible() &&
                                  impl->legendSolo == Impl::SoloTarget::None;
        if (showPtsScene) {
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glDepthMask(GL_FALSE);
            impl->ionosondeLayer.render(view, proj);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }
    impl->sceneFBO.unbind();
    // M1: resolve MSAA -> colorTexture ANTES del bloom (las pasadas 2 y 3
    // siguen leyendo colorTexture; no-op con samples == 0).
    impl->sceneFBO.resolve();

    // --- Pass 2: Bloom ---
    if (impl->bloomEnabled) {
        glDisable(GL_DEPTH_TEST);
        impl->bloomShader.use();

        // Horizontal pass
        impl->bloomFBO[0].bind();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
            impl->bloomShader.setVec2("u_direction", glm::vec2(6.0f, 0.0f));
        impl->bloomShader.setVec2("u_resolution", glm::vec2((float)impl->bloomFBO[0].width, (float)impl->bloomFBO[0].height));
        impl->bloomShader.setFloat("u_threshold", impl->bloomThreshold);
        impl->bloomShader.setFloat("u_intensity", impl->bloomIntensity);
        impl->bloomShader.setInt("u_pass", 0);   // M1: pasada H (sin intensity)
        impl->bloomShader.setInt("u_texture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, impl->sceneFBO.colorTexture);
        glBindVertexArray(impl->quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        impl->bloomFBO[0].unbind();

        // Vertical pass
        impl->bloomFBO[1].bind();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
            impl->bloomShader.setVec2("u_direction", glm::vec2(0.0f, 6.0f));
        impl->bloomShader.setInt("u_pass", 1);   // M1: pasada V (aplica intensity)
        impl->bloomShader.setFloat("u_threshold", hfc::bloomBypassThreshold());  // M9f: V en passthrough (la H umbraliza; smoothstep(-1,-0.75,l>=0)=1)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, impl->bloomFBO[0].colorTexture);
        glBindVertexArray(impl->quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        impl->bloomFBO[1].unbind();

        // Restaurar el viewport a resolución completa: unbind() no lo restaura y el
        // composite/ImGui se dibujarían a mitad de resolución (globo de otro tamaño + borroso).
        glViewport(0, 0, winW, winH);

        glEnable(GL_DEPTH_TEST);
    }

    // --- Overlays: se dibujan en sceneFBO DESPUES de computar el bloom (no
    // alimentan el bright-pass: sin realimentacion ni quemado) pero ANTES del
    // composite: el bloom ya calculado (solo Tierra) se les anade encima y el
    // tonemap ACES les afecta igual que al resto de la escena.
    bool additiveDrawn = false;
    // Hover-solo de leyenda (frame anterior): aislar una entrada ionosferica.
    // Fondo (Tierra/estrellas) y referencia (atmosfera/terminador/anillos)
    // siempre se dibujan; el resto solo si no hay solo o si es el elegido.
    Impl::SoloTarget solo = impl->legendSolo;
    auto soloOk = [&](Impl::SoloTarget t) {
        return solo == Impl::SoloTarget::None || solo == t;
    };
    bool showTec = impl->tecLayer.isVisible() && soloOk(Impl::SoloTarget::Main);
    bool showShell = impl->shellLayer.isVisible() && soloOk(Impl::SoloTarget::Main);
    bool showPeak = impl->peakShell.isVisible() && soloOk(Impl::SoloTarget::Peak);
    bool showModel = impl->tecColShell.isVisible() && soloOk(Impl::SoloTarget::Model);
    bool showVol = impl->volumeLayer.isVisible() && soloOk(Impl::SoloTarget::Volume);
    bool showVolScene = showVol && impl->volumeMode == 0;   // con tonemap
    bool showVolCat = showVol && impl->volumeMode == 1;     // post-composite, exacto
    // (showPts: pase de escena pre-bloom desde release-prep; solo sigue para el resto)
    bool showSW = impl->solarWindLayer.isVisible() && solo == Impl::SoloTarget::None;
    bool showFar = impl->faradayLayer.isVisible() && solo == Impl::SoloTarget::None;
    bool showAur = impl->auroraLayer.isVisible() && solo == Impl::SoloTarget::None;
    bool showSlice = impl->sliceOn && impl->sliceLayer.isVisible() && solo == Impl::SoloTarget::None;
    if (impl->showAtmosphere || impl->terminatorVisible || showTec || showSW || showFar || showAur || showShell || showVolScene || showPeak || showModel || showSlice || impl->refShells.isVisible()) {
        impl->sceneFBO.bind();
        glViewport(0, 0, winW, winH);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        // Atmosfera primero (halo base bajo las capas de datos).
        if (impl->showAtmosphere) {
            // M6 (P3, plan 9.1): escala = ATM_R del shader. El literal 1.0255f va
            // CRUZADO con la constante ATM_R de shaders/atmosphere.frag y con el
            // bake de la LUT en Atmosphere.cpp — mantener los tres sincronizados.
            glm::mat4 atmoModel = glm::scale(glm::mat4(1.0f), glm::vec3(1.0255f));
            impl->atmosphereShader.use();
            impl->atmosphereShader.setMat4("u_model", atmoModel);
            impl->atmosphereShader.setMat4("u_view", view);
            impl->atmosphereShader.setMat4("u_projection", proj);
            impl->atmosphereShader.setVec3("u_sunDir", impl->sunDirection);
            impl->atmosphereShader.setVec3("u_cameraPos", camPos);
            // M6 (plan 9.4): calidad 0 = High / 1 = LUT + intensidad (slider P5).
            impl->atmosphereShader.setInt("u_lutMode", impl->atmoQuality);
            impl->atmosphereShader.setFloat("u_intensity", impl->atmoIntensity);
            impl->atmosphereShader.setInt("u_lutTex", 0);
            // M6: aditivo PURO para la atmosfera. El frag escribe alpha = luminancia
            // (plan 9.1); con (SRC_ALPHA, ONE) la contribucion seria col*alpha
            // (cuadratica) y el limbo azul (col ~ 0.02-0.12) quedaria en negro.
            // Se restaura el blend del bloque justo despues del draw.
            glBlendFunc(GL_ONE, GL_ONE);
            impl->atmosphere.render();   // M6 (P2): cull + LUT bind dentro
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        }
        if (showTec) impl->tecLayer.render(view, proj);
        if (showShell) impl->shellLayer.render(view, proj);
        if (showPeak) impl->peakShell.render(view, proj);
        if (showModel) impl->tecColShell.render(view, proj);
        if (showVolScene) impl->volumeLayer.render(view, proj, camPos);
        if (showSlice) impl->sliceLayer.render(view, proj);
        // Terminador aqui (post-bloom): linea fina y nitida, sin quemarse.
        if (impl->terminatorVisible) {
            impl->terminatorShader.use();
            glm::mat4 tmodel = glm::mat4(1.0f);
            impl->terminatorShader.setMat4("u_model", tmodel);
            impl->terminatorShader.setMat4("u_view", view);
            impl->terminatorShader.setMat4("u_projection", proj);
            impl->terminator.render();
        }
        if (impl->refShells.isVisible()) impl->refShells.render(view, proj);
        // (showPts: pase de escena pre-bloom desde release-prep)
        if (showSW) impl->solarWindLayer.render(view, proj);
        if (showFar) impl->faradayLayer.render(view, proj);
        if (showAur) impl->auroraLayer.render(view, proj);
        // M8 (plan 11.3): el ribbon se dibuja en el pase de escena (M9f,
        // antes del resolve) para alimentar el bloom; aqui ya no va nada.
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        impl->sceneFBO.unbind();
        // M1: re-resolver tras los overlays (el composite lee colorTexture;
        // mismo blit que antes del bloom, no-op sin MSAA).
        impl->sceneFBO.resolve();
        glViewport(0, 0, winW, winH);
        additiveDrawn = true;
    }

    // --- Pass 3: Composite to screen ---
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    impl->compositeShader.use();

    // Unit 0 = scene
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, impl->sceneFBO.colorTexture);
    impl->compositeShader.setInt("u_scene", 0);

    // Unit 1 = bloom (DO NOT rebind GL_TEXTURE0!)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, impl->bloomFBO[1].colorTexture);
    impl->compositeShader.setInt("u_bloom", 1);

    impl->compositeShader.setBool("u_bloomEnabled", impl->bloomEnabled);
    impl->compositeShader.setFloat("u_bloomStrength", impl->bloomStrength);
    impl->compositeShader.setBool("u_toneMap", impl->toneMap);
    impl->compositeShader.setFloat("u_exposure", impl->exposure);

    // Draw fullscreen quad with persistent VAO
    glBindVertexArray(impl->quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);

    // --- Pass 3b: capa categorica DESPUES del composite (colores exactos,
    // sin tonemap ni bloom; oclusion terrestre analitica en el shader).
    if (showVolCat) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        impl->volumeLayer.render(view, proj, camPos);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // --- ImGui --- (Hide UI lo oculta todo salvo el aviso breve;
    // siempre visible mientras llegan los datos iniciales)
    bool loading = !impl->initialLoadDone.load();
    bool uiHidden = impl->hideUI && (float)glfwGetTime() >= impl->cineHintUntil && !loading;
    if (!uiHidden) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if ((float)glfwGetTime() < impl->cineHintUntil && impl->hintMsg[0] != '\0') {
        ImGui::SetNextWindowPos(ImVec2((float)winW * 0.5f - 110.0f, (float)winH - 40.0f), ImGuiCond_Always);
        ImGui::Begin("##cinehint", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.85f), "%s", impl->hintMsg);
        ImGui::End();
    }

    // Loading overlay: live data arrives in the background, globe is immediate.
    if (!impl->initialLoadDone.load()) {
        int done = impl->loadDone.load(), total = impl->loadTotal.load();
        ImGui::SetNextWindowPos(ImVec2((float)winW * 0.5f - 160.0f, (float)winH * 0.5f - 50.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(320, 90), ImGuiCond_Always);
        ImGui::Begin("##loading", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::Text("Downloading live data... %d/%d", done, total > 0 ? total : 5);
        ImGui::ProgressBar(total > 0 ? (float)done / (float)total : 0.0f, ImVec2(-1, 0), "");
        ImGui::TextDisabled("Globe, clouds and UI stay interactive.");
        ImGui::End();
    }

    // FPS
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 60), ImGuiCond_FirstUseEver);
    ImGui::Begin("Ionosphere Live 3D");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Frames per second. Below ~30 the view feels sluggish (try disabling Bloom).");
    ImGui::Text("OpenGL: %s", glGetString(GL_VERSION));
    ImGui::Checkbox("Bloom", &impl->bloomEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Post-process glow on Earth/atmosphere. Ionosphere layers are drawn afterwards, so they never bloom.");
    if (ImGui::SliderFloat("Threshold", &impl->bloomThreshold, 0.0f, 1.0f)) {
        impl->bloomThreshold = glm::clamp(impl->bloomThreshold, 0.0f, 1.0f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Bright-pass threshold: only pixels brighter than this feed the glow. Lower = stronger, more diffuse glow. Soft knee (0.6 wide): the glow fades in smoothly instead of popping.");
    if (ImGui::SliderFloat("Intensity", &impl->bloomIntensity, 0.0f, 3.0f)) {
        impl->bloomIntensity = glm::clamp(impl->bloomIntensity, 0.0f, 3.0f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Blur pass gain, LINEAR since M1 (it used to behave as its square). Scales the extracted bright areas before compositing; old settings are migrated once on load.");
    if (ImGui::SliderFloat("Strength", &impl->bloomStrength, 0.0f, 2.0f)) {
        impl->bloomStrength = glm::clamp(impl->bloomStrength, 0.0f, 2.0f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Final mix: how much glow is added over the scene. 0 = no visible effect even if Bloom is on.");
    ImGui::Checkbox("Tone map", &impl->toneMap);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("ACES tone mapping on scene+bloom: highlights roll off instead of clipping to flat white. Off = original image, for A/B comparison.");
    if (ImGui::SliderFloat("Exposure", &impl->exposure, 0.2f, 2.5f)) {
        impl->exposure = glm::clamp(impl->exposure, 0.2f, 2.5f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Manual exposure for the tone mapping curve. Only applies when Tone map is on.");
    {
        // M1: MSAA de escena 0/2/4 (persistible). El cambio recrea los targets
        // en caliente: mismo punto de recreacion que el resize (hilo seguro).
        const char* msaaNames[3] = { "Off", "2x", "4x" };
        int msaaIdx = (impl->msaaSamples >= 4) ? 2 : (impl->msaaSamples == 2 ? 1 : 0);
        if (ImGui::SliderInt("MSAA", &msaaIdx, 0, 2, msaaNames[msaaIdx])) {
            impl->msaaSamples = msaaIdx * 2;   // 0 / 2 / 4
            recreateRenderTargets(winW, winH);
        }
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Multisample anti-aliasing on the 3D scene (shells, terminator, station points). Rebuilds render targets on change; 0 = off (fastest, for weak GPUs and A/B).");
    }
    if (ImGui::Checkbox("Cinema (C)", &impl->cineMode)) {
        snprintf(impl->hintMsg, sizeof(impl->hintMsg), "CINEMA %s - press C to exit",
                 impl->cineMode ? "ON" : "OFF");
        impl->cineHintUntil = (float)glfwGetTime() + 4.0f;
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Cinema mode: slow auto-rotation. UI stays visible (use Hide UI to clear the screen).");
    if (ImGui::SliderFloat("Cine speed", &impl->cineSpeed, 0.005f, 0.30f, "%.3f rad/s")) {
        impl->cineSpeed = glm::clamp(impl->cineSpeed, 0.005f, 0.30f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Auto-rotation speed in cinema mode (radians per second). 0.05 = one turn every ~2 min.");
    if (ImGui::Checkbox("Hide UI (H)", &impl->hideUI)) {
        if (impl->hideUI) {
            snprintf(impl->hintMsg, sizeof(impl->hintMsg), "UI HIDDEN - press H to restore");
            impl->cineHintUntil = (float)glfwGetTime() + 4.0f;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Hide every panel and window. The hint fades after 4 s; press H to bring the UI back.");
    // FASE B (P5): bloque unico de atribucion (G7). Texto completo aqui;
    // los badges por fuente siguen visibles en sus paneles.
    if (ImGui::CollapsingHeader("Data sources & attribution")) {
        ImGui::TextWrapped("GIRO / LGDC community ionosonde data (CC BY-NC-SA 4.0, non-commercial educational use). Please cite: Reinisch, B. W., and I. A. Galkin, Global ionospheric radio observatory (GIRO), Earth, Planets and Space, 63, 377-381, doi:10.5047/eps.2011.03.001, 2011. SPASE: http://spase.info/SMWG/Observatory/GIRO.");
        ImGui::TextWrapped("Failover: prop.kc2g.com redistribution (GIRO data with permission, WWROF-supported, plus eSWua/INGV and NOAA feeds). Also: eSWua/INGV, DIAS, NOAA SWPC (GloTEC, SDO/HMI, sunspots).");
        ImGui::TextWrapped("Access policy: polite cadence and mirrors lower the risk; they don't remove it - we have measured 429/504 under load, which is why the kc2g failover exists.");
        ImGui::TextWrapped("IRTAM/GAMBIT (UML): retrospective IRTAM coefficients via LGDC gambit-coeffs (same GIRO citation above; 1 req/15 s per client, last 3 days unavailable by design).");
    }
    // M-prov-matrix (048): seccion viva dentro de Data sources (thin glue).
    if (ImGui::CollapsingHeader("Provider status (live)")) {
        static double lastProvGate = -1.0;
        const double nowUtcPS = (double)std::time(nullptr);
        if (lastProvGate < 0.0 || nowUtcPS - lastProvGate >= 1.0) {
            lastProvGate = nowUtcPS;
            // 4 filas main-thread (accessors ya thread-safe) + copia.
            GiroAdapter::GiroNetStatus gs = impl->giro.getNetStatus();
            const int mode = impl->ionSourceMode.load();
            provstatus::Snapshot s0;
            s0.id = provstatus::PROV_GIRO;
            s0.hasData = gs.withData > 0;
            double latestT = 0.0;
            for (const auto& sm : impl->giro.getSamples())
                if (sm.valid && sm.timeUtc > latestT) latestT = sm.timeUtc;
            s0.dataEpoch = latestT;
            s0.fetching = impl->giro.isFetching();
            s0.rateLimited = (mode == Impl::SRC_KC2G_FALLBACK) ||
                             provstatus::hard429(gs.roundAttempts, gs.roundOk);
            s0.retryInMin = gs.retryInMin;
            s0.data = std::to_string(gs.withData) + "/" + std::to_string(gs.polled) + " st";
            if (gs.histHours > 0.0) {
                const int th = (int)(gs.histHours * 10.0);
                char hb[32];
                std::snprintf(hb, sizeof(hb), "%d.%d h", th / 10, th % 10);
                s0.data += " · hist ";
                s0.data += hb;
            }
            s0.error = gs.degraded > 0 ? ("backoff " + std::to_string(gs.degraded) + " st") : "";
            Kc2gAdapter::Kc2gStatus ks = impl->kc2g.getStatus();
            provstatus::Snapshot s1;
            s1.id = provstatus::PROV_KC2G;
            s1.hasData = ks.rows > 0;
            s1.dataEpoch = ks.lastOkUtc;
            s1.fetching = impl->kc2g.isFetching();
            s1.data = std::to_string(ks.rows) + " rows";
            const int nDias = impl->dias.getStationCount();
            provstatus::Snapshot s2;
            s2.id = provstatus::PROV_DIAS;
            s2.hasData = nDias > 0;
            s2.fetching = impl->dias.isFetching();
            s2.data = std::to_string(nDias) + " st";
            s2.error = impl->dias.getErrorMessage();
            const EbroSample ebLatest = impl->ebro.getLatest();
            provstatus::Snapshot s3;
            s3.id = provstatus::PROV_EBRO;
            s3.hasData = ebLatest.valid;
            s3.dataEpoch = ebLatest.timeUtc;
            s3.fetching = impl->ebro.isFetching();
            s3.error = impl->ebro.getErrorMessage();
            {
                std::lock_guard<std::mutex> lk(impl->bundleMutex);
                impl->provBoard[provstatus::PROV_GIRO] = s0;
                impl->provBoard[provstatus::PROV_KC2G] = s1;
                impl->provBoard[provstatus::PROV_DIAS] = s2;
                impl->provBoard[provstatus::PROV_EBRO] = s3;
                for (int i = 0; i < (int)provstatus::PROV_COUNT; i++)
                    impl->provCopy[i] = impl->provBoard[i];
            }
        }
        std::vector<provstatus::Row> prows = provstatus::buildRows(
            impl->provCopy, (size_t)provstatus::PROV_COUNT, nowUtcPS);
        if (ImGui::BeginTable("provstatus", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Provider");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("Data");
            ImGui::TableSetupColumn("Reason");
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < prows.size(); i++) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(prows[i].provider.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(prows[i].stateText.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(prows[i].data.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(prows[i].reason.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();

    // Layer panel
    ImGui::SetNextWindowPos(ImVec2((float)winW - 220, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(210, 250), ImGuiCond_FirstUseEver);
    ImGui::Begin("Layers");
    ImGui::Checkbox("Earth", &impl->showEarth);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Earth globe with day/night texture driven by the solar position.");
    if (ImGui::SliderFloat("Night light", &impl->nightLevel, 0.05f, 1.0f)) {
        impl->nightLevel = glm::clamp(impl->nightLevel, 0.05f, 1.0f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Brightness of the night side (NASA Black Marble city lights). Raise it if the lights are dim on your monitor.");
    if (ImGui::SliderFloat("Clouds", &impl->cloudAmt, 0.0f, 1.0f)) {
        impl->cloudAmt = glm::clamp(impl->cloudAmt, 0.0f, 1.0f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Procedural animated cloud cover (seamless noise, drifts with time). 0 = clear sky.");
    ImGui::Checkbox("Atmosphere", &impl->showAtmosphere);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Rayleigh/Mie atmosphere (single scattering): blue daytime limb, red ring at the terminator, aerial perspective + cloud shadows on Earth.");
    // M6 (plan 9.4): toggle de calidad + slider de intensidad junto al
    // checkbox (P5). atmoQuality/atmoIntensity persisten (saveSettings).
    const char* atmoQ[] = { "High (raymarch)", "LUT (fast)" };
    if (ImGui::Combo("Atmo quality", &impl->atmoQuality, atmoQ, 2)) {
        impl->atmoQuality = glm::clamp(impl->atmoQuality, 0, 1);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("High: 14x5 raymarch per pixel. LUT: 12 view samples + baked transmittance table (iGPU friendly).");
    if (ImGui::SliderFloat("Atmo intensity", &impl->atmoIntensity, 0.25f, 2.0f, "%.2f")) {
        impl->atmoIntensity = glm::clamp(impl->atmoIntensity, 0.25f, 2.0f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Atmosphere brightness multiplier (default 1.0).");
    ImGui::Checkbox("Stars", &impl->showStars);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Starfield background. Decorative only.");
    ImGui::Checkbox("Terminator", &impl->terminatorVisible);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Thin ring on the day/night boundary (follows the real sun). Turn off if you prefer a clean globe.");
    bool sunWas = impl->sunVisible;
    ImGui::Checkbox("Sun", &impl->sunVisible);  // M11 (N2: abrir patea)
    if (impl->sunVisible && !sunWas) impl->sdoKick.store(true);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("SDO solar disk (HMI/AIA-304) with active regions. Downloads only while open (1 JPG/15 min).");
    ImGui::Separator();
    ImGui::Text("Ionosphere");
    HelpMarker("Color shell over the globe. Pick the variable (real TEC or GIRO/HF interpolations) and it is painted with its colormap.");
    // Variable de la capa de colores esferica (Seccion A + Fase 5 + M-IRTAM-F2 P4 + B0B1):
    // TEC / GIRO (foF2,MUF,hmF2,NmF2) / Radio (LUF,FOT,Spread-F) / IRTAM (foF2,hmF2,B0,B1 retrospectivos).
    const char* mapVars[] = { "TEC", "foF2", "MUF", "hmF2", "NmF2", "LUF", "FOT", "Spread-F", "IRTAM foF2", "IRTAM hmF2", "IRTAM B0", "IRTAM B1" };
    if (ImGui::Combo("Variable", &impl->mapVariable, mapVars, 12)) {
        applyColorLayer();
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("TEC: electron content (TECU, NOAA GloTEC).\nfoF2: F2-layer critical frequency (MHz, GIRO).\nMUF: max usable at 3000 km (MHz, GIRO).\nhmF2: F2 peak height (km, GIRO).\nNmF2: F2 peak density (e/m3, from foF2).\nLUF: lowest usable (MHz, ITU-style derived).\nFOT: optimum working = 0.85*MUF (MHz).\nSpread-F: % spread-echo occurrence (diverging).\nIRTAM foF2/hmF2: retrospective IRTAM grids (MHz/km, gambit-coeffs, lag ~3 d).\nIRTAM B0/B1: bottomside thickness/shape (km/[], gambit-coeffs, lag ~3 d).");
    // M-IRTAM-F2 (P4 cond. 3 + P5): badge DATA-age desde el cache P2.
    // Fuente = TOV del bucket (red line); visibilidad = solo con capa IRTAM.
    // Edad andante: se calcula AL PINTAR con el reloj de lectura (E10).
    // P5: estado textual honesto via irtamstate (motivo en linea, no solo
    // tooltip) + nota frozen-declarada en replay (C9).
    // F2-B0B1-R1 Q3: badge para 8-11; B0/B1 exigen ademas su grid presente
    // (par ausente = ausente); DATA-age y valid los gobierna F/H (Q1).
    // M-irtam-replay-A (ruling R1 Q5.3): en replay el badge nombra el TOV
    // MUESTREADO (no el fresco) via badgeSampled.
    if ((impl->mapVariable >= 8 && impl->mapVariable <= 9) ||
        (impl->mapVariable >= 10 && impl->mapVariable <= 11)) {
        const char* ipName = (impl->mapVariable == 8)    ? "foF2"
                             : (impl->mapVariable == 9)  ? "hmF2"
                             : (impl->mapVariable == 10) ? "B0"
                                                         : "B1";
        auto ib = impl->getIrtam();
        const double nowUtc = (double)std::time(nullptr);
        const double ageMin = (ib && ib->dataEpoch > 0.0)
            ? (nowUtc - ib->dataEpoch) / 60.0 : -1.0;
        bool bValid = (ib && ib->valid);
        if (ib && (impl->mapVariable == 10 || impl->mapVariable == 11)) {
            const GridData& src =
                (impl->mapVariable == 10) ? ib->b0 : ib->b1;
            if (src.values.empty() || !src.valid) bValid = false;
        }
        const std::string bErr = ib ? ib->error : "";
        std::string line;
        bool dimmed = !bValid || ageMin < 0.0;
        if (impl->replayMode && impl->replayEpoch > 0.0) {
            // M-irtam-replay-024: lectura bajo handoff corto (el worker
            // escribe la muestra; solo escalares, sin espera).
            // M-irtam-replay-028: el badge nombra el TOV del bracket MAS
            // CERCANO al cursor (la imagen es el lerp ahi).
            double sTov = 0.0;
            bool sCl = false, sHave = false;
            {
                std::lock_guard<std::mutex> lk(impl->irtamSampleMutex);
                const auto& s = impl->irtamSample_;
                const int wantPair =
                    (impl->mapVariable == 10 || impl->mapVariable == 11)
                        ? 1
                        : 0;
                if (s.have && s.pairKind == wantPair && s.t1 > s.t0) {
                    const double e = impl->replayEpoch;
                    sTov = (e - s.t0 < s.t1 - e) ? s.t0 : s.t1;
                    sCl = s.clamped;
                    sHave = true;
                }
            }
            if (sHave) {
                line = irtamstate::badgeSampled(ipName, sTov, nowUtc, sCl);
                dimmed = false;
            } else {
                line = irtamstate::badgeSampled(ipName, 0.0, nowUtc, false);
                dimmed = true;
            }
        } else {
            line = irtamstate::badgeLine(ipName, bValid,
                                         ib ? ib->dataEpoch : 0.0, bErr,
                                         ageMin, impl->replayMode);
        }
        if (dimmed) {
            ImGui::TextDisabled("%s", line.c_str());
        } else {
            ImGui::Text("%s", line.c_str());
        }
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("DATA-age del bucket gambit-coeffs (TOV del mensaje, no del fetch). Malla 46x45 evaluada en P1b.");
    }
    bool showTEC = impl->tecVisible;
    if (ImGui::Checkbox("Color Layer", &showTEC)) {
        impl->tecVisible = showTEC;
        impl->tecLayer.setVisible(showTEC);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Shows/hides the spherical color layer (the variable selected above).");
    // Colormap manual: sobreescribe el automatico de cualquier variable. Con el fix
    // en GridLayer::setColormap, el cambio se refleja al instante.
    const char* colormaps[] = { "viridis", "inferno", "plasma", "turbo" };
    if (ImGui::Combo("Colormap", &impl->manualColormap, colormaps, 4)) {
        impl->colormapOverride = true;
        impl->tecLayer.setColormap(colormaps[impl->manualColormap & 3]);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Color palette. Each variable has its own by default (Auto); picking one enables manual override.");
    ImGui::SameLine();
    if (ImGui::Button("Auto")) {   // restablecer el colormap automatico de la variable
        impl->colormapOverride = false;
        applyColorLayer();
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Back to the variable's automatic colormap (TEC=viridis, foF2/LUF=plasma, MUF/FOT=inferno, hmF2/NmF2/Spread-F=turbo).");
    if (impl->colormapOverride) ImGui::TextColored(ImVec4(0.7f,0.9f,0.7f,1), "Manual override ON");
    if (ImGui::SliderFloat("Altitude (km)", &impl->altKm, 100.0f, 800.0f)) {
        impl->altKm = glm::clamp(impl->altKm, 100.0f, 800.0f);
        applyAltitudes();
        impl->hfCircuitDirty = true;   // M9fix: rebuild del apice en el mismo frame (sigue al slider de capa)
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Height of the color shell above the surface (100-800 km). The outer shell follows +0.025. Visual only. The HF ray apex follows this (offset over h').");
    if (ImGui::SliderFloat("Layer opacity", &impl->tecOpacity, 0.0f, 2.0f)) {
        impl->tecLayer.setOpacity(impl->tecOpacity);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Opacity of the main color layer (TEC, foF2, MUF, hmF2, NmF2, LUF, FOT or Spread-F — whichever is selected).");
    // ---- Relieve 3D estilo volumetric (imagen 1) ----
    if (ImGui::Checkbox("3D relief", &impl->reliefOn)) {
        applyRelief();
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Deform the shell in radius by the local value (like the storm-time volumetric renders): highs bulge outward, lows hug the globe.");
    if (ImGui::SliderFloat("Relief amp", &impl->reliefAmp, 0.0f, 0.10f)) {
        impl->reliefAmp = glm::clamp(impl->reliefAmp, 0.0f, 0.10f);
        applyRelief();
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Base relief amplitude in Earth radii (globe = 1.0). 0.025 is subtle, 0.06 dramatic.");
    if (ImGui::Checkbox("Storm swell (Kp)", &impl->stormSwell)) {
        applyRelief();
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Scale relief by (1 + Kp/6): the shell visibly breathes with geomagnetic activity.");
    if (ImGui::Checkbox("Outer shell", &impl->outerShell)) {
        impl->shellLayer.setVisible(impl->outerShell);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Second nested isosurface (+0.025 radius) sharing the same grid and variable.");
    if (ImGui::SliderFloat("Shell opacity", &impl->shellOpacity, 0.0f, 2.0f)) {
        impl->shellOpacity = glm::clamp(impl->shellOpacity, 0.0f, 2.0f);
        impl->shellLayer.setOpacity(impl->shellOpacity);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Opacity of the outer shell. Visual only.");
    if (ImGui::Combo("Shell colormap", &impl->shellColormap, colormaps, 4)) {
        impl->shellColormap = glm::clamp(impl->shellColormap, 0, 3);
        impl->shellLayer.setColormap(colormaps[impl->shellColormap & 3]);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Palette of the outer shell (independent from the main layer).");
    if (ImGui::Checkbox("F2 peak shell", &impl->peakShellVisible)) {
        impl->peakShell.setVisible(impl->peakShellVisible);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("F2 peak extracted from the volume: floats at the real measured hmF2 per column, colored by peak log-density (plasma). Rises and reddens in storms.");
    if (ImGui::Checkbox("Model TEC", &impl->tecColVisible)) {
        impl->tecColShell.setVisible(impl->tecColVisible);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("TEC integrated from the volume (model, flat at 300 km). Compare against measured GloTEC: ocean/night bias reveals the median background fill.");
    if (ImGui::Checkbox("Volumetric density", &impl->volumeVisible)) {
        impl->volumeLayer.setVisible(impl->volumeVisible);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Real raymarched electron density Ne(lat, lon, h) rebuilt from station profiles (Chapman + Epstein) every ~5 s. Heavier on the GPU; follows live and replay.");
    ImGui::Checkbox("Limb zoom", &impl->pipVisible);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Magnifier following the east limb: same framebuffer, x3 zoom, no extra render cost. Shows shell stacking edge-on.");
    if (ImGui::SliderFloat("Volume opacity", &impl->volumeOpacity, 0.0f, 2.5f)) {
        impl->volumeOpacity = glm::clamp(impl->volumeOpacity, 0.0f, 2.5f);
        impl->volumeLayer.setOpacityScale(impl->volumeOpacity);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Brightness of the volumetric rendering. Visual only.");
    const char* volModes[] = { "Density", "Layers" };
    if (ImGui::Combo("Volume mode", &impl->volumeMode, volModes, 2)) {
        impl->volumeMode = impl->volumeMode & 1;
        impl->volumeLayer.setColorMode(impl->volumeMode);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Density: continuous viridis scale. Layers: 4 fixed colors by winning layer (D orange, E green, F1 yellow, F2 cyan), exact boundaries (unfiltered).");
    if (ImGui::Checkbox("Iso bands", &impl->isoBands)) {
        impl->volumeLayer.setIsoBands(impl->isoBands);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Highlight shells at logNe 10.5/11/11.5 (white bands). Works in both volume modes.");
    ImGui::Checkbox("Labels", &impl->labelsVisible);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Floating labels anchored in 3D (station F2 peak, aurora oval, TEC shell) with leader lines.");
    if (ImGui::SliderFloat("Explode", &impl->explodeFactor, 1.0f, 5.0f)) {
        impl->explodeFactor = glm::clamp(impl->explodeFactor, 1.0f, 5.0f);
        applyAltitudes();
        impl->volumeLayer.setExplode(impl->explodeFactor);
        impl->refShells.setExplode(impl->explodeFactor);
        impl->hfCircuitDirty = true;   // M9: rebuild del ribbon en el mismo frame (la altura lo sigue)
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Exploded view: separates shell altitudes to reveal stacking order. x1 = true scale. The HF ray ribbon follows: apex height scales with this; width stays demo-constant (0.0075).");
    if (ImGui::Checkbox("Path slice", &impl->sliceOn)) {
        impl->sliceLayer.setVisible(impl->sliceOn);
        if (!impl->sliceOn) { impl->sliceA.clear(); impl->sliceB.clear(); }
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Vertical Ne(h) curtain with viridis scale. Source below; rebuilt with the volume.");
    const char* sliceSrcs[] = { "Pin+Hover", "Manual A/B", "Fixed point" };
    ImGui::SetNextItemWidth(130.0f);
    ImGui::Combo("Slice src", &impl->sliceSrc, sliceSrcs, 3);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Curtain endpoints: pinned+hovered stations, manual coordinates, or a fixed ±2° curtain around one point.");
    if (impl->sliceOn && impl->sliceSrc >= 1) {
        ImGui::SliderFloat("A lat", &impl->sliceLatA, -90.0f, 90.0f);
        ImGui::SliderFloat("A lon", &impl->sliceLonA, -180.0f, 180.0f);
        if (impl->sliceSrc == 1) {
            ImGui::SliderFloat("B lat", &impl->sliceLatB, -90.0f, 90.0f);
            ImGui::SliderFloat("B lon", &impl->sliceLonB, -180.0f, 180.0f);
        }
    }
    if (impl->sliceOn) {
        if (!impl->sliceA.empty() && !impl->sliceB.empty() && impl->sliceLayer.hasData()) {
            ImGui::Text("%s -> %s  pk %.1f", impl->sliceA.c_str(), impl->sliceB.c_str(), impl->slicePeakLog);
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Path endpoints and peak log10 electron density [m^-3] along it. Higher peak = easier reflection on HF.");
        } else if (impl->sliceSrc == 0) {
            ImGui::TextDisabled("Pin a station (A), hover another (B).");
        } else {
            ImGui::TextDisabled("Waiting for volume...");
        }
    }
    if (ImGui::Checkbox("Ref shells E/F1/F2", &impl->refShellsVisible)) {
        impl->refShells.setVisible(impl->refShellsVisible);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Reference altitude rings: E fixed at 110 km, F1/F2 follow the measured hmF2 median (dynamic). No data of their own, just height cues.");

    // ---- GIRO ionosondas (Fase 3) ----
    ImGui::Separator();
    // M4 (P4/P5): estado de red GIRO real (polled/withData/degraded/hist).
    GiroAdapter::GiroNetStatus giroStatus = impl->giro.getNetStatus();
    bool showGIRO = impl->giroStationsVisible;
    if (ImGui::Checkbox("GIRO stations", &showGIRO)) {
        impl->giroStationsVisible = showGIRO;
        impl->ionosondeLayer.setVisible(showGIRO);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("GIRO network (%d ionosondes in the catalog, %d polled via getbest). Each dot is a station with foF2/MUF/hmF2/NmF2; non-polled ones keep their catalog position without data. Hover the globe for details, click to pin.", impl->giro.getStationCount(), giroStatus.polled);
    const char* giroModes[] = { "foF2 [MHz]", "MUF(D) [MHz]", "hmF2 [km]", "NmF2 [m^-3]" };
    if (ImGui::Combo("Metric", &impl->giroMode, giroModes, 4)) {
        impl->ionosondeLayer.setMode(impl->giroMode);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Quantity coloring the dots:\nfoF2 = F2 critical frequency (MHz).\nMUF(D) = max usable at 3000 km (MHz).\nhmF2 = F2 peak height (km).\nNmF2 = peak density (e/m3, computed from foF2).");
    if (ImGui::SliderFloat("Point size", &impl->giroPointSize, 3.0f, 30.0f)) {
        impl->giroPointSize = glm::clamp(impl->giroPointSize, 3.0f, 30.0f);
        impl->ionosondeLayer.setPointSize(impl->giroPointSize);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Dot size in pixels. Visual only.");
    if (impl->ionosondeLayer.isFetching()) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Fetching GIRO... %d%%", impl->ionosondeLayer.getProgress());
    } else {
        float minV, maxV;
        impl->ionosondeLayer.getActiveRange(minV, maxV);
        ImGui::Text("GIRO: %d/%d polled w/ data (%d pts shown)", giroStatus.withData,
                    giroStatus.polled, impl->ionosondeLayer.getStationCount());
        ImGui::Text("Range: %.2f .. %.2f", minV, maxV);
    }
    if (giroStatus.degraded > 0) {
        // M4 (7.2): estaciones en backoff (429/fallo) — reintento proximo.
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
            "GIRO: rate-limited, retry %.0f min", giroStatus.retryInMin);
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Some stations failed (429/timeout): their refresh interval doubles up to 30 min until success. Their last good data is kept.");
    }
    // M4R-A (FASE A): badge de fuente + estado y atribucion del failover.
    if (impl->ionSourceMode.load() == Impl::SRC_KC2G_FALLBACK) {
        Kc2gAdapter::Kc2gStatus ks = impl->kc2g.getStatus();
        // FASE B (P4): edad viva al pintar — now menos el timeUtc de la
        // fila gridUsable mas fresca (recorre samples_ bajo lock). Misma
        // enmienda semantica que cerro M12/N3-read: la edad es del DATO
        // contra el reloj de LECTURA. El status queda como metrica de
        // fetch (diagnostico).
        const double liveAge =
            impl->kc2g.freshestAgeMinLive((double)std::time(nullptr));
        ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f),
            "Source: kc2g-fallback (%d/%d st, freshest %.0f min)",
            ks.panelUsable, ks.rows, liveAge < 0.0 ? 0.0 : liveAge);
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip(
            "GIRO (lgdc.uml.edu) is failing or rate-limited: automatic failover to the prop.kc2g.com redistribution (101 stations; GIRO data with permission, WWROF-supported, plus eSWua/INGV and NOAA feeds). One request per cycle, only while GIRO fails. Rows older than 6 h stay out of the maps; up to 24 h they only feed panels, marked stale. EG931 is excluded from maps (NOAA feed frozen since 2024).");
        ImGui::TextDisabled("Data: GIRO/WWROF via kc2g + eSWua/INGV");
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip(
            "Attribution per KC2G's acknowledgment page and GIRO Rules of the Road: GIRO community data, CC BY-NC-SA 4.0 (non-commercial educational use). Please cite: Reinisch, B. W., and I. A. Galkin, Global ionospheric radio observatory (GIRO), Earth, Planets and Space, 63, 377-381, doi:10.5047/eps.2011.03.001, 2011.");
    } else if (impl->ionSourceMode.load() == Impl::SRC_MODEL) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Source: model (no live GIRO/kc2g)");
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip(
            "Neither GIRO (lgdc) nor the kc2g failover has fresh station data: the maps and the HF guide fall back to the climatological model / global medians (M5 net).");
    }
    {
        int nDias = impl->dias.getStationCount();
        if (impl->dias.isFetching()) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Fetching DIAS...");
        } else if (nDias > 0) {
            ImGui::Text("DIAS: %d pts (EU)", nDias);
        } else if (!impl->dias.getErrorMessage().empty()) {
            ImGui::TextDisabled("DIAS: %s", impl->dias.getErrorMessage().c_str());
        } else {
            ImGui::TextDisabled("DIAS: waiting for data...");
        }
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("DIAS Ionostream (NOA Athens): full EU profiles over plain HTTPS (no FTP). Fills stations missing in GIRO; GIRO wins ties. EB040 does not flow here (verified).");
    }

    // Tooltip + hover/pin: nearest station under the cursor on the globe.
    // Click (no drag) on a station pins it; click on empty space unpins.
    // The Radio Propagation panel shows the pinned station, else the hovered one.
    ImGuiIO& io2 = ImGui::GetIO();
    int hitIdx = -1;
    double curMX = 0.0, curMY = 0.0;
    if (!io2.WantCaptureMouse && impl->ionosondeLayer.isVisible() && impl->ionosondeLayer.getStationCount() > 0) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        curMX = mx; curMY = my;
        float ndcX = (float)(2.0 * mx / winW - 1.0);
        float ndcY = (float)(1.0 - 2.0 * my / winH);
        hitIdx = impl->ionosondeLayer.hitTest(ndcX, ndcY, view, proj, glm::radians(12.0f));
        if (hitIdx >= 0) {
            const StationPoint* sp = impl->ionosondeLayer.getSample(hitIdx);
            if (sp) {
                ImGui::BeginTooltip();
                ImGui::Text("%s (%s)", sp->name.c_str(), sp->code.c_str());
                ImGui::Text("lat %.2f  lon %.2f", sp->latDeg, sp->lonDeg);
                ImGui::Text("foF2 %.2f MHz  MUF %.1f MHz", sp->foF2, sp->muf);
                ImGui::Text("hmF2 %.0f km  NmF2 %.2e m^-3", sp->hmF2, sp->nmF2);
                ImGui::Text("foE %s  foEs %s  fmin %s",
                    giroFmt(sp->foE, 2).c_str(), giroFmt(sp->foEs, 2).c_str(), giroFmt(sp->fmin, 2).c_str());
                ImGui::Text("B0 %s km  B1 %s",
                    giroFmt(sp->B0, 0).c_str(), giroFmt(sp->B1, 1).c_str());
                // M4R-A: fila en banda stale (kc2g 6-24 h / EG931) — marca.
                if (sp->stale && sp->valid)
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                        "stale row (>6 h; maps exclude it)");
                // M4 (P4): fuera de la lista prioritaria -> catalogo visible
                // sin polling (dropdown): avisarlo en el tooltip (en modo
                // kc2g TODAS las filas mostradas traen dato del failover).
                if (!impl->giro.isPolled(sp->code) &&
                    impl->ionSourceMode.load() != Impl::SRC_KC2G_FALLBACK)
                    ImGui::TextDisabled("not polled (catalog only)");
                // Sparkline foF2 del historial real (paso 5 min; M4: hasta 6 h).
                // Busca en GIRO y DIAS.
                {
                    std::vector<float> fh;
                    for (const auto& s : impl->giro.copySamples()) {
                        if (s.code != sp->code) continue;
                        for (const auto& h : s.history) fh.push_back(h.foF2);
                        break;
                    }
                    if (fh.size() < 2) {
                        for (const auto& s : impl->dias.copySamples()) {
                            if (s.code != sp->code) continue;
                            for (const auto& h : s.history) fh.push_back(h.foF2);
                            break;
                        }
                    }
                    if (fh.size() >= 2) {
                        float mn = fh[0], mx = fh[0];
                        for (float v : fh) { mn = fminf(mn, v); mx = fmaxf(mx, v); }
                        if (mx - mn < 0.5f) { mx += 0.25f; mn -= 0.25f; }
                        ImGui::Text("foF2 %.1fh", (fh.size() - 1) * 5.0f / 60.0f);
                        ImGui::PlotLines("##fof2hist", fh.data(), (int)fh.size(), 0, nullptr, mn, mx, ImVec2(180, 36));
                    }
                }
                ImGui::TextDisabled("Click to pin this station");
                ImGui::EndTooltip();
            } else {
                hitIdx = -1;
            }
        }
    }
    impl->hoveredStation = hitIdx;
    {
        bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (leftDown && !impl->mouseLeftPrev) {
            impl->mouseDownX = curMX; impl->mouseDownY = curMY;
        }
        if (!leftDown && impl->mouseLeftPrev && !io2.WantCaptureMouse) {
            double dx = curMX - impl->mouseDownX, dy = curMY - impl->mouseDownY;
            if (dx * dx + dy * dy < 36.0 && impl->ionosondeLayer.isVisible()) {
                if (hitIdx >= 0) {
                    const StationPoint* sp = impl->ionosondeLayer.getSample(hitIdx);
                    if (impl->hfPickRx) {
                        // M8 (plan 11.4): «Pick RX...» — el click fija el RX del
                        // circuito (consume el click, no toca el pin).
                        if (sp) {
                            impl->hfRxCode = sp->code;
                            impl->hfCircuitDirty = true;
                        }
                        impl->hfPickRx = false;
                    } else if (sp && sp->code == impl->pinnedCode) {
                        impl->pinnedCode.clear();  // toggle off
                    } else if (sp) {
                        impl->pinnedCode = sp->code;
                    }
                } else if (impl->hfPickRx) {
                    impl->hfPickRx = false;   // click al vacio: cancela el pick (pin intacto)
                } else {
                    impl->pinnedCode.clear();
                }
            }
        }
        impl->mouseLeftPrev = leftDown;
    }

    // ---- Viento solar (Fase 4) ----
    ImGui::Separator();
    auto windB = impl->getWind();
    static const SolarWindSample kNoWind{};
    static const std::vector<SolarWindHistoryPoint> kNoBz{};
    static const std::string kNoErr{};
    const SolarWindSample& swl = windB ? windB->latest : kNoWind;
    const bool windSim = windB ? windB->simulated : true;
    const std::string& windErr = windB ? windB->error : kNoErr;
    const auto& bzh = windB ? windB->bzHist : kNoBz;
    bool showSW = impl->swArrowsVisible;
    if (ImGui::Checkbox("Solar wind", &showSW)) {
        impl->swArrowsVisible = showSW;
        impl->solarWindLayer.setVisible(showSW);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("3D arrows of the solar flow (NOAA RTSW). Direction subsolar -> antisolar, length follows speed.");
    const char* swModes[] = { "color: velocity", "color: Bz (red N / blue S)" };
    if (ImGui::Combo("Arrow color", &impl->swArrowMode, swModes, 2)) {
        impl->solarWindLayer.updateArrowData(swl.speed, swl.bz, impl->sunDirection, impl->swArrowMode);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Arrow color: by speed (blue->red) or by Bz (red=north, blue=south/geoeffective).");
    if (ImGui::SliderFloat("Arrow scale", &impl->swArrowScale, 0.2f, 4.0f)) {
        impl->solarWindLayer.setArrowScale(impl->swArrowScale);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Arrow length multiplier. Visual only.");
    ImGui::Text("Speed: %.0f km/s", swl.speed);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Solar wind speed (NOAA RTSW wind_1m, proton_speed). Calm ~350-450 km/s, CME >600.");
    ImGui::Text("Density: %.1f cm^-3", swl.density);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Proton density (NOAA RTSW wind_1m). Rises in sheaths/CMEs.");
    ImVec4 bzCol = swl.bz >= 0 ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 0.5f, 1.0f, 1.0f);
    ImGui::TextColored(bzCol, "Bz: %+.1f nT  (%s)", swl.bz, swl.bz >= 0 ? "north" : "south");
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("IMF Bz GSM (NOAA RTSW mag_1m). Southward (negative) means reconnection; sustained < -10 nT likely storms.");
    ImGui::ProgressBar(glm::clamp((swl.speed - 200.0f) / 800.0f, 0.0f, 1.0f), ImVec2(0, 0), "speed");
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Speed normalized to 200-1000 km/s.");
    if (swl.valid && windSim)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "(simulated: offline)");
    else if (swl.valid && !windSim)
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "(NOAA live)");
    if (!windErr.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", windErr.c_str());

    // Mini-grafica temporal de Bz (ultimas 24h).
    std::vector<float> bzVals;
    for (const auto& h : bzh) bzVals.push_back(h.value);
    if (!bzVals.empty()) {
        ImGui::Text("Bz last 24h");
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Bz history ~24h (NOAA RTSW). Dips below -10 nT mean a possible geomagnetic storm.");
        ImGui::PlotLines("Bz", bzVals.data(), (int)bzVals.size(), 0, nullptr, -15.0f, 15.0f, ImVec2(190, 40));
    }

    // ---- Faraday rotation (Fase 5): vectores 3D GL_LINES sobre la esfera ----
    ImGui::Separator();
    bool showFar = impl->faradayVisible;
    if (ImGui::Checkbox("Faraday rotation", &showFar)) {
        impl->faradayVisible = showFar;
        impl->faradayLayer.setVisible(showFar);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Rotation of the polarization plane by the ionosphere (deg at 100 MHz). Stronger by day and near the equator. Matters for HF/linear satellites.");
    const char* farModes[] = { "length: value", "length: fixed + color" };
    if (ImGui::Combo("Faraday mode", &impl->faradayMode, farModes, 2)) {
        static const GridData kNoGrid{};
        const GridData& tecg = impl->lastTec ? impl->lastTec->grid : kNoGrid;
        impl->faradayLayer.updateData(tecg, 15.0f,
                                      impl->sunDirection, impl->faradayMode);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Vector mode: length follows the angle, or fixed length with diverging color only (blue=low, red=high).");
    if (ImGui::SliderFloat("Faraday scale", &impl->faradayScale, 0.2f, 4.0f)) {
        impl->faradayLayer.setScale(impl->faradayScale);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Vector length multiplier. Visual only.");
    ImGui::Text("Vectors: %d", impl->faradayLayer.getVectorCount());
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Number of vectors drawn (7x12 grid).");

    // ---- Aurora oval (NOAA Ovation): green overlay, both hemispheres ----
    ImGui::Separator();
    bool showAur = impl->auroraVisible;
    if (ImGui::Checkbox("Aurora oval", &showAur)) {
        impl->auroraVisible = showAur;
        impl->auroraLayer.setVisible(showAur);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("NOAA Ovation auroral oval forecast (~30-90 min ahead). Green shell at ~110 km over both poles. Brightens and expands toward the equator during storms (high AE/Kp).");
    if (ImGui::SliderFloat("Aurora opacity", &impl->aurOpacity, 0.0f, 1.5f)) {
        impl->aurOpacity = glm::clamp(impl->aurOpacity, 0.0f, 1.5f);
        impl->auroraLayer.setOpacity(impl->aurOpacity);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Opacity of the aurora overlay. Visual only.");
    auto aurB = impl->getAurora();
    if (aurB && aurB->grid.valid) {
        float amn, amx;
        impl->auroraLayer.getDisplayRange(amn, amx);
        ImGui::Text("Ovation max: %.0f", amx);
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Peak auroral intensity in the current Ovation forecast (arbitrary units, higher = brighter oval).");
    } else if (aurB && !aurB->error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Aurora: %s", aurB->error.c_str());
    } else {
        ImGui::TextDisabled("Aurora: waiting for data...");
    }

    // (Release-prep: cartoon solar retirado — 6 manchas fijas simuladas;
    // el dato real vive en la ventana Sun M11.)

    ImGui::Separator();
    ImGui::Text("Sun: %.2f, %.2f, %.2f", impl->sunDirection.x, impl->sunDirection.y, impl->sunDirection.z);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Subsolar direction (computed). Defines day/night and the terminator.");
    {
        // Reloj verificador dia/noche: UTC + punto subsolar en el marco del globo.
        std::time_t nowT = std::time(nullptr);
        std::tm g;
#ifdef _WIN32
        gmtime_s(&g, &nowT);
#else
        gmtime_r(&nowT, &g);
#endif
        char cbuf[32] = "--:--:--";
        strftime(cbuf, sizeof(cbuf), "%H:%M:%S UTC", &g);
        glm::vec3 sd = glm::normalize(impl->sunDirection);
        float slat = glm::degrees(asinf(glm::clamp(sd.y, -1.0f, 1.0f)));
        float slon = glm::degrees(atan2f(sd.z, -sd.x));
        ImGui::Text("%s  sub %+.1f, %+.1f", cbuf, slat, slon);
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("UTC clock and subsolar point (lat, lon). Sanity check: at 12:00 UTC lon ~0 (Greenwich noon), at 00:00 UTC lon ~180. If the terminator disagrees, the sun math is wrong.");
    }
    ImGui::Separator();
    auto tecStatusB = impl->getTec();
    if (tecStatusB && tecStatusB->grid.valid) {
        if (tecStatusB->fromCache && !tecStatusB->simulated) {
            char tcbuf[16] = "--:--";
            {
                std::time_t tt = (std::time_t)tecStatusB->grid.timestamp;
                std::tm gg;
#ifdef _WIN32
                gmtime_s(&gg, &tt);
#else
                gmtime_r(&tt, &gg);
#endif
                strftime(tcbuf, sizeof(tcbuf), "%H:%M", &gg);
            }
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "TEC: cache %s UTC (%dx%d)",
                tcbuf, tecStatusB->grid.width, tecStatusB->grid.height);
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Cached GloTEC grid from disk (M10): real data from a previous session. Live download replaces it automatically; frames older than tecCacheMaxAgeH feed replay only.");
        } else if (tecStatusB->simulated) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "TEC: %dx%d (MOCK offline)",
                tecStatusB->grid.width, tecStatusB->grid.height);
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Simulated diurnal pattern (EsaAdapter): GloTEC unreachable. Switches to real data automatically when the download succeeds.");
        } else {
            char tgbuf[16] = "--:--";
            {
                std::time_t tt = (std::time_t)tecStatusB->grid.timestamp;
                std::tm gg;
#ifdef _WIN32
                gmtime_s(&gg, &tt);
#else
                gmtime_r(&tt, &gg);
#endif
                strftime(tgbuf, sizeof(tgbuf), "%H:%M", &gg);
            }
            ImGui::Text("TEC: %dx%d GloTEC %s (+%.0fs)", tecStatusB->grid.width, tecStatusB->grid.height, tgbuf,
                fmax(0.0, difftime(std::time(nullptr), (std::time_t)tecStatusB->grid.timestamp)));
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Real GloTEC grid (NOAA SWPC) with its data time (UTC) and age ticking every second.");
            ImGui::Text("Source: NOAA SWPC GloTEC");
        }
    } else {
        ImGui::TextDisabled("TEC: waiting for data...");
    }
    ImGui::End();

    drawSunPanel();  // M11: ventana Sun (disco SDO + ARs)

    // ---- Space Weather HUD (indices NOAA SWPC, Fase 1) ----
    auto idxB = impl->getIndices();
    static const IndexSnapshot kNoSnap{};
    static const std::vector<IndexHistoryPoint> kNoKp{}, kNoDst{}, kNoF107{}, kNoSsn{};
    const IndexSnapshot& sw = idxB ? idxB->snap : kNoSnap;
    ImGui::SetNextWindowPos(ImVec2((float)winW - 220, 280), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(210, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Space Weather");
    ImGui::TextWrapped("Kp / Ap / Dst / F10.7 / SSN  (NOAA SWPC)");
    HelpMarker("Live space weather indices (NOAA SWPC): Kp geomagnetic storms 0-9, Dst equatorial ring current, F10.7 solar flux, Ap planetary index, SSN sunspots. Any field offline falls back to mock.");
    if (sw.simulated)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "(simulated: offline)");
    else
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "(NOAA live)");

    // Kp medidor con color G-scale
    int gScale = (int)std::floor(sw.kp);
    ImVec4 kpCol(0.3f, 0.9f, 0.3f, 1.0f);
    if (sw.kp >= 5)       kpCol = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
    if (sw.kp >= 7)       kpCol = ImVec4(0.9f, 0.4f, 0.0f, 1.0f);
    if (sw.kp >= 8)       kpCol = ImVec4(0.9f, 0.1f, 0.1f, 1.0f);
    ImGui::TextColored(kpCol, "Kp: %.1f  (G%d)", sw.kp, gScale);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Planetary Kp 0-9 (3h). >=5 G1 storm, >=7 strong G3, >=8 severe G4. Degrades HF and GPS.");
    ImGui::SameLine();
    ImGui::ProgressBar(sw.kp / 9.0f, ImVec2(0, 0), "");
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Kp normalized 0-9.");

    // Dst con umbrales de tormenta
    ImVec4 dstCol(0.3f, 0.9f, 0.3f, 1.0f);
    if (sw.dst < -50)  dstCol = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
    if (sw.dst < -100) dstCol = ImVec4(0.9f, 0.1f, 0.1f, 1.0f);
    ImGui::TextColored(dstCol, "Dst: %.0f nT", sw.dst);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Dst: ring current (nT). <-50 moderate, <-100 intense, <-200 extreme. Drops in the storm main phase.");

    ImGui::Text("F10.7: %.0f SFU", sw.f107);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Solar flux at 10.7 cm (SFU). EUV proxy: <80 minimum, ~100 moderate, >150 high. Raises MUF/TEC.");
    ImGui::Text("Ap: %.0f nT", sw.ap);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Daily planetary Ap (nT, from Kp). <10 quiet, >30 active, >50 storm.");
    ImGui::Text("SSN: %.0f", sw.ssn);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Sunspot Number. Cycle 25 peak ~100-150. Higher SSN means higher MUF/FOT.");
    {
        auto xB = impl->getXray();
        if (xB && xB->latest.valid) {
            const XraySample& xr = xB->latest;
            ImGui::Text("X-ray: %c%.1f (%.0e W/m2)", xr.fclass, xr.fsub, (double)xr.flux);
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("GOES X-ray flux, 0.1-0.8 nm band (dayside D-layer driver). M/X flares switch on daytime absorption on low bands.");
        } else {
            ImGui::TextDisabled("X-ray: waiting for data...");
        }
    }

    ImGui::Separator();
    ImGui::Text("Trends");
    HelpMarker("Kp/Dst: recent intraday samples (1-min cadence). F10.7: 45-day daily outlook (day 0 = today). SSN: last 30 daily observed.");
    std::vector<float> kpVals;   for (auto& p : (idxB ? idxB->kp : kNoKp))   kpVals.push_back(p.value);
    std::vector<float> dstVals;  for (auto& p : (idxB ? idxB->dst : kNoDst))  dstVals.push_back(p.value);
    std::vector<float> f107Vals; for (auto& p : (idxB ? idxB->f107 : kNoF107)) f107Vals.push_back(p.value);
    std::vector<float> ssnVals;  for (auto& p : (idxB ? idxB->ssn : kNoSsn))  ssnVals.push_back(p.value);
    if (!kpVals.empty())   ImGui::PlotLines("Kp",   kpVals.data(),   (int)kpVals.size(),   0, nullptr, 0.0f, 9.0f, ImVec2(190, 30));
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Kp recent hours (estimated_kp, 0-9).");
    if (!dstVals.empty())  ImGui::PlotLines("Dst",  dstVals.data(),  (int)dstVals.size(),  0, nullptr, -80.0f, 10.0f, ImVec2(190, 30));
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Dst recent hours (nT). Valleys mark storm passages.");
    if (!f107Vals.empty()) ImGui::PlotLines("F10.7", f107Vals.data(), (int)f107Vals.size(), 0, nullptr, 0.0f, 250.0f, ImVec2(190, 30));
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("F10.7 daily: 45-day outlook, day 0 = today (SFU).");
    if (!ssnVals.empty())  ImGui::PlotLines("SSN",  ssnVals.data(),  (int)ssnVals.size(),  0, nullptr, 0.0f, 300.0f, ImVec2(190, 30));
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("SSN daily observed, last 30 days.");

    if (!sw.error.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", sw.error.c_str());
    }
    ImGui::End();

    // ---- Radio Propagation HUD (Fase 5): MUF/LUF/FOT + S4/TAI/AE/Faraday/Spread-F ----
    // Source: pinned station if set, else hovered station, else global median.
    auto radioB = impl->getRadio();
    static const RadioPropSample kNoRp{};
    static const std::vector<RadioPropHistoryPoint> kNoAe{}, kNoS4{};
    const RadioPropSample& rp = radioB ? radioB->latest : kNoRp;
    const bool rpSim = radioB ? radioB->simulated : true;
    const bool rpGiro = radioB ? radioB->usedGiro : false;
    static const std::string kNoErrRp{};
    const std::string& rpErr = radioB ? radioB->error : kNoErrRp;
    const auto& rpAeH = radioB ? radioB->aeHist : kNoAe;
    const auto& rpS4H = radioB ? radioB->s4Hist : kNoS4;
    const StationPoint* rpStation = nullptr;
    bool rpPinned = false;
    {
        int n = impl->ionosondeLayer.getStationCount();
        if (!impl->pinnedCode.empty()) {
            for (int i = 0; i < n; i++) {
                const StationPoint* c = impl->ionosondeLayer.getSample(i);
                if (c && c->code == impl->pinnedCode) { rpStation = c; rpPinned = true; break; }
            }
            if (!rpStation) impl->pinnedCode.clear();  // station gone after refresh
        }
        if (!rpStation && impl->hoveredStation >= 0 && impl->hoveredStation < n) {
            rpStation = impl->ionosondeLayer.getSample(impl->hoveredStation);
        }
    }
    // Per-station values (real epoch for the diurnal term).
    double rpEpoch = (double)std::time(nullptr);
    float rpKp = currentKp();
    // M7 (plan 10): flujo X-ray del circuito (guard del patron del flare
    // del volumen) para el termino SID del LUF y el aviso de fadeout.
    float rpFlux = (impl->lastXray && impl->lastXray->latest.valid)
        ? impl->lastXray->latest.flux : 0.0f;
    float stMuf = 0, stFot = 0, stLuf = 0, stS4 = 0, stFar = 0, stSpF = 0;
    bool stHasData = false;
    if (rpStation) {
        stHasData = rpStation->valid && rpStation->muf > 0.0f;
        if (stHasData) {
            stMuf = rpStation->muf;
            stFot = RadioPropagationAdapter::deriveFOT(stMuf);
            stLuf = RadioPropagationAdapter::deriveLUF(stMuf, rpStation->foF2, rpStation->latDeg, rpStation->lonDeg, rpEpoch, rpFlux, rpKp);
            stS4 = RadioPropagationAdapter::deriveS4(rpStation->latDeg, rpStation->lonDeg, rpEpoch);
            stSpF = RadioPropagationAdapter::deriveSpreadF(rpStation->latDeg, rpStation->lonDeg, rpEpoch, rpKp);
            float tecu = 15.0f;
            if (rpStation->nmF2 > 0.0f) tecu = rpStation->nmF2 * 2e-11f;  // ~slab 200 km
            stFar = RadioPropagationAdapter::deriveFaraday(tecu, rpStation->latDeg);
        }
    }
    float showMuf = rpStation && stHasData ? stMuf : rp.muf;
    float showFot = rpStation && stHasData ? stFot : rp.fot;
    float showLuf = rpStation && stHasData ? stLuf : rp.luf;
    float showS4  = rpStation && stHasData ? stS4  : rp.s4;
    float showFaraday = rpStation && stHasData ? stFar : rp.faradayDeg;
    float showSpF = rpStation && stHasData ? stSpF : rp.spreadF;
    ImGui::SetNextWindowPos(ImVec2(10, 280), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220, 360), ImGuiCond_FirstUseEver);
    ImGui::Begin("Radio Propagation");
    ImGui::TextWrapped("MUF/LUF/FOT + S4/AE/TAI (GIRO+NOAA)");
    HelpMarker("HF guide: transmit between LUF and MUF, best at FOT. S4/AE/TAI/Faraday/Spread-F warn of degradation. Hover a GIRO station (or click to pin) for its values; otherwise the global median is shown.");
    if (rpStation) {
        ImGui::TextColored(rpPinned ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f) : ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
            "%s (%s) %s", rpStation->name.c_str(), rpStation->code.c_str(),
            rpPinned ? "[pinned]" : "[hover]");
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip(
            impl->ionSourceMode.load() == Impl::SRC_KC2G_FALLBACK
                ? "Station source: kc2g failover (foF2/MUF/hmF2 measured, redistributed by prop.kc2g.com; B0/B1 fall back to IRI defaults). Click the station again or empty space to unpin."
                : "Station source: GIRO getbest (foF2/MUF/hmF2/B0 measured, rest derived). Click the station again or empty space to unpin.");
        if (rpPinned && ImGui::SmallButton("Unpin")) impl->pinnedCode.clear();
        // M4R-A: fila del panel en banda stale (6-24 h) — marcada, honesta.
        if (rpStation->stale && rpStation->valid) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "stale (%.1f h old)",
                (rpEpoch - rpStation->timeUtc) / 3600.0);
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip(
                "kc2g failover time-filter: this row is older than 6 h, so it still feeds panels but stays out of the interpolated maps.");
        }
        if (!stHasData) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "No data yet for this station");
        // Perfil directo del Ebre (solo EB040): SAO crudo via FTP, sin GIRO.
        if (rpStation->code == "EB040") {
            const EbroSample eb = impl->ebro.getLatest();
            ImGui::Separator();
            ImGui::Text("Ebro direct (FTP SAO)");
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Raw SAO4 profile from Observatori de l'Ebre (5-min cadence), no GIRO intermediary. Same data as the fof2-monitor app.");
            if (eb.valid) {
                ImGui::Text("foF2 %s MHz  MUF %s  M(D) %s",
                    ebFmt(eb.foF2, 2).c_str(), ebFmt(eb.muf, 1).c_str(), ebFmt(eb.mD, 2).c_str());
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Direct scaled characteristics. M(D) = MUF/foF2.");
                ImGui::Text("foF1 %s  foE %s  foEs %s",
                    ebFmt(eb.foF1, 2).c_str(), ebFmt(eb.foE, 2).c_str(), ebFmt(eb.foEs, 2).c_str());
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Lower layers: F1/E critical frequencies and sporadic-E.");
                ImGui::Text("fmin %s  fxI %s",
                    ebFmt(eb.fmin, 2).c_str(), ebFmt(eb.fxI, 2).c_str());
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("fmin = lowest echo; fxI = extraordinary-wave critical frequency.");
                ImGui::Text("h'F %s  h'F2 %s  h'E %s  h'Es %s",
                    ebFmt(eb.hF, 0).c_str(), ebFmt(eb.hF2, 0).c_str(),
                    ebFmt(eb.hE, 0).c_str(), ebFmt(eb.hEs, 0).c_str());
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Virtual heights (km) from the ionogram trace.");
                ImGui::Text("hmF2 %s  B0 %s  B1 %s",
                    ebFmt(eb.hmF2, 0).c_str(), ebFmt(eb.B0, 0).c_str(), ebFmt(eb.B1, 1).c_str());
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("True-height profile: F2 peak height plus Chapman shape parameters B0 (thickness) and B1 (shape).");
                double ageMin = (rpEpoch - eb.timeUtc) / 60.0;
                ImGui::TextDisabled("%s (%.0f min ago)", eb.sourceFile.c_str(), ageMin);
                if (stHasData && !std::isnan(eb.muf)) {
                    ImGui::Text("GIRO MUF %s (d %+.1f)",
                        ebFmt(rpStation->muf, 1).c_str(), rpStation->muf - eb.muf);
                    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Cross-check: GIRO DIDBase MUF minus direct SAO MUF. Small delta = consistent autoscaling.");
                }
            } else if (impl->ebro.isFetching()) {
                ImGui::TextDisabled("Ebro FTP: fetching SAO...");
            } else {
                ImGui::TextDisabled("Ebro FTP: %s", impl->ebro.getErrorMessage().empty() ?
                    "waiting for data..." : impl->ebro.getErrorMessage().c_str());
            }
            ImGui::Separator();
        }
    } else {
        ImGui::TextDisabled("Global median - hover a station");
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("No station under the cursor. Hover a GIRO dot on the globe, or click it to pin, to see per-station values.");
    }
    // FASE B (P3): la mediana global rp.* sigue al pase de datos vivo
    // (worker, §5); en replay se etiqueta "live" — nunca ambigua. Los
    // valores por estacion (pinned/hover) si siguen al replay via gsAct.
    if (!rpStation && impl->replayMode)
        ImGui::Text("MUF: %.1f MHz (live)", showMuf);
    else
        ImGui::Text("MUF: %.1f MHz", showMuf);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip(rpStation ? "MUF(D): highest usable frequency at 3000 km (MHz), measured at this station. Above it there is no F2 reflection." : "MUF(D): highest usable frequency at 3000 km (MHz). Global GIRO median. Above it there is no F2 reflection.");
    ImGui::Text("FOT (0.85*MUF): %.1f MHz", showFot);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("FOT = 0.85*MUF (ITU). Optimum working frequency: reliable ~90% of the time. Transmit here.");
    ImGui::Text("LUF: %.1f MHz", showLuf);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("LUF: lowest usable frequency (MHz, ITU-style with daytime D-layer absorption + flare SID term, M7). Rises by day; during X-ray flares the D layer absorbs more and the LUF jumps (see the amber fadeout notice below).");
    // M7 (plan 10, P5 sellado): aviso ambar de fadeout cuando el termino de
    // flare del circuito supera 1 MHz-equivalente (~M1+). cosChi via densityDir
    // (theta = pi - lon, marco del motor — patron sellado M5) con el sol del
    // HUD (replay-coherente, M2). Solo con estacion pinned/hovered: el plan
    // habla del circuito actual.
    if (rpStation) {
        const float kPi = 3.14159265f;
        float phi = (90.0f - rpStation->latDeg) * kPi / 180.0f;
        float theta = kPi - rpStation->lonDeg * kPi / 180.0f;   // convencion del motor
        glm::vec3 rd(sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta));
        float cosChi = glm::dot(rd, glm::normalize(impl->sunDirection));
        // Trigger con la MISMA fuente de verdad del termino flare (P4):
        // flareAbsorption. dayFactor del panel = max(0, cosChi); deriveLUF usa
        // dayF lstH por dentro — caveat dayF-vs-cosChi del sello (ambos
        // monotonos en F, trigger advisory, sin falsa precision).
        if (RadioPropagationAdapter::flareAbsorption(rpFlux, fmaxf(0.0f, cosChi)) > 1.0f) {
            float dB = RadioPropagationAdapter::dRegionAbsorptionDb(cosChi, rpFlux, rpKp);
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
                "D-region fadeout: +%.1f dB", dB);
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("SID (sudden ionospheric disturbance): the ongoing X-ray flare enhances D-layer absorption and closes the lower HF bands. Model total absorption in dB (Sauer-Wilkinson simplified: flare + auroral Kp terms at this point). Fades out as the flare decays.");
        }
    }
    // S4 gauge 0..1
    ImVec4 s4Col(0.3f, 0.9f, 0.3f, 1.0f);
    if (showS4 > 0.3f) s4Col = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
    if (showS4 > 0.6f) s4Col = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
    ImGui::TextColored(s4Col, "S4: %.2f", showS4);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("S4: ionospheric scintillation index 0-1 (fading). <0.2 slight, >0.3 moderate (GPS/HF suffer), >0.6 severe. Peaks at night near the equator.");
    ImGui::ProgressBar(glm::clamp(showS4, 0.0f, 1.0f), ImVec2(0, 0), "scintillation");
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("S4 normalized 0-1.");
    // AE gauge 0..1000 nT (global index)
    ImVec4 aeCol(0.3f, 0.9f, 0.3f, 1.0f);
    if (rp.ae > 300) aeCol = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
    if (rp.ae > 700) aeCol = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
    ImGui::TextColored(aeCol, "AE: %.0f nT (global)", rp.ae);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("AE: auroral electrojet (nT, Kp proxy; authoritative: Kyoto WDC). <100 quiet, >300 substorm, >700 storm. Auroras and degraded polar HF. Global index, not per station.");
    ImGui::ProgressBar(glm::clamp(rp.ae / 1000.0f, 0.0f, 1.0f), ImVec2(0, 0), "auroral");
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("AE normalized 0-1000 nT.");
    ImGui::Text("TAI: %.1f / 3 (global)", rp.tai);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("TAI: TEC activity 0-3 (Kp proxy). 0 quiet, 3 heavily disturbed. Anticipates GPS errors. Global index, not per station.");
    ImGui::ProgressBar(glm::clamp(rp.tai / 3.0f, 0.0f, 1.0f), ImVec2(0, 0), "tec activity");
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("TAI normalized 0-3.");
    ImGui::Text("Faraday: %.0f deg", showFaraday);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip(rpStation ? "Faraday rotation at this station (deg at 100 MHz, one hop), from its NmF2-derived TEC. Rotates polarization; fading on linear/satellite antennas. See 3D vectors in Layers." : "Median Faraday rotation (deg at 100 MHz, one hop). Rotates polarization; fading on linear/satellite antennas. See 3D vectors in Layers.");
    ImGui::Text("Spread-F: %.0f %%", showSpF);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Spread-F: % occurrence of spread echoes in the ionogram. Nocturnal equatorial/auroral. Causes fading and unstable MUF. The map uses a diverging colormap.");
    ImGui::ProgressBar(glm::clamp(showSpF / 100.0f, 0.0f, 1.0f), ImVec2(0, 0), "spread-F");
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Spread-F 0-100%.");
    std::vector<float> aeVals; for (auto& p : rpAeH) aeVals.push_back(p.value);
    std::vector<float> s4Vals; for (auto& p : rpS4H) s4Vals.push_back(p.value);
    if (!aeVals.empty()) ImGui::PlotLines("AE", aeVals.data(), (int)aeVals.size(), 0, nullptr, 0.0f, 800.0f, ImVec2(190, 30));
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("AE 24h (nT). Peaks = auroral substorms.");
    if (!s4Vals.empty()) ImGui::PlotLines("S4", s4Vals.data(), (int)s4Vals.size(), 0, nullptr, 0.0f, 1.0f, ImVec2(190, 30));
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("S4 24h (0-1). Rises at night near the equator.");
    if (rp.valid && rpSim)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "(simulated: offline)");
    else if (rp.valid && rpGiro)   // M4R-A: la mediana puede venir del failover
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
            impl->ionSourceMode.load() == Impl::SRC_KC2G_FALLBACK ? "(kc2g real)" : "(GIRO real)");
    if (!rpErr.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", rpErr.c_str());
    ImGui::End();

    // ---- M8 (plan 11.4): panel «Circuit» (TX/RX + matriz de bandas) ----
    {
        ImGui::SetNextWindowPos(ImVec2(10, 650), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(260, 400), ImGuiCond_FirstUseEver);
        const bool hfOpen = ImGui::Begin("Circuit");
        impl->hfPanelOpen = hfOpen;
        if (hfOpen) {
            ImGui::TextWrapped("HF multi-hop circuit (MUF/FOT/LUF + band matrix)");
            HelpMarker("Great-circle path with N hops at virtual height h'. Hop MUF = foF2 at the reflection point times M(D) (Dudeney/ITU-R P.1240). Circuit LUF comes from the M7 D-region absorption grid (max over hops). Bands: open between LUF and FOT, marginal up to MUF, closed beyond. Drag the frequency slider or replay the timeline and the circuit follows.");
            if (ImGui::Checkbox("HF trace layer", &impl->hfTraceVisible))
                impl->hfCircuitDirty = true;   // encender pide grids frescos
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Draw the ray path as a camera-facing ribbon: green = hop open at the panel frequency, amber = marginal, red = closed. A brightness pulse travels along the arc.");

            // TX/RX: combos sobre las estaciones visibles (catalogo incluido).
            std::vector<const StationPoint*> hfSts;
            hfSts.reserve(impl->ionosondeLayer.samples_.size());
            for (const auto& hfs : impl->ionosondeLayer.samples_) hfSts.push_back(&hfs);
            std::sort(hfSts.begin(), hfSts.end(),
                      [](const StationPoint* a, const StationPoint* b) { return a->code < b->code; });
            std::vector<std::string> hfTxNames, hfRxNames;
            hfTxNames.push_back("(pinned / DB049)");
            hfRxNames.push_back("(default EG931)");
            int hfTxSel = 0, hfRxSel = 0;
            for (const StationPoint* hfs : hfSts) {
                hfTxNames.push_back(hfs->code + " " + hfs->name);
                hfRxNames.push_back(hfs->code + " " + hfs->name);
                if (hfs->code == impl->hfTxCode) hfTxSel = (int)hfTxNames.size() - 1;
                if (hfs->code == impl->hfRxCode) hfRxSel = (int)hfRxNames.size() - 1;
            }
            std::vector<const char*> hfTxItems, hfRxItems;
            for (const auto& hn : hfTxNames) hfTxItems.push_back(hn.c_str());
            for (const auto& hn : hfRxNames) hfRxItems.push_back(hn.c_str());
            if (ImGui::Combo("TX", &hfTxSel, hfTxItems.data(), (int)hfTxItems.size())) {
                impl->hfTxCode = (hfTxSel == 0) ? std::string() : hfSts[(size_t)(hfTxSel - 1)]->code;
                impl->hfCircuitDirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Transmit end. Default: the pinned station (click a station on the globe), else DB049 (Dourbes) as in the demo.");
            if (ImGui::Combo("RX", &hfRxSel, hfRxItems.data(), (int)hfRxItems.size())) {
                impl->hfRxCode = (hfRxSel == 0) ? std::string() : hfSts[(size_t)(hfRxSel - 1)]->code;
                impl->hfCircuitDirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Receive end. Default: EG931 (Fairford) as in the demo.");
            ImGui::SameLine();
            if (impl->hfPickRx) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.6f, 0.2f, 1.0f));
                if (ImGui::Button("Pick RX...")) impl->hfPickRx = false;
                ImGui::PopStyleColor();
            } else {
                if (ImGui::Button("Pick RX...")) impl->hfPickRx = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Next click on a station marker on the globe fixes RX (the pin is left untouched).");
            if (impl->hfPickRx)
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "pick mode: click a station...");
            if (ImGui::SliderFloat("Freq (MHz)", &impl->hfFreqMHz, 1.0f, 30.0f, "%.1f"))
                impl->hfCircuitDirty = true;
            ImGui::TextDisabled("ray height: 'Explode' x%.1f + 'Altitude' %+g km over h'",
                                 (double)impl->explodeFactor,
                                 (double)(impl->altKm - 300.0f));

            const HFCircuitResult& hfR = impl->hfTrace.result();
            if (!hfR.valid) {
                ImGui::TextDisabled("Waiting for TX/RX coordinates + grids...");
            } else {
                ImGui::Text("D %.0f km   %d hop(s)", hfR.distKm, hfR.nHops);
                ImGui::Text("h' %.0f km   elev %.1f deg", hfR.hVirtualKm, hfR.elevDeg);
                ImGui::Text("MUF %.1f   FOT %.1f   LUF %.1f MHz", hfR.mufMHz, hfR.fotMHz, hfR.lufMHz);
                // Matriz de bandas (plan 11.4, literales: 160..10 m).
                static const char* hfBands[10] = { "160 m", "80 m", "60 m", "40 m", "30 m",
                                                   "20 m", "17 m", "15 m", "12 m", "10 m" };
                static const float hfFreqs[10] = { 1.8f, 3.5f, 5.3f, 7.0f, 10.1f,
                                                   14.0f, 18.1f, 21.0f, 24.9f, 28.0f };
                for (int i = 0; i < 10; i++) {
                    ImGui::Text("%-6s", hfBands[i]);
                    ImGui::SameLine();
                    ImGui::Text("%5.1f MHz", hfFreqs[i]);
                    ImGui::SameLine();
                    const hfc::BandState hfb = hfc::bandState((double)hfFreqs[i],
                                                              hfR.lufMHz, hfR.fotMHz, hfR.mufMHz);
                    if (hfb == hfc::BAND_OPEN)
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "open");
                    else if (hfb == hfc::BAND_MARGINAL)
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "marginal");
                    else
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "closed");
                }
                if (impl->replayMode)
                    ImGui::TextDisabled("replay: circuit follows the epoch");
            }
        }
        ImGui::End();
        // Marcadores + etiquetas proyectadas TX/RX (plan 11.3: discos en
        // espacio de pantalla — proyeccion mundo->NDC->pixel del frame).
        const HFCircuitResult& hfR2 = impl->hfTrace.result();
        if (impl->hfTraceVisible && hfR2.valid) {
            auto hfMarker = [&](const char* label, double lat, double lon, const ImVec4& col) {
                const glm::vec3 wp = densityDir((float)lat, (float)lon);
                const glm::vec4 cp = proj * view * glm::vec4(wp, 1.0f);
                if (cp.w <= 0.0f) return;   // detras de la camara
                const float sx = (float)winW * (0.5f + 0.5f * cp.x / cp.w);
                const float sy = (float)winH * (0.5f - 0.5f * cp.y / cp.w);
                ImDrawList* dl = ImGui::GetBackgroundDrawList();
                dl->AddCircleFilled(ImVec2(sx, sy), 5.0f, ImGui::GetColorU32(col));
                dl->AddText(ImVec2(sx + 8.0f, sy - 7.0f), ImGui::GetColorU32(col), label);
            };
            hfMarker("TX", hfR2.txLatDeg, hfR2.txLonDeg, ImVec4(0.4f, 1.0f, 0.5f, 1.0f));
            hfMarker("RX", hfR2.rxLatDeg, hfR2.rxLonDeg, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
        }
    }

    // ---- Legend: active color scale of the spherical layer (units + range) ----
    {
        static const char* LEG_NAMES[12] = { "TEC", "foF2", "MUF", "hmF2", "NmF2", "LUF", "FOT", "Spread-F", "IRTAM foF2", "IRTAM hmF2", "IRTAM B0", "IRTAM B1" };
        static const char* LEG_UNITS[12] = { "TECU", "MHz", "MHz", "km", "e/m3", "MHz", "MHz", "%", "MHz", "km", "km", "[]" };
        int li = glm::clamp(impl->mapVariable, 0, 11);
        ImGui::SetNextWindowPos(ImVec2(10, (float)winH - 130), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(220, 110), ImGuiCond_FirstUseEver);
        ImGui::Begin("Legend");
        ImGui::Text("%s [%s] (%s)", LEG_NAMES[li], LEG_UNITS[li], impl->tecLayer.getColormapName().c_str());
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Active scale of the spherical color layer. It follows the Variable selector and the manual Colormap override.");
        // Hover-solo: aislar la entrada bajo el cursor (se aplica en el
        // render 3D del frame siguiente; None = todo normal).
        impl->legendSolo = App::Impl::SoloTarget::None;
        if (ImGui::IsItemHovered()) impl->legendSolo = App::Impl::SoloTarget::Main;
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Hover to isolate this layer (solo).");
        if (impl->peakShell.isVisible()) ImGui::TextDisabled("F2peak [logNe] (plasma)");
        if (ImGui::IsItemHovered() && impl->peakShell.isVisible()) impl->legendSolo = App::Impl::SoloTarget::Peak;
        if (impl->tecColShell.isVisible()) ImGui::TextDisabled("modelTEC [TECU] (viridis)");
        if (ImGui::IsItemHovered() && impl->tecColShell.isVisible()) impl->legendSolo = App::Impl::SoloTarget::Model;
        if (impl->volumeLayer.isVisible()) {
            ImGui::Text("Volume [%s] (%s)", impl->volumeMode == 1 ? "layer" : "logNe",
                        impl->volumeLayer.getColormapName().c_str());
            if (ImGui::IsItemHovered()) {
                impl->legendSolo = App::Impl::SoloTarget::Volume;
                ImGui::SetItemTooltip("Hover to isolate the volume. Mode Layers shows fixed D/E/F1/F2 colors.");
            }
        }
        if (impl->tecLayer.getGrid().valid && impl->tecLayer.isVisible()) {
            float mn, mx;
            impl->tecLayer.getDisplayRange(mn, mx);
            const std::vector<float>& cm = impl->tecLayer.getColormap();
            ImVec2 p = ImGui::GetCursorScreenPos();
            const float bw = 190.0f, bh = 14.0f;
            const int NST = 64;
            for (int i = 0; i < NST; i++) {
                int ci = (i * 255) / (NST - 1);
                ImU32 c = IM_COL32((int)(cm[ci * 3 + 0] * 255), (int)(cm[ci * 3 + 1] * 255),
                                   (int)(cm[ci * 3 + 2] * 255), 255);
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(p.x + bw * i / NST, p.y), ImVec2(p.x + bw * (i + 1) / NST, p.y + bh), c);
            }
            ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + bw, p.y + bh), IM_COL32(255, 255, 255, 90));
            ImGui::Dummy(ImVec2(bw, bh + 2.0f));
            auto fmtR = [](float v, char* b, size_t n) {
                if (v != 0.0f && (fabsf(v) >= 10000.0f || fabsf(v) < 0.01f)) snprintf(b, n, "%.2e", v);
                else snprintf(b, n, "%.2f", v);
            };
            char bmn[32], bmx[32];
            fmtR(mn, bmn, sizeof(bmn)); fmtR(mx, bmx, sizeof(bmx));
            ImGui::TextUnformatted(bmn);
            float tw = ImGui::CalcTextSize(bmx).x;
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - tw);
            ImGui::TextUnformatted(bmx);
            // Timestamp del dato + modo (como las imagenes 1 y 4) + edad tickeando.
            double dts = (double)std::time(nullptr);
            bool isReplayTs = false;
            {
                std::lock_guard<std::mutex> tlk(impl->tecHistMutex);
                if (impl->replayMode && impl->tecHist.size() >= 2) {
                    size_t ri0 = (size_t)impl->replayPos;
                    if (ri0 >= impl->tecHist.size() - 1) ri0 = impl->tecHist.size() - 2;
                    double rf = impl->replayPos - (double)ri0;
                    dts = impl->tecHist[ri0].timestamp * (1.0 - rf) + impl->tecHist[ri0 + 1].timestamp * rf;
                    isReplayTs = true;
                } else if (!impl->tecHist.empty()) {
                    dts = impl->tecHist.back().timestamp;
                }
            }
            char dbuf[48] = "--:--:--";
            {
                std::time_t tt = (std::time_t)dts;
                std::tm g;
#ifdef _WIN32
                gmtime_s(&g, &tt);
#else
                gmtime_r(&tt, &g);
#endif
                strftime(dbuf, sizeof(dbuf), "%H:%M:%S", &g);
            }
            double ageSec = difftime(std::time(nullptr), (std::time_t)dts);
            if (isReplayTs) {
                ImGui::Text("FRAME %s REPLAY", dbuf);
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Frame time (UTC) of the displayed replay grid. Scrub or play to move it.");
            } else if (ageSec > 1800.0) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "DATA %s LIVE (stale %.0fm)", dbuf, ageSec / 60.0);
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Data older than 30 min: source unreachable, showing the last valid grid.");
            } else {
                ImGui::Text("DATA %s LIVE (+%.0fs)", dbuf, ageSec < 0.0 ? 0.0 : ageSec);
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Data time (UTC) of the displayed TEC grid + its age ticking every second. New frame every ~5 min (or 10 min from preload).");
            }
        } else {
            ImGui::TextDisabled(impl->tecLayer.isVisible() ? "waiting for data..." : "color layer hidden");
        }
        ImGui::End();
    }

    // ---- Altitude ruler (Fase 1): 60-700 km + pico bajo pin/hover ----
    {
        ImGui::SetNextWindowPos(ImVec2((float)winW - 345.0f, 290.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(112, 300), ImGuiCond_FirstUseEver);
        ImGui::Begin("Altitude");
        // Ancla: pineada, si no hover.
        const StationPoint* anchor = nullptr;
        {
            int n = impl->ionosondeLayer.getStationCount();
            if (!impl->pinnedCode.empty()) {
                for (int i = 0; i < n; i++) {
                    const StationPoint* c = impl->ionosondeLayer.getSample(i);
                    if (c && c->code == impl->pinnedCode) { anchor = c; break; }
                }
            }
            if (!anchor && impl->hoveredStation >= 0 && impl->hoveredStation < n)
                anchor = impl->ionosondeLayer.getSample(impl->hoveredStation);
        }
        float peakAlt = -1.0f, peakLog = 0.0f;
        if (anchor && anchor->valid && impl->lastVolume.valid) {
            for (int a = 0; a < impl->lastVolume.altRes; a++) {
                float h = volumeAltKm(a, impl->lastVolume.altRes,
                                      impl->lastVolume.altMinKm, impl->lastVolume.altMaxKm);
                float v = sampleVolume(impl->lastVolume, anchor->latDeg, anchor->lonDeg, h);
                float lg = 8.0f + 4.5f * v;
                if (lg > peakLog) { peakLog = lg; peakAlt = h; }
            }
        }
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float trackH = 200.0f, trackX = p.x + 24.0f;
        const float A0 = 60.0f, A1 = 700.0f;
        auto yOf = [&](float alt) {
            return p.y + trackH * (1.0f - (alt - A0) / (A1 - A0));
        };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(trackX, p.y), ImVec2(trackX, p.y + trackH), IM_COL32(140, 140, 140, 255), 2.0f);
        struct Mark { float alt; const char* label; };
        static const Mark marks[] = { {75.0f, "D"}, {110.0f, "E"}, {180.0f, "F1"}, {300.0f, "F2"} };
        for (const auto& m : marks) {
            float y = yOf(m.alt);
            dl->AddLine(ImVec2(trackX - 5.0f, y), ImVec2(trackX + 5.0f, y), IM_COL32(140, 140, 140, 255), 1.0f);
            dl->AddText(ImVec2(trackX + 8.0f, y - 7.0f), IM_COL32(170, 170, 170, 255), m.label);
        }
        if (peakAlt > 0.0f) {
            float y = yOf(peakAlt);
            dl->AddTriangleFilled(ImVec2(trackX - 9.0f, y - 5.0f), ImVec2(trackX - 9.0f, y + 5.0f),
                                  ImVec2(trackX - 2.0f, y), IM_COL32(255, 220, 80, 255));
            char pb[32];
            snprintf(pb, sizeof(pb), "%.0f km", peakAlt);
            dl->AddText(ImVec2(trackX + 8.0f, yOf(peakAlt) - 24.0f), IM_COL32(255, 220, 80, 255), pb);
        } else {
            dl->AddText(ImVec2(p.x, p.y + trackH + 4.0f), IM_COL32(120, 120, 120, 255), "hover a station");
        }
        ImGui::Dummy(ImVec2(100, trackH + 22.0f));
        ImGui::End();
    }

    // ---- Limb zoom (Fase 2): lupa del limbo este con el propio framebuffer ----
    if (impl->pipVisible) {
        ImGui::SetNextWindowPos(ImVec2((float)winW - 440.0f, (float)winH - 270.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 230), ImGuiCond_FirstUseEver);
        ImGui::Begin("Limb x3");
        glm::vec3 vd = glm::normalize(-camPos);
        glm::vec3 rr = glm::cross(vd, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec2 uvc(-1.0f, -1.0f);
        if (glm::length(rr) > 1e-4f) {
            glm::vec3 limb = glm::normalize(rr);   // punto del limbo este
            glm::vec4 c = proj * view * glm::vec4(limb, 1.0f);
            if (c.w > 0.0f) {
                glm::vec3 ndc = glm::vec3(c) / c.w;
                if (ndc.x > -1.0f && ndc.x < 1.0f && ndc.y > -1.0f && ndc.y < 1.0f)
                    uvc = glm::vec2(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f);
            }
        }
        if (uvc.x >= 0.0f) {
            // Region ~x3 sin distorsion (mismo aspecto que el display).
            float hw = 0.055f;
            float hh = hw * (320.0f / 190.0f) / ((float)winW / (float)winH);
            float x0 = glm::clamp(uvc.x - hw, 0.0f, 1.0f);
            float x1 = glm::clamp(uvc.x + hw, 0.0f, 1.0f);
            float y0 = glm::clamp(uvc.y - hh, 0.0f, 1.0f);
            float y1 = glm::clamp(uvc.y + hh, 0.0f, 1.0f);
            ImGui::Image((ImTextureID)(intptr_t)impl->sceneFBO.colorTexture,
                         ImVec2(320, 190), ImVec2(x0, y1), ImVec2(x1, y0));   // limb fix: sin espejo V (uvc ya es bottom-up; FBO fila 0 = abajo)
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("East limb magnified from the live framebuffer (scene without bloom). Watch shells stack edge-on.");
        } else {
            ImGui::TextDisabled("limb behind camera");
        }
        ImGui::End();
    }
    {
        ImGui::SetNextWindowPos(ImVec2((float)winW * 0.5f - 190.0f, (float)winH - 96.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 104), ImGuiCond_FirstUseEver);   // M4/P5: +1 linea de estado GIRO
        ImGui::Begin("Timeline");
        int nfr = 0;
        { std::lock_guard<std::mutex> tlk(impl->tecHistMutex); nfr = (int)impl->tecHist.size(); }
        // M4 (P5a): estado GIRO real para la linea de estado y el rango.
        GiroAdapter::GiroNetStatus gs = impl->giro.getNetStatus();
        if (!impl->replayMode) {
            if (impl->histLoading.load()) {
                ImGui::Text("LIVE  (loading history %d/%d...)",
                    impl->histDone.load(), impl->histTotal.load());
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Downloading past GloTEC frames in the background for instant replay.");
            } else {
                ImGui::Text("LIVE  (%d frames)", nfr);
            }
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Live mode shows the latest TEC grid. Frames (live + preloaded history, up to 432) feed the replay. GIRO station history: %.1f h.", gs.histHours);
            if (nfr >= 2) {
                if (ImGui::Button("Replay session")) {
                    impl->replayMode = true;
                    impl->replayPlaying = true;
                    // M-irtam-replay-A: el cursor nace en el frame TEC mas
                    // viejo (equivale al antiguo replayPos = 0).
                    const double nowUtc0 = (double)std::time(nullptr);
                    impl->replayEpoch = nowUtc0 - 345600.0;
                    {
                        std::lock_guard<std::mutex> tlk(impl->tecHistMutex);
                        if (!impl->tecHist.empty())
                            impl->replayEpoch =
                                impl->tecHist.front().timestamp;
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Enter replay: animates the stored TEC frames and the GIRO station history at the same time. Works with any Variable.");
            } else {
                ImGui::TextDisabled("Collecting frames... open Replay after 2+ fetches");
            }
        } else {
            if (ImGui::Button("Back to Live")) {
                impl->replayMode = false;
                impl->replayPlaying = false;
                applyColorLayer();  // refresh the live grid of the current variable
            }
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Exit replay and return to the live layer and previous variable.");
            ImGui::SameLine();
            if (ImGui::Button(impl->replayPlaying ? "Pause" : "Play")) {
                impl->replayPlaying = !impl->replayPlaying;
            }
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Play/pause the animation (loops over stored frames).");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SliderFloat("speed", &impl->replaySpeed, 0.5f, 8.0f, "%.1f fps");
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Replay speed in frames per second.");
            // M-irtam-replay-031 (toggle): checkbox de modo (marcado = union
            // 96 h, defecto R2) + recogida UNICA de rangos que alimenta
            // slider, etiqueta de modo y DATA (misma cuenta de locks).
            bool fullWin = !impl->replayLayerLoop;
            if (ImGui::Checkbox("Full 96 h window", &fullWin)) {
                impl->replayLayerLoop = !fullWin;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetItemTooltip("Union 96 h loop (comparative scrub across zones) or per-layer loop (dense replay of the active layer only). Without layer history, always union.");
            const double nowUtcR = (double)std::time(nullptr);
            irtamstate::LayerRanges lr;
            {
                std::lock_guard<std::mutex> tlk(impl->tecHistMutex);
                if (!impl->tecHist.empty()) {
                    lr.tecOldest = impl->tecHist.front().timestamp;
                    lr.tecNewest = impl->tecHist.back().timestamp;
                }
            }
            if (gs.histHours > 0.0) {
                lr.giroNewest = nowUtcR;
                lr.giroOldest = nowUtcR - gs.histHours * 3600.0;
            }
            // Bounds por modo (union por defecto; capa con fallback).
            double sloB = nowUtcR - irtamstate::kIrtamReplayWindowSec;
            double shiB = nowUtcR;
            std::string modeLabel("Window: union 96 h");
            if (impl->replayLayerLoop) {
                const irtamstate::LayerLoopRange llr =
                    irtamstate::layerLoopRange(impl->mapVariable, nowUtcR,
                                              lr);
                if (llr.valid) {
                    sloB = llr.oldest;
                    shiB = llr.newest;
                    const char* fam = (impl->mapVariable == 0)    ? "TEC"
                                      : (impl->mapVariable <= 7) ? "GIRO"
                                                                 : "IRTAM";
                    char mb[64];
                    std::snprintf(mb, sizeof(mb), "Loop: %s %.1f h", fam,
                                  (shiB - sloB) / 3600.0);
                    modeLabel = mb;
                } else {
                    modeLabel = "Loop: capa sin historia (unión)";
                }
            }
            ImGui::TextDisabled("%s", modeLabel.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetItemTooltip("Active loop mode and span (live family + hours).");
            // M-irtam-replay-A (ruling R1 Q6): slider de TIEMPO con bounds
            // dinamicos del modo (union [0,96]; capa [hMin,hMax] reales);
            // scrubbing-pauses y Play/speed se conservan.
            if (impl->replayEpoch <= 0.0) impl->replayEpoch = sloB;
            float hoursBack =
                (float)((nowUtcR - impl->replayEpoch) / 3600.0);
            const float hMin = (float)((nowUtcR - shiB) / 3600.0);
            const float hMax = (float)((nowUtcR - sloB) / 3600.0);
            if (ImGui::SliderFloat("hours back", &hoursBack, hMin, hMax,
                                   "%.1f h")) {
                impl->replayEpoch = nowUtcR - (double)hoursBack * 3600.0;
                impl->replayPlaying = false;  // scrubbing pauses
            }
            if (ImGui::IsItemHovered())
                ImGui::SetItemTooltip("Scrub time over the active loop range "
                                      "(pauses playback). TEC covers [T-72,T], "
                                      "IRTAM [T-96,T-72]; overlap at T-72. In "
                                      "per-layer mode bounds follow the layer "
                                      "real data (per-layer bounds may exceed "
                                      "72 h with inherited frames). Switching "
                                      "Variable re-clamps the cursor into the "
                                      "new bounds when needed (never jumps to live).");
            ImGui::SameLine();
            char tbuf[32] = "--:--";
            {
                std::time_t tt = (std::time_t)impl->replayEpoch;
                std::tm g;
#ifdef _WIN32
                gmtime_s(&g, &tt);
#else
                gmtime_r(&tt, &g);
#endif
                strftime(tbuf, sizeof(tbuf), "%H:%M UTC", &g);
            }
            ImGui::TextUnformatted(tbuf);
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Replay epoch (UTC).");
            // Zonas declaradas (ruling CORRECCION 2: el TimeBar lo declara).
            // M-irtam-replay-038 (familia 022): en modo por capa la etiqueta
            // Zone: deriva del mismo dato real (LayerRanges + banda IRTAM)
            // que Loop/DATA; en union la via union queda byte-identica.
            if (impl->replayLayerLoop) {
                ImGui::TextDisabled(
                    "Zone: %s",
                    irtamstate::perLayerZoneName(impl->replayEpoch, nowUtcR,
                                                lr));
                if (ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("Zone per-layer: sources with real "
                                          "data at the cursor (TEC/GIRO real "
                                          "ranges; IRTAM structural band "
                                          "[T-96,T-72]). The real span floats "
                                          "with cadence — 72 h is the nominal "
                                          "download window; frames older "
                                          "than T-72 h are inherited from "
                                          "disk, not downloaded. Titles "
                                          "report data availability at the "
                                          "cursor; HF route numbers (MUF/FOT/"
                                          "LUF) are interpolated usability "
                                          "(IDW + median floor + edge clamp), "
                                          "so their edges may not match.");
            } else {
                const irtamstate::ReplayZone rz = irtamstate::zoneForAge(
                    nowUtcR - impl->replayEpoch);
                ImGui::TextDisabled("Zone: %s", irtamstate::zoneName(rz));
                if (ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("Union window zones — [T-96,T-72] IRTAM-only (sampled lerp) · [T-72,T] TEC (+ GIRO 6 h).");
            }
            // M-irtam-replay-025 (ruling R2 RELOJ): hora-dato de la capa
            // ACTIVA con rangos reales (reutiliza la recogida de arriba).
            {
                const bool busy = impl->irtamSampleBusy.load();
                const irtamstate::LayerDataTime ldt =
                    irtamstate::layerDataTime(impl->mapVariable,
                                             impl->replayEpoch, nowUtcR, lr,
                                             busy);
                if (ldt.dataEpoch > 0.0) {
                    if (ldt.stale) {
                        ImGui::TextDisabled(
                            "DATA %s (stale)",
                            irtamstate::formatTovShort(ldt.dataEpoch).c_str());
                    } else {
                        ImGui::Text(
                            "DATA %s",
                            irtamstate::formatTovShort(ldt.dataEpoch).c_str());
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetItemTooltip("Hora-dato real de la capa activa (stale = clampeada al borde de su historia o worker en curso).");
                } else {
                    ImGui::TextDisabled("DATA sin dato en capa");
                }
            }
        }
        // M4 (P5a): linea de estado GIRO en ambos modos — rango real de replay
        // de estaciones y aviso de cortesia (rate-limit con reintento).
        // M4R-A (FASE A): la linea ES el badge de fuente (GIRO-live /
        // kc2g-fallback / model); en fallback resume el estado kc2g.
        if (impl->ionSourceMode.load() == Impl::SRC_KC2G_FALLBACK) {
            Kc2gAdapter::Kc2gStatus ks = impl->kc2g.getStatus();
            ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f),
                "kc2g-fallback: %d/%d st (retry GIRO in %.0f min)",
                ks.panelUsable, ks.rows, gs.retryInMin);
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip(
                "GIRO is rate-limited: station data comes from the prop.kc2g.com failover (GIRO/WWROF, 1 req/cycle). The staggered GIRO round keeps retrying in the background; when it answers again the source switches back to GIRO-live automatically.");
        } else if (impl->ionSourceMode.load() == Impl::SRC_MODEL) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "model: no live station data");
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip(
                "No fresh GIRO or kc2g station data: climatological model in charge (M5).");
        } else if (gs.degraded > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                "GIRO-live: degraded, retry %.0f min", gs.retryInMin);
        } else {
            ImGui::Text("GIRO-live: %d/%d st  hist %.1f h", gs.withData, gs.polled, gs.histHours);
        }
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Polled GIRO stations and their history depth (the worst station limits station replay). Backoff protects the GIRO service.");
        ImGui::End();
    }

    // ---- Etiquetas 3D ancladas (Fase 1) ----
    if (impl->labelsVisible) {
        auto project = [&](const glm::vec3& w, ImVec2& out) -> bool {
            if (glm::dot(glm::normalize(w), glm::normalize(camPos)) < 0.15f) return false;  // cara oculta
            glm::vec4 c = proj * view * glm::vec4(w, 1.0f);
            if (c.w <= 0.0f) return false;
            glm::vec3 ndc = glm::vec3(c) / c.w;
            if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f || ndc.z > 1.0f) return false;
            out = ImVec2((ndc.x * 0.5f + 0.5f) * (float)winW, (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)winH);
            return true;
        };
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        auto label = [&](const glm::vec3& w, const char* text, ImU32 col) {
            ImVec2 s;
            if (!project(w, s)) return;
            ImVec2 t(s.x + 12.0f, s.y - 26.0f);
            fg->AddLine(s, t, col, 1.0f);
            fg->AddCircleFilled(s, 2.5f, col);
            fg->AddText(t, col, text);
        };
        // 1) Pico F2 sobre pineada (si no, hover).
        if (impl->ionosondeLayer.isVisible()) {
            const StationPoint* a = nullptr;
            int n = impl->ionosondeLayer.getStationCount();
            if (!impl->pinnedCode.empty()) {
                for (int i = 0; i < n; i++) {
                    const StationPoint* c = impl->ionosondeLayer.getSample(i);
                    if (c && c->code == impl->pinnedCode) { a = c; break; }
                }
            }
            if (!a && impl->hoveredStation >= 0 && impl->hoveredStation < n)
                a = impl->ionosondeLayer.getSample(impl->hoveredStation);
            if (a && a->valid) {
                float hm = a->hmF2 > 0.0f ? a->hmF2 : 300.0f;
                char tb[64];
                snprintf(tb, sizeof(tb), "%s F2 %.0fkm", a->code.c_str(), hm);
                label(densityDir(a->latDeg, a->lonDeg) * (1.0f + hm / 6371.0f),
                      tb, IM_COL32(255, 220, 80, 255));
            }
        }
        // 2) Ovalo auroral (67N, meridiano de medianoche aprox).
        if (impl->auroraLayer.isVisible()) {
            glm::vec3 sd = glm::normalize(impl->sunDirection);
            float slon = glm::degrees(atan2f(sd.z, -sd.x)) + 180.0f;
            if (slon > 180.0f) slon -= 360.0f;
            label(densityDir(67.0f, slon) * (1.0f + 110.0f / 6371.0f),
                  "Aurora oval", IM_COL32(120, 255, 140, 255));
        }
        // 3) Shell TEC bajo camara.
        if (impl->tecLayer.isVisible()) {
            glm::vec3 cd = glm::normalize(camPos);
            float clat = glm::degrees(asinf(glm::clamp(cd.y, -1.0f, 1.0f)));
            float clon = glm::degrees(atan2f(cd.z, -cd.x));
            label(densityDir(clat, clon) * (1.0f + 300.0f / 6371.0f),
                  "TEC shell", IM_COL32(140, 200, 255, 255));
        }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }   // !uiHidden
}

void App::shutdown() {
    saveSettings();
    impl->shutdownRequested.store(true);
    if (impl->fetchThread.joinable()) impl->fetchThread.join();
    // M0.2: esperar las precargas ANTES de destruir impl (leen impl).
    if (impl->histPreloadThread.joinable()) impl->histPreloadThread.join();
    if (impl->catalogPreloadThread.joinable()) impl->catalogPreloadThread.join();
    if (impl->irtamBackfillThread.joinable()) impl->irtamBackfillThread.join();  // M-IRTAM-F2 (P4)
    if (impl->irtamSampleThread.joinable()) impl->irtamSampleThread.join();  // M-irtam-replay-024 (M0.2)
    impl->tecLayer.destroy();
    impl->volumeLayer.destroy();
    impl->peakShell.destroy();
    impl->tecColShell.destroy();
    impl->sliceLayer.destroy();
    impl->shellLayer.destroy();
    impl->ionosondeLayer.destroy();
    impl->solarWindLayer.destroy();
    impl->faradayLayer.destroy();
    impl->refShells.destroy();
    impl->auroraLayer.destroy();
    impl->hfTrace.destroy();

    if (impl->quadVAO) glDeleteVertexArrays(1, &impl->quadVAO);
    if (impl->quadVBO) glDeleteBuffers(1, &impl->quadVBO);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (window) glfwDestroyWindow(window);
    glfwTerminate();

    delete impl;
    impl = nullptr;
}
