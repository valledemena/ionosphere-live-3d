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
#include "Data/SolarIndicesAdapter.h"
#include "Data/SolarWindAdapter.h"
#include "Data/RadioPropagationAdapter.h"
#include "Data/AuroraAdapter.h"
#include "Data/EbroAdapter.h"
#include "Data/XrayAdapter.h"
#include "Ionosphere/FaradayLayer.h"
#include "Ionosphere/RefShells.h"
#include "Ionosphere/VolumeRenderer.h"
#include "Ionosphere/SliceLayer.h"
#include "Ionosphere/LayerProfile.h"
#include "Ionosphere/DensityVolume.h"
#include "Utils/SolarPosition.h"
#include "Utils/CoordinateSystem.h"

struct App::Impl {
    // --- Bundles publicados por el hilo worker (inmutables, refcount) ---
    struct TecBundle { GridData grid; bool simulated = false; };
    struct IndicesBundle {
        IndexSnapshot snap;
        std::vector<IndexHistoryPoint> kp, dst, f107, ssn;
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
    mutable std::mutex bundleMutex;
    std::shared_ptr<const TecBundle> tec;
    std::shared_ptr<const IndicesBundle> indices;
    std::shared_ptr<const WindBundle> wind;
    std::shared_ptr<const AuroraBundle> auroraB;
    std::shared_ptr<const RadioBundle> radio;
    std::shared_ptr<const XrayBundle> xray;
    std::shared_ptr<const TecBundle> getTec() const { std::lock_guard<std::mutex> lk(bundleMutex); return tec; }
    std::shared_ptr<const IndicesBundle> getIndices() const { std::lock_guard<std::mutex> lk(bundleMutex); return indices; }
    std::shared_ptr<const WindBundle> getWind() const { std::lock_guard<std::mutex> lk(bundleMutex); return wind; }
    std::shared_ptr<const AuroraBundle> getAurora() const { std::lock_guard<std::mutex> lk(bundleMutex); return auroraB; }
    std::shared_ptr<const RadioBundle> getRadio() const { std::lock_guard<std::mutex> lk(bundleMutex); return radio; }
    std::shared_ptr<const XrayBundle> getXray() const { std::lock_guard<std::mutex> lk(bundleMutex); return xray; }
    // Hilo worker de red + progreso de carga inicial.
    std::thread fetchThread;
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
    float bloomThreshold = 0.35f;  // bright-pass threshold (scene luminance)
    float bloomIntensity = 1.0f;   // blur pass gain
    float bloomStrength = 0.6f;    // composite add amount
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
    float tecOpacity = 1.0f;
    // Variable del mapa de colores esferico (Seccion A del plan + Fase 5):
    // 0=TEC(GloTEC), 1=foF2, 2=MUF, 3=hmF2, 4=NmF2 (1-4 GIRO),
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

    // Viento solar (Fase 4): estado de la capa de flechas 3D.
    bool swArrowsVisible = false;
    int swArrowMode = 0;       // 0=color por velocidad, 1=color por Bz
    float swArrowScale = 1.0f;

    // Propagacion HF + Faraday (Fase 5).
    bool faradayVisible = false;
    int faradayMode = 0;       // 0=longitud por valor, 1=longitud fija + color
    float faradayScale = 1.0f;

    // Estacion GIRO bajo el cursor (hover) y fijada con click (pin, por codigo
    // URSI para que sobreviva al refresco incremental de muestras).
    int hoveredStation = -1;
    std::string pinnedCode;
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
    // Volumen + shells de referencia.
    VolumeRenderer volumeLayer;
    bool volumeVisible = false;
    float volumeOpacity = 1.0f;
    DensityVolume lastVolume;   // ultimo volumen construido (para el slice)
    // Cascaras derivadas del volumen (Fase 2/3, plan volume-derived-shells).
    GridLayer peakShell;        // F2: geometria hm real + color logNe
    bool peakShellVisible = false;
    GridLayer tecColShell;      // TEC modelado (integral) a 300 km planos
    bool tecColVisible = false;
    // Slice QTH-DX: A = pineada, B = hover.
    SliceLayer sliceLayer;
    bool sliceOn = false;
    std::string sliceA, sliceB;
    float slicePeakLog = 0.0f;  // log10(Ne) maximo del trayecto
    // Anillo protegido (el hilo de precarga escribe mientras el render lee).
    std::mutex tecHistMutex;
    std::atomic<int> histTotal{0}, histDone{0};
    std::atomic<bool> histLoading{false};
    // El worker la activa al publicar el primer TEC real (no mock).
    std::atomic<bool> liveTecArrived{false};
    // Catalogo GIRO en background.
    std::atomic<bool> catalogReady{false};
    bool catalogApplied = false;

    glm::vec3 sunDirection = glm::vec3(1.0f, 0.3f, 0.5f);

