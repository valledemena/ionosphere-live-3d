#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

struct GridData;   // Ionosphere/GridLayer.h (solo referencia en la interfaz)
#include "Data/SdoImageAdapter.h"  // M11: SdoImage (std-only, seguro en header)

class App {
public:
    App();
    ~App();
    bool init(int width, int height, const char* title);
    void run();
    void shutdown();

private:
    void processInput();
    void update(float dt);
    void render();
    void recreateRenderTargets(int w, int h);  // M0.1: resize diferido (FBOs + aspect)
    void applyColorLayer();   // aplica la variable de mapa elegida a la capa de colores esferica
    void applyRelief();       // amplitud de relieve efectiva (storm swell) a ambas cascaras
    void applyAltitudes();    // altitudes base x explodeFactor (vista explotada)
    void updateMainGrid(const GridData& grid);  // TEC + outer shell a la vez
    void pushHistoryFrame(const GridData& grid, double ts, bool simulated = false);
    void uploadSunTexture(const SdoImage& im);  // M11: subida GL (render thread)
    void drawSunPanel();  // M11: ventana Sun (disco SDO + ARs)  // anillo ordenado, dedup, cap 432 + write-through M10
    void drawGeospacePanel();  // Lote 3: panel de datos WSPR / ENLIL / NWS
    void drawSolarImagesPanel();  // Lote 4B: ventana LASCO C2 + D-RAP
    void drawParkerPanel();  // Lote 5D: ventana de la espiral de Parker
    void drawVoyagerPanel();  // Lote 5B: ventana Voyager (JPL Horizons)
    void restoreTecCache();  // M10: anillo + bundle desde cache/ (init)
    void restoreKc2gCache();  // FASE B (P2): historia kc2g desde cache/ (init)
    void saveSettings();      // GUI -> settings.cfg (working dir)
    void loadSettings();      // settings.cfg -> GUI (si existe)
    // Raiz del cache de datos (Paths.h) desde settings.cfg: la clave
    // "cacheDir <ruta>" (con espacios) DEBE aplicarse como PRIMERA instruccion
    // de App::init, antes de los init() de adaptadores (que restauran ficheros
    // de cache) y de la creacion de hilos (histPreload, catalogPreload,
    // irtamBackfill, fetchThread). loadSettings la re-aplica si el fichero
    // cambio (setCacheRoot es no-op si el valor no cambio, con mutex).
    void applyCacheDirectory();
    void fetchWorker();       // hilo background: red+parse, publica bundles
    void consumeBundles();    // hilo principal: aplica bundles frescos a capas/UI
    // M-irtam-replay-024 (ruling R2 PERF): el render NUNCA muestrea — postea
    // peticion idempotente al worker y consume la ultima muestra lista
    // (handoff corto). En miss pinta lo anterior. Render-path wait-free.
    // M-irtam-replay-028: el worker entrega el BRACKET (endpoints) y el
    // render interpola por cursor cada frame (simetria TEC).
    struct IrtamBracketView {
        std::vector<float> a0, a1, b0, b1;  // mallas 45x46 row-major E2
        double t0 = 0.0, t1 = 0.0;          // TOVs del bracket
        bool have = false, clamped = false;
    };
    bool consumeIrtamBracket(int pairKind, IrtamBracketView& out);
    // M-irtam-replay-028: cuerpo del worker (HILO SAMPLER, nunca render).
    bool sampleIrtamBracketWorker(int pairKind, double epoch,
                                  long long slot);
    void rebuildSlice(bool force);  // cortina A=pinned B=hover desde lastVolume
    float currentKp() const;  // Kp del ultimo bundle (2.0 si no hay)

    GLFWwindow* window = nullptr;
    int winW = 1280, winH = 720;
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    double mouseX = 0.0, mouseY = 0.0;
    bool firstMouse = true;
    float lastMouseX = 640.0f, lastMouseY = 360.0f;

    // Forward declarations via pimpl-like
    struct Impl;
    Impl* impl = nullptr;
};