    void updateSunDirection() {
        // OJO: aqui va tiempo Unix REAL (el bug anterior pasaba glfwGetTime(),
        // segundos desde el arranque, y el terminador quedaba congelado en 1970).
        double utc = (double)std::time(nullptr);
        sunDirection = SolarPosition::getDirection(utc);
    }
};

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
    for (const auto& s : samples) if (s.valid) pts.push_back(s);
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
// var: 0=LUF, 1=FOT, 2=Spread-F, 3=MUF. kpProxy modula Spread-F.
static GridData radioInterpGrid(const std::vector<StationSample>& samples, int var, int w, int h,
                                double utc, float kpProxy) {
    const int W = w, H = h;
    GridData g;
    g.width = W;
    g.height = H;
    g.values.assign(W * H, 0.0f);
    g.valid = false;

    std::vector<StationSample> pts;
    for (const auto& s : samples) if (s.valid) pts.push_back(s);
    if (pts.empty()) return g;

    const float maxDist = 50.0f;
    const float cellLat = 180.0f / H;
    const float cellLon = 360.0f / W;
    // Fondo = mediana global (sin acantilados en oceanos).
    const float wBg = 0.004f;
    std::vector<float> mvals;
    mvals.reserve(pts.size());
    for (const auto& p : pts) {
        float vv = RadioPropagationAdapter::stationValue(p, var, utc, kpProxy);
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
                float val = RadioPropagationAdapter::stationValue(p, var, utc, kpProxy);
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

// Interpola perfiles de capa (IDW, mismo patron/radio/peso que giroInterpGrid
// para consistencia visual con las capas 2D). Fondo = mediana por campo.
static std::vector<LayerProfile> interpLayerProfiles(const std::vector<StationSample>& samples, int w, int h) {
    std::vector<LayerProfile> out((size_t)w * h);
    std::vector<StationSample> pts;
    for (const auto& s : samples) if (s.valid) pts.push_back(s);
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
            for (const auto& p : pts) {
                float dlat = p.latDeg - latG;
                float dlon = p.lonDeg - lonG;
                if (dlon > 180.0f) dlon -= 360.0f;
                if (dlon < -180.0f) dlon += 360.0f;
                float d = sqrtf(dlat * dlat + dlon * dlon);
                if (d > maxDist) continue;
                float w = 1.0f / (d * d + 0.5f);
                if (p.foE > 0.0f)  { float v = 1.24e10f * p.foE * p.foE; wNmE += w; vNmE += (double)w * v; }
                if (p.foF1 > 0.0f) { float v = 1.24e10f * p.foF1 * p.foF1; wNmF1 += w; vNmF1 += (double)w * v; }
                if (p.foF2 > 0.0f) { float v = 1.24e10f * p.foF2 * p.foF2; wNmF2 += w; vNmF2 += (double)w * v; }
                if (p.hmF2 > 0.0f) { wHm += w; vHm += (double)w * p.hmF2; }
                if (p.B0 > 0.0f)   { wB0 += w; vB0 += (double)w * p.B0; }
                if (p.B1 > 0.0f)   { wB1 += w; vB1 += (double)w * p.B1; }
            }
            LayerProfile pr;
            pr.NmE = (float)(vNmE / wNmE);
            pr.hasE = pr.NmE > 0.0f;
            pr.NmF1 = (float)(vNmF1 / wNmF1);
            pr.hasF1 = pr.NmF1 > 0.0f;
            pr.NmF2 = (float)(vNmF2 / wNmF2);
            pr.hmF2 = (float)(vHm / wHm);
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

static void framebufferSizeCallback(GLFWwindow* w, int width, int height) {
    glViewport(0, 0, width, height);
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
    glfwWindowHint(GLFW_SAMPLES, 4);

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
        impl->atmosphereShader.loadFromSource(
            "#version 400 core\n"
            "layout(location=0) in vec3 aPos;\n"
            "uniform mat4 u_model, u_view, u_projection;\n"
            "out vec3 vPos;\n"
            "void main(){\n"
            "  vPos = aPos;\n"
            "  gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);\n"
            "}\n",
            "#version 400 core\n"
            "in vec3 vPos;\n"
            "uniform vec3 u_sunDir;\n"
            "uniform vec3 u_cameraPos;\n"
            "out vec4 fragColor;\n"
            "void main(){\n"
            "  vec3 n = normalize(vPos);\n"
            "  vec3 viewDir = normalize(u_cameraPos - vPos);\n"
            "  float fresnel = 1.0 - max(dot(viewDir, n), 0.0);\n"
            "  fresnel = pow(fresnel, 3.0);\n"
            "  float sunAlign = max(dot(n, normalize(u_sunDir)), 0.0);\n"
            "  vec3 dayColor = vec3(0.3, 0.6, 1.0);\n"
            "  vec3 sunsetColor = vec3(1.0, 0.4, 0.1);\n"
            "  vec3 col = mix(dayColor, sunsetColor, pow(1.0 - sunAlign, 5.0));\n"
            "  col *= 0.5 + 0.5 * sunAlign;\n"
            "  fragColor = vec4(col, fresnel * 0.6);\n"
            "}\n"
        );
    }

    if (!impl->bloomShader.load("shaders/bloom.vert", "shaders/bloom.frag")) {
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
            "out vec4 fragColor;\n"
            "void main(){\n"
            "  float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
            "  vec3 result = texture(u_texture, vUV).rgb * weight[0];\n"
            "  vec2 texel = u_direction / u_resolution;\n"
            "  for(int i=1;i<5;i++){\n"
            "    result += texture(u_texture, vUV + texel * float(i)).rgb * weight[i];\n"
            "    result += texture(u_texture, vUV - texel * float(i)).rgb * weight[i];\n"
            "  }\n"
            "  fragColor = vec4(result, 1.0);\n"
            "}\n"
        );
    }

    if (!impl->compositeShader.load("shaders/composite.vert", "shaders/composite.frag")) {
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
            "out vec4 fragColor;\n"
            "void main(){\n"
            "  vec3 scene = texture(u_scene, vUV).rgb;\n"
            "  vec3 bloom = texture(u_bloom, vUV).rgb;\n"
            "  vec3 col = scene;\n"
            "  if(u_bloomEnabled) col += bloom * 0.4;\n"
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
    // Precarga del historico GloTEC (indice recuperado): ultimos 18 frames
    // (~3 h) en background para que el replay tenga pasado real al instante.
    {
        App* self = this;
        std::thread([self]() {
            // Esperar al primer TEC vivo (max 180 s) para no competir con el
            // arranque; sin red se omite la precarga (el replay queda en sesion).
            bool gated = false;
            for (int i = 0; i < 180 && !self->impl->liveTecArrived.load(); i++)
                std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!self->impl->liveTecArrived.load()) return;
            self->impl->histLoading.store(true);
            GloTecAdapter loader;   // instancia propia: sin estado compartido
            loader.setGridSize(72, 72);
            std::vector<GloTecFrame> idx;
            if (loader.fetchIndex(idx)) {
                size_t take = idx.size() > 18 ? 18 : idx.size();
                self->impl->histTotal.store((int)take);
                for (size_t k = idx.size() - take; k < idx.size(); k++) {
                    GridData fr;
                    if (loader.fetchFrame(idx[k].url, idx[k].timeUtc, fr))
                        self->pushHistoryFrame(fr, idx[k].timeUtc);
                    self->impl->histDone.store(self->impl->histDone.load() + 1);
                }
                printf("[App] History preload done\n");
            }
            self->impl->histLoading.store(false);
        }).detach();
    }

    // GIRO ionosondas (Fase 3): el catalogo tambien va en background para no
    // congelar el arranque (30 s de timeout en el peor caso).
    impl->giro.init();   // solo curl_global_init + printf (sin red)
    impl->ionosondeLayer.init();
    impl->ionosondeLayer.setVisible(true);
    impl->ionosondeLayer.setHeightOffset(320.0f / 6371.0f);
    impl->ionosondeLayer.setPointSize(11.0f);
    {
        App* self = this;
        std::thread([self]() {
            if (self->impl->giro.fetchCatalog())
                self->impl->catalogReady.store(true);
        }).detach();
    }

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

    // Framebuffers
    impl->sceneFBO.init(winW, winH);
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

    impl->updateSunDirection();

    // Persistencia GUI: aplicar settings.cfg si existe (visibilidades,
    // variables, sliders, camara) y refrescar las capas en consecuencia.
    loadSettings();
    impl->tecLayer.setVisible(impl->tecVisible);
    impl->tecLayer.setOpacity(impl->tecOpacity);
    impl->tecLayer.setAltitude(impl->altKm / 6371.0f);
    impl->shellLayer.setVisible(impl->outerShell);
    impl->shellLayer.setAltitude(impl->altKm / 6371.0f + 0.025f);
    impl->ionosondeLayer.setVisible(impl->giroStationsVisible);
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
    impl->peakShell.setVisible(impl->peakShellVisible);
    impl->tecColShell.setVisible(impl->tecColVisible);
    impl->sliceLayer.setVisible(impl->sliceOn);
    impl->refShells.setVisible(impl->refShellsVisible);
    applyColorLayer();
    applyRelief();

    printf("[OK] All initialized\n");
    return true;
}

void App::run() {
    while (!glfwWindowShouldClose(window)) {
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
    impl->updateSunDirection();
    impl->terminator.update(impl->sunDirection);  // anillo dia/noche real (~1 KB/frame)

    // Catalogo GIRO listo (hilo background) -> aplicar puntos y arrancar datos.
    if (impl->catalogReady.load() && !impl->catalogApplied) {
        impl->ionosondeLayer.updateSamples(giroPointsFrom(impl->giro.getSamples()));
        impl->giro.fetchStationsAsync();
        impl->catalogApplied = true;
    }

    // Consumir bundles del hilo worker (red+parse fuera del hilo de render).
    consumeBundles();

    // GIRO: refresco periodico de las estaciones en background (5 min).
    // Ebro directo: idem. La red NOAA la hace el worker (sin congelar).
    static double lastIndexFetch = -1.0;
    double now = glfwGetTime();
    if (lastIndexFetch < 0 || (now - lastIndexFetch) > 300.0) {
        if (impl->catalogApplied && !impl->giro.isFetching()) {
            impl->giro.fetchStationsAsync();
        }
        impl->ebro.fetchAsync((double)std::time(nullptr));
        lastIndexFetch = now;
    }

    // Replay temporal: TEC interpolado + red GIRO muestreada al mismo instante.
    // En replay, puntos, grids derivados (foF2/MUF/hmF2/NmF2/LUF/FOT/Spread-F)
    // y panel de estacion siguen al tiempo de replay, no al directo.
    double replayEpoch = -1.0;
    {
        // Copia minima bajo lock (el hilo de precarga puede estar escribiendo).
        GridData fa, fb;
        double f = 0.0;
        bool havePair = false;
        if (impl->replayMode) {
            std::lock_guard<std::mutex> lk(impl->tecHistMutex);
            if (impl->tecHist.size() >= 2) {
                if (impl->replayPlaying) {
                    impl->replayPos += (double)dt * impl->replaySpeed;
                    double maxPos = (double)impl->tecHist.size() - 1.0;
                    if (impl->replayPos >= maxPos) impl->replayPos = 0.0;  // loop
                }
                size_t i0 = (size_t)impl->replayPos;
                if (i0 >= impl->tecHist.size() - 1) i0 = impl->tecHist.size() - 2;
                f = impl->replayPos - (double)i0;
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
            impl->giroReplayCache = impl->giro.sampleHistoryAt(replayEpoch);
        }
    }
    const std::vector<StationSample>& gsLive = impl->giro.getSamples();
    const std::vector<StationSample>& gsAct =
        (impl->replayMode && !impl->giroReplayCache.empty()) ? impl->giroReplayCache : gsLive;

    // Reflejar en la capa de ionosondas el estado de las muestras GIRO (streaming
    // incremental: el hilo background va completando estaciones poco a poco).
    // En replay se muestran las muestras interpoladas al tiempo de replay.
    impl->ionosondeLayer.setFetching(impl->giro.isFetching() && !impl->replayMode);
    impl->ionosondeLayer.setProgress(impl->giro.getProgress());
    impl->ionosondeLayer.setDataIndicator(impl->giro.getDataIndicator());
    static double lastSamplesApply = -1.0;
    if (lastSamplesApply < 0 || (now - lastSamplesApply) > 2.0) {
        impl->ionosondeLayer.updateSamples(giroPointsFrom(gsAct));
        lastSamplesApply = now;
    }

    // Si la variable de mapa es una magnitud GIRO (1-4), re-agregar su grid interpolado
    // cuando las muestras gyro hayan cambiado (streaming incremental).
    // Fase 5: variables 5-7 (LUF/FOT/Spread-F) via RadioPropagationAdapter.
    // En replay, los grids derivados tambien siguen al tiempo de replay (gsAct).
    if (impl->mapVariable >= 1 && impl->mapVariable <= 4) {
        bool centered = (impl->mapVariable == 3);  // hmF2: relieve de alturas reales
        impl->tecLayer.setDisplaceCentered(centered);
        impl->shellLayer.setDisplaceCentered(centered);
        updateMainGrid(giroInterpGrid(gsAct, impl->mapVariable - 1, 72, 72));
    } else if (impl->mapVariable >= 5 && impl->mapVariable <= 7) {
        float kpProxy = currentKp();
        double epoch = (replayEpoch > 0.0) ? replayEpoch : (double)std::time(nullptr);
        updateMainGrid(radioInterpGrid(gsAct,
            impl->mapVariable - 5, 72, 72, epoch, kpProxy));
    }

    // Volumen Ne(h) + RefShells dinamicos: reconstruir cada ~5 s con las
    // muestras activas (sigue live, replay, sol y flares).
    static double lastVolBuild = -1.0;
    if (now - lastVolBuild > 5.0) {
        lastVolBuild = now;
        auto profs = interpLayerProfiles(gsAct, 72, 72);
        glm::vec3 sd = glm::normalize(impl->sunDirection);
        applyDayNight(profs, 72, 72, sd.x, sd.y, sd.z);
        float flare = 1.0f;
        if (impl->lastXray && impl->lastXray->latest.valid)
            flare = XrayAdapter::flareFactor(impl->lastXray->latest.flux);
        DensityVolume vol = buildDensityVolume(profs, 72, 72, 60, 60.0f, 700.0f,
                                               sd.x, sd.y, sd.z, flare);
        vol.timestamp = (replayEpoch > 0.0) ? replayEpoch : (double)std::time(nullptr);
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

void App::updateMainGrid(const GridData& grid) {
    impl->tecLayer.updateFromGrid(grid);
    impl->shellLayer.updateFromGrid(grid);
}

void App::saveSettings() {
    FILE* f = fopen("settings.cfg", "w");
    if (!f) return;
    fprintf(f, "ionolive1\n");
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
    fprintf(f, "showStars %d\n", impl->showStars ? 1 : 0);
    fprintf(f, "nightLevel %g\n", (double)impl->nightLevel);
    fprintf(f, "cloudAmt %g\n", (double)impl->cloudAmt);
    fprintf(f, "bloomEnabled %d\n", impl->bloomEnabled ? 1 : 0);
    fprintf(f, "bloomThreshold %g\n", (double)impl->bloomThreshold);
    fprintf(f, "bloomIntensity %g\n", (double)impl->bloomIntensity);
    fprintf(f, "bloomStrength %g\n", (double)impl->bloomStrength);
    fprintf(f, "reliefOn %d\n", impl->reliefOn ? 1 : 0);
    fprintf(f, "reliefAmp %g\n", (double)impl->reliefAmp);
    fprintf(f, "stormSwell %d\n", impl->stormSwell ? 1 : 0);
    fprintf(f, "outerShell %d\n", impl->outerShell ? 1 : 0);
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
    fprintf(f, "peakShellVisible %d\n", impl->peakShellVisible ? 1 : 0);
    fprintf(f, "tecColVisible %d\n", impl->tecColVisible ? 1 : 0);
    fprintf(f, "volumeOpacity %g\n", (double)impl->volumeOpacity);
    fprintf(f, "sliceOn %d\n", impl->sliceOn ? 1 : 0);
    fprintf(f, "camRX %g\n", (double)impl->camera.rotateX);
    fprintf(f, "camRY %g\n", (double)impl->camera.rotateY);
    fprintf(f, "camDist %g\n", (double)impl->camera.distance);
    fprintf(f, "pinned %s\n", impl->pinnedCode.empty() ? "-" : impl->pinnedCode.c_str());
    fclose(f);
}

void App::loadSettings() {
    FILE* f = fopen("settings.cfg", "r");
    if (!f) return;
    char tag[32] = {0};
    if (!fgets(tag, sizeof(tag), f)) { fclose(f); return; }
    if (std::string(tag).find("ionolive1") == std::string::npos) { fclose(f); return; }
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
        if (k == "mapVariable") impl->mapVariable = glm::clamp((int)v, 0, 7);
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
        else if (k == "showStars") impl->showStars = clB(v);
        else if (k == "nightLevel") impl->nightLevel = glm::clamp((float)v, 0.05f, 1.0f);
        else if (k == "cloudAmt") impl->cloudAmt = glm::clamp((float)v, 0.0f, 1.0f);
        else if (k == "bloomEnabled") impl->bloomEnabled = clB(v);
        else if (k == "bloomThreshold") impl->bloomThreshold = glm::clamp((float)v, 0.0f, 1.0f);
        else if (k == "bloomIntensity") impl->bloomIntensity = glm::clamp((float)v, 0.0f, 3.0f);
        else if (k == "bloomStrength") impl->bloomStrength = glm::clamp((float)v, 0.0f, 2.0f);
        else if (k == "reliefOn") impl->reliefOn = clB(v);
        else if (k == "reliefAmp") impl->reliefAmp = glm::clamp((float)v, 0.0f, 0.10f);
        else if (k == "stormSwell") impl->stormSwell = clB(v);
        else if (k == "outerShell") impl->outerShell = clB(v);
        else if (k == "refShellsVisible") impl->refShellsVisible = clB(v);
        else if (k == "swArrowsVisible") impl->swArrowsVisible = clB(v);
        else if (k == "swArrowMode") impl->swArrowMode = glm::clamp((int)v, 0, 1);
        else if (k == "swArrowScale") impl->swArrowScale = glm::clamp((float)v, 0.2f, 4.0f);
        else if (k == "faradayVisible") impl->faradayVisible = clB(v);
        else if (k == "faradayMode") impl->faradayMode = glm::clamp((int)v, 0, 1);
        else if (k == "faradayScale") impl->faradayScale = glm::clamp((float)v, 0.2f, 4.0f);
        else if (k == "auroraVisible") impl->auroraVisible = clB(v);
        else if (k == "tecVisible") impl->tecVisible = clB(v);
        else if (k == "giroStationsVisible") impl->giroStationsVisible = clB(v);
        else if (k == "cineSpeed") impl->cineSpeed = glm::clamp((float)v, 0.005f, 0.30f);
        else if (k == "replaySpeed") impl->replaySpeed = glm::clamp((float)v, 0.5f, 8.0f);
        else if (k == "volumeVisible") impl->volumeVisible = clB(v);
        else if (k == "peakShellVisible") impl->peakShellVisible = clB(v);
        else if (k == "tecColVisible") impl->tecColVisible = clB(v);
        else if (k == "volumeOpacity") impl->volumeOpacity = glm::clamp((float)v, 0.0f, 2.5f);
        else if (k == "sliceOn") impl->sliceOn = clB(v);
        else if (k == "camRX") impl->camera.rotateX = glm::clamp((float)v, -1.5f, 1.5f);
        else if (k == "camRY") impl->camera.rotateY = (float)v;
        else if (k == "camDist") impl->camera.distance = glm::clamp((float)v, 1.5f, 20.0f);
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
    EsaAdapter esa;   // solo mock offline para el TEC
    bool haveTec = false;
    bool haveXray = false;
    bool firstPass = true;

    auto noteDone = [&]() {
        if (firstPass) impl->loadDone.store(impl->loadDone.load() + 1);
    };

    while (!impl->shutdownRequested.load()) {
        double epoch = (double)std::time(nullptr);

        // 1) TEC GloTEC primero (es la capa principal; asi aparece en segundos).
        //    Si falla y no hay nada todavia, mock Esa offline marcado simulated.
        {
            GridData g;
            bool ok = glo.fetchTEC(epoch);
            bool sim = false;
            if (ok) {
                g = glo.getGrid();
            } else if (!haveTec) {
                esa.fetchTEC(epoch);
                g.width = esa.getData().width;
                g.height = esa.getData().height;
                g.values = esa.getData().values;
                g.timestamp = epoch;
                g.valid = esa.getData().valid;
                sim = true;
            }
            if (g.valid) {
                auto b = std::make_shared<Impl::TecBundle>();
                b->grid = g;
                b->grid.timestamp = epoch;
                b->simulated = sim;
                { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->tec = b; }
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
            { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->indices = b; }
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
            }
        }
        noteDone();

        // 5) Propagacion HF (usa muestras GIRO thread-safe).
        {
            auto samples = impl->giro.copySamples();
            rp.fetch(epoch, samples);
            if (rp.getLatest().valid && (rp.usedGiroReal() || !impl->getRadio())) {
                auto b = std::make_shared<Impl::RadioBundle>();
                b->latest = rp.getLatest();
                b->aeHist = rp.getAeHistory();
                b->s4Hist = rp.getS4History();
                b->simulated = rp.isSimulated();
                b->usedGiro = rp.usedGiroReal();
                b->error = rp.getErrorMessage();
                { std::lock_guard<std::mutex> lk(impl->bundleMutex); impl->radio = b; }
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
                haveXray = true;
            }
        }
        noteDone();

        if (firstPass) {
            firstPass = false;
            impl->initialLoadDone.store(true);
            printf("[App] Initial fetch pass done\n");
        }

        // Espera interrumpible 300 s.
        for (int i = 0; i < 300 && !impl->shutdownRequested.load(); i++)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// Aplica en el hilo principal los bundles frescos del worker (llamadas GL
// y UI van aqui; el worker nunca toca OpenGL).
void App::consumeBundles() {
    auto t = impl->getTec();
    if (t && t != impl->lastTec) {
        impl->lastTec = t;
        if (t->grid.valid) {
            pushHistoryFrame(t->grid, t->grid.timestamp);
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
}

// Cortina vertical A (pinned) -> B (hover) muestreada de lastVolume.
// Sin GL salvo updateSlice (hilo principal). Convencion verificada del motor.
void App::rebuildSlice(bool force) {
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
    if (!A || !B || A->code == B->code || !impl->lastVolume.valid) return;
    if (!force && A->code == impl->sliceA && B->code == impl->sliceB) return;
    impl->sliceA = A->code;
    impl->sliceB = B->code;

    glm::vec3 da = densityDir(A->latDeg, A->lonDeg);
    glm::vec3 db = densityDir(B->latDeg, B->lonDeg);
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

void App::pushHistoryFrame(const GridData& grid, double ts) {    if (!grid.valid || grid.values.empty()) return;
    std::lock_guard<std::mutex> lk(impl->tecHistMutex);
    for (const auto& e : impl->tecHist) {
        if (fabs(e.timestamp - ts) < 240.0) return;  // dedup ~4 min
    }
    GridData fr = grid;
    fr.timestamp = ts;
    size_t pos = 0;
    while (pos < impl->tecHist.size() && impl->tecHist[pos].timestamp < ts) pos++;
    impl->tecHist.insert(impl->tecHist.begin() + pos, fr);
    while (impl->tecHist.size() > 36) impl->tecHist.erase(impl->tecHist.begin());
}

void App::applyColorLayer() {    // 0 = TEC (GloTEC real). 1-4 = foF2/MUF/hmF2/NmF2 interpoladas de GIRO.
    // 5-7 = LUF/FOT/Spread-F derivados GIRO via RadioPropagationAdapter (Fase 5).
    // Si hay override manual se respeta; si no, automatico por variable.
    static const char* CM_NAMES[4] = { "viridis", "inferno", "plasma", "turbo" };
    static const int VAR_AUTO_CM[8] = { 0, 2, 1, 3, 3, 2, 1, 3 };  // TEC,foF2,MUF,hmF2,NmF2,LUF,FOT,SpF

    if (impl->mapVariable == 0) {
        impl->tecLayer.setDisplaceCentered(false);
        impl->shellLayer.setDisplaceCentered(false);
        impl->tecLayer.clearRange();
        if (auto t = impl->getTec()) {
            if (t->grid.valid) updateMainGrid(t->grid);
        }
    } else if (impl->mapVariable >= 1 && impl->mapVariable <= 4) {
        int var = impl->mapVariable - 1;   // 0=foF2,1=MUF,2=hmF2,3=NmF2
        GridData g = giroInterpGrid(impl->giro.getSamples(), var, 72, 72);
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
        double epoch = (double)std::time(nullptr);  // derive*() espera Unix UTC
        int var = impl->mapVariable - 5;   // 0=LUF,1=FOT,2=Spread-F
        GridData g = radioInterpGrid(impl->giro.getSamples(), var, 72, 72, epoch, kpProxy);
        if (!g.valid) return;
        if (var == 2) impl->tecLayer.setRange(0.0f, 100.0f);  // Spread-F % fijo
        else impl->tecLayer.clearRange();
        updateMainGrid(g);
        if (var != 2) { /* rango auto para LUF/FOT */ }
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
        impl->earth.render();
    }

    // (Atmosfera y terminador: se dibujan en el pase aditivo post-bloom para
    // que el halo salga nitido y no se queme a blanco con el bloom.)

    impl->sceneFBO.unbind();

    // --- Pass 2: Bloom ---
    if (impl->bloomEnabled) {
        glDisable(GL_DEPTH_TEST);
        impl->bloomShader.use();

        // Horizontal pass
        impl->bloomFBO[0].bind();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        impl->bloomShader.setVec2("u_direction", glm::vec2(1.0f, 0.0f));
        impl->bloomShader.setVec2("u_resolution", glm::vec2((float)impl->bloomFBO[0].width, (float)impl->bloomFBO[0].height));
        impl->bloomShader.setFloat("u_threshold", impl->bloomThreshold);
        impl->bloomShader.setFloat("u_intensity", impl->bloomIntensity);
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
        impl->bloomShader.setVec2("u_direction", glm::vec2(0.0f, 1.0f));
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

    // --- Overlays: se dibujan DESPUES del bloom para que no participen
    // en el bright-pass (el bloom solo ilumina la Tierra; atmosfera, TEC y
    // resto salen nitidos sin quemarse a blanco).
    bool additiveDrawn = false;
    if (impl->showAtmosphere || impl->terminatorVisible || impl->tecLayer.isVisible() || impl->ionosondeLayer.isVisible() || impl->solarWindLayer.isVisible() || impl->faradayLayer.isVisible() || impl->auroraLayer.isVisible() || impl->shellLayer.isVisible() || impl->refShells.isVisible()) {
        impl->sceneFBO.bind();
        glViewport(0, 0, winW, winH);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        // Atmosfera primero (halo base bajo las capas de datos).
        if (impl->showAtmosphere) {
            glm::mat4 atmoModel = glm::scale(glm::mat4(1.0f), glm::vec3(1.02f));
            impl->atmosphereShader.use();
            impl->atmosphereShader.setMat4("u_model", atmoModel);
            impl->atmosphereShader.setMat4("u_view", view);
            impl->atmosphereShader.setMat4("u_projection", proj);
            impl->atmosphereShader.setVec3("u_sunDir", impl->sunDirection);
            impl->atmosphereShader.setVec3("u_cameraPos", camPos);
            impl->atmosphere.render();
        }
        if (impl->tecLayer.isVisible()) impl->tecLayer.render(view, proj);
        if (impl->shellLayer.isVisible()) impl->shellLayer.render(view, proj);
        if (impl->peakShell.isVisible()) impl->peakShell.render(view, proj);
        if (impl->tecColShell.isVisible()) impl->tecColShell.render(view, proj);
        if (impl->volumeLayer.isVisible()) impl->volumeLayer.render(view, proj, camPos);
        if (impl->sliceOn && impl->sliceLayer.isVisible()) impl->sliceLayer.render(view, proj);
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
        if (impl->ionosondeLayer.isVisible()) impl->ionosondeLayer.render(view, proj);
        if (impl->solarWindLayer.isVisible()) impl->solarWindLayer.render(view, proj);
        if (impl->faradayLayer.isVisible()) impl->faradayLayer.render(view, proj);
        if (impl->auroraLayer.isVisible()) impl->auroraLayer.render(view, proj);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        impl->sceneFBO.unbind();
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

    // Draw fullscreen quad with persistent VAO
    glBindVertexArray(impl->quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);

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
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Bright-pass threshold: only pixels brighter than this feed the glow. Lower = stronger, more diffuse glow.");
    if (ImGui::SliderFloat("Intensity", &impl->bloomIntensity, 0.0f, 3.0f)) {
        impl->bloomIntensity = glm::clamp(impl->bloomIntensity, 0.0f, 3.0f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Blur pass gain. Scales the extracted bright areas before compositing.");
    if (ImGui::SliderFloat("Strength", &impl->bloomStrength, 0.0f, 2.0f)) {
        impl->bloomStrength = glm::clamp(impl->bloomStrength, 0.0f, 2.0f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Final mix: how much glow is added over the scene. 0 = no visible effect even if Bloom is on.");
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
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Atmospheric rim glow (fresnel). Purely visual, no data.");
    ImGui::Checkbox("Stars", &impl->showStars);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Starfield background. Decorative only.");
    ImGui::Checkbox("Terminator", &impl->terminatorVisible);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Thin ring on the day/night boundary (follows the real sun). Turn off if you prefer a clean globe.");
    ImGui::Separator();
    ImGui::Text("Ionosphere");
    HelpMarker("Color shell over the globe. Pick the variable (real TEC or GIRO/HF interpolations) and it is painted with its colormap.");
    // Variable de la capa de colores esferica (Seccion A + Fase 5):
    // TEC / GIRO (foF2,MUF,hmF2,NmF2) / Radio (LUF,FOT,Spread-F).
    const char* mapVars[] = { "TEC", "foF2", "MUF", "hmF2", "NmF2", "LUF", "FOT", "Spread-F" };
    if (ImGui::Combo("Variable", &impl->mapVariable, mapVars, 8)) {
        applyColorLayer();
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("TEC: electron content (TECU, NOAA GloTEC).\nfoF2: F2-layer critical frequency (MHz, GIRO).\nMUF: max usable at 3000 km (MHz, GIRO).\nhmF2: F2 peak height (km, GIRO).\nNmF2: F2 peak density (e/m3, from foF2).\nLUF: lowest usable (MHz, ITU-style derived).\nFOT: optimum working = 0.85*MUF (MHz).\nSpread-F: % spread-echo occurrence (diverging).");
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
        impl->tecLayer.setAltitude(impl->altKm / 6371.0f);
        impl->shellLayer.setAltitude(impl->altKm / 6371.0f + 0.025f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Height of the color shell above the surface (100-800 km). The outer shell follows +0.025. Visual only.");
    if (ImGui::SliderFloat("TEC opacity", &impl->tecOpacity, 0.0f, 2.0f)) {
        impl->tecLayer.setOpacity(impl->tecOpacity);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Opacity of the color layer (applies to any variable, not just TEC).");
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
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Second nested isosurface (inferno, +0.025 radius, 0.45 opacity) sharing the same grid.");
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
    if (ImGui::SliderFloat("Volume opacity", &impl->volumeOpacity, 0.0f, 2.5f)) {
        impl->volumeOpacity = glm::clamp(impl->volumeOpacity, 0.0f, 2.5f);
        impl->volumeLayer.setOpacityScale(impl->volumeOpacity);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Brightness of the volumetric rendering. Visual only.");
    if (ImGui::Checkbox("Path slice", &impl->sliceOn)) {
        impl->sliceLayer.setVisible(impl->sliceOn);
        if (!impl->sliceOn) { impl->sliceA.clear(); impl->sliceB.clear(); }
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Vertical Ne(h) curtain between the pinned station (A, click to pin) and the hovered station (B). Same volume data, viridis scale.");
    if (impl->sliceOn) {
        if (!impl->sliceA.empty() && !impl->sliceB.empty() && impl->sliceLayer.hasData()) {
            ImGui::Text("%s -> %s  pk %.1f", impl->sliceA.c_str(), impl->sliceB.c_str(), impl->slicePeakLog);
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Path endpoints and peak log10 electron density [m^-3] along it. Higher peak = easier reflection on HF.");
        } else {
            ImGui::TextDisabled("Pin a station (A), hover another (B).");
        }
    }
    if (ImGui::Checkbox("Ref shells E/F1/F2", &impl->refShellsVisible)) {
        impl->refShells.setVisible(impl->refShellsVisible);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Reference altitude rings: E fixed at 110 km, F1/F2 follow the measured hmF2 median (dynamic). No data of their own, just height cues.");

    // ---- GIRO ionosondas (Fase 3) ----
    ImGui::Separator();
    bool showGIRO = impl->giroStationsVisible;
    if (ImGui::Checkbox("GIRO stations", &showGIRO)) {
        impl->giroStationsVisible = showGIRO;
        impl->ionosondeLayer.setVisible(showGIRO);
    }
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("GIRO network (~129 ionosondes). Each dot is a station with foF2/MUF/hmF2/NmF2 from scaled.php. Hover the globe for details, click to pin.");
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
        ImGui::Text("GIRO: %d pts  (%.0f%% data)", impl->ionosondeLayer.getStationCount(),
                    impl->ionosondeLayer.getDataIndicator());
        ImGui::Text("Range: %.2f .. %.2f", minV, maxV);
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
                // Sparkline foF2 de la serie GIRO (~2 h, 5 min).
                {
                    std::vector<float> fh;
                    for (const auto& s : impl->giro.copySamples()) {
                        if (s.code != sp->code) continue;
                        for (const auto& h : s.history) fh.push_back(h.foF2);
                        break;
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
                    if (sp && sp->code == impl->pinnedCode) impl->pinnedCode.clear();  // toggle off
                    else if (sp) impl->pinnedCode = sp->code;
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

    // --- Disco solar (D2+D3): rotacion de Carrington + manchas geolocalizadas ---
    ImGui::Separator();
    time_t nowEpoch = time(nullptr);
    int cr = SolarWindAdapter::getCarringtonRotation((double)nowEpoch);
    ImGui::Text("Solar disk  (Carrington %d)", cr);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Carrington rotation (~27.27 days). Black dots are simulated spots (SSN); didactic disk, not a real image.");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetCursorScreenPos();
    const float dd = 96.0f;   // diametro del disco en px
    ImVec2 c0(wp.x + dd * 0.5f, wp.y + dd * 0.5f);
    dl->AddCircleFilled(c0, dd * 0.5f, IM_COL32(240, 180, 40, 255), 64);
    // Manchas solares (regiones activas) simuladas, geolocalizadas por
    // longitud de Carrington y latitud heliografica.
    static const float spots[][3] = {
        { 10.0f, 12.0f, 1.0f }, { -30.0f, -8.0f, 0.7f }, { 55.0f, 20.0f, 0.5f },
        { -55.0f, -15.0f, 0.6f }, { 20.0f, -25.0f, 0.4f }, { -80.0f, 5.0f, 0.5f },
    };
    const float R = dd * 0.48f;
    for (const auto& sp : spots) {
        float clon = sp[0] * 3.14159265f / 180.0f;
        float blat = sp[1] * 3.14159265f / 180.0f;
        float px = c0.x + R * cosf(blat) * sinf(clon);
        float py = c0.y - R * sinf(blat);
        float pr = dd * 0.5f * (0.06f + 0.05f * sp[2]);
        dl->AddCircleFilled(ImVec2(px, py), pr, IM_COL32(20, 10, 5, 255), 20);
        dl->AddCircle(ImVec2(px, py), pr * 1.6f, IM_COL32(180, 100, 30, 160), 20);
    }
    ImGui::Dummy(ImVec2(dd, dd + 6.0f));
    ImGui::Text("Spots: NOAA SWPC SSN (sim)");
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Spots placed by Carrington longitude (simulated). The real SSN is in Space Weather.");

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
        if (tecStatusB->simulated) {
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
    float stMuf = 0, stFot = 0, stLuf = 0, stS4 = 0, stFar = 0, stSpF = 0;
    bool stHasData = false;
    if (rpStation) {
        stHasData = rpStation->valid && rpStation->muf > 0.0f;
        if (stHasData) {
            stMuf = rpStation->muf;
            stFot = RadioPropagationAdapter::deriveFOT(stMuf);
            stLuf = RadioPropagationAdapter::deriveLUF(stMuf, rpStation->foF2, rpStation->latDeg, rpStation->lonDeg, rpEpoch);
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
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Station source: GIRO scaled.php (foF2/MUF measured, rest derived). Click the station again or empty space to unpin.");
        if (rpPinned && ImGui::SmallButton("Unpin")) impl->pinnedCode.clear();
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
    ImGui::Text("MUF: %.1f MHz", showMuf);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip(rpStation ? "MUF(D): highest usable frequency at 3000 km (MHz), measured at this station. Above it there is no F2 reflection." : "MUF(D): highest usable frequency at 3000 km (MHz). Global GIRO median. Above it there is no F2 reflection.");
    ImGui::Text("FOT (0.85*MUF): %.1f MHz", showFot);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("FOT = 0.85*MUF (ITU). Optimum working frequency: reliable ~90% of the time. Transmit here.");
    ImGui::Text("LUF: %.1f MHz", showLuf);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("LUF: lowest usable frequency (MHz, ITU-style with daytime D-layer absorption). Below it the D layer absorbs the signal. Rises by day.");
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
    else if (rp.valid && rpGiro)
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "(GIRO real)");
    if (!rpErr.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", rpErr.c_str());
    ImGui::End();

    // ---- Legend: active color scale of the spherical layer (units + range) ----
    {
        static const char* LEG_NAMES[8] = { "TEC", "foF2", "MUF", "hmF2", "NmF2", "LUF", "FOT", "Spread-F" };
        static const char* LEG_UNITS[8] = { "TECU", "MHz", "MHz", "km", "e/m3", "MHz", "MHz", "%" };
        int li = glm::clamp(impl->mapVariable, 0, 7);
        ImGui::SetNextWindowPos(ImVec2(10, (float)winH - 130), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(220, 110), ImGuiCond_FirstUseEver);
        ImGui::Begin("Legend");
        ImGui::Text("%s [%s] (%s)", LEG_NAMES[li], LEG_UNITS[li], impl->tecLayer.getColormapName().c_str());
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Active scale of the spherical color layer. It follows the Variable selector and the manual Colormap override.");
        if (impl->peakShell.isVisible()) ImGui::TextDisabled("F2peak [logNe] (plasma)");
        if (impl->tecColShell.isVisible()) ImGui::TextDisabled("modelTEC [TECU] (viridis)");
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

    // ---- Timeline: Live vs session replay of the TEC layer ----
    {
        ImGui::SetNextWindowPos(ImVec2((float)winW * 0.5f - 190.0f, (float)winH - 96.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 86), ImGuiCond_FirstUseEver);
        ImGui::Begin("Timeline");
        int nfr = 0;
        { std::lock_guard<std::mutex> tlk(impl->tecHistMutex); nfr = (int)impl->tecHist.size(); }
        if (!impl->replayMode) {
            if (impl->histLoading.load()) {
                ImGui::Text("LIVE  (loading history %d/%d...)",
                    impl->histDone.load(), impl->histTotal.load());
                if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Downloading past GloTEC frames in the background for instant replay.");
            } else {
                ImGui::Text("LIVE  (%d frames)", nfr);
            }
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Live mode shows the latest TEC grid. Frames (live + preloaded history, up to 36) feed the replay.");
            if (nfr >= 2) {
                if (ImGui::Button("Replay session")) {
                    impl->replayMode = true;
                    impl->replayPos = 0.0;
                    impl->replayPlaying = true;
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
            int cur = (int)impl->replayPos;
            if (ImGui::SliderInt("Frame", &cur, 0, nfr - 1)) {
                impl->replayPos = (double)cur;
                impl->replayPlaying = false;  // scrubbing pauses
            }
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Scrub through stored frames (pauses playback).");
            ImGui::SameLine();
            size_t i0 = (size_t)glm::clamp(cur, 0, nfr - 1);
            char tbuf[32] = "--:--";
            {
                std::time_t tt = (std::time_t)impl->tecHist[i0].timestamp;
                std::tm g;
#ifdef _WIN32
                gmtime_s(&g, &tt);
#else
                gmtime_r(&tt, &g);
#endif
                strftime(tbuf, sizeof(tbuf), "%H:%M UTC", &g);
            }
            ImGui::TextUnformatted(tbuf);
            if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Frame time (UTC) of the stored TEC grid.");
        }
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }   // !uiHidden
}

void App::shutdown() {
    saveSettings();
    impl->shutdownRequested.store(true);
    if (impl->fetchThread.joinable()) impl->fetchThread.join();
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
