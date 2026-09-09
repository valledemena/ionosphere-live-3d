#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Muestra de una estacion para render (copia ligera de StationSample de GiroAdapter,
// sin dependencias de red, para que la capa no conozca el adapter).
struct StationPoint {
    float latDeg = 0.0f;
    float lonDeg = 0.0f;
    float foF2 = 0.0f;   // MHz
    float muf = 0.0f;    // MHz
    float hmF2 = 0.0f;   // km
    float nmF2 = 0.0f;   // m^-3
    float foF1 = 0.0f;   // MHz
    float foE = 0.0f;    // MHz
    float foEs = 0.0f;   // MHz
    float fxI = 0.0f;    // MHz
    float hF2 = 0.0f;    // km (virtual)
    float hF = 0.0f;     // km (virtual)
    float hE = 0.0f;     // km (virtual)
    float hEs = 0.0f;    // km (virtual)
    float B0 = 0.0f;     // km (IRI thickness)
    float B1 = 0.0f;     // IRI shape
    float fmin = 0.0f;   // MHz
    std::string code;
    std::string name;
    bool valid = false;
    double timeUtc = 0.0;   // M4R-A: epoch de la fila (edad en paneles)
    bool stale = false;     // M4R-A: fila kc2g 6-24 h o EG931 (panel si, grids no)
};

// IonosondeLayer: renderiza estaciones de sondeo ionosferico (red GIRO) como
// puntos 3D sobre el globo, coloreados por una magnitud escalada (foF2, MUF, hmF2
// o NmF2) con un colormap, y proporciona hit-testing para tooltips.
//
// Convencion de coordenadas (coincide con la esfera del GridLayer):
//   phi = (90 - lat) en radianes  (0 = norte, pi = sur)
//   theta = lon en radianes
//   x = r*sin(phi)*cos(theta), y = r*cos(phi), z = r*sin(phi)*sin(theta)
class IonosondeLayer {
public:
    IonosondeLayer();
    ~IonosondeLayer();

    void init();
    void updateSamples(const std::vector<StationPoint>& samples);
    void render(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    // Modo de coloracion: 0=foF2, 1=MUF, 2=hmF2, 3=NmF2
    void setMode(int mode) { mode_ = (mode % 4 + 4) % 4; rebuildColors_ = true; }
    int getMode() const { return mode_; }

    // Color de los puntos por colormap. Con nombre vacio ("") se usa el colormap
    // automatico asociado a la metric seleccionada; con un nombre de paleta concreto
    // (viridis/inferno/plasma/turbo) se sobreescribe (override manual) ese colormap.
    void setPointColormapOverride(const std::string& name) {
        pointColormapOverride_ = !name.empty();
        pointColormapOverrideName_ = name;
        rebuildColors_ = true;
    }
    bool hasPointColormapOverride() const { return pointColormapOverride_; }

    void setVisible(bool v) { visible_ = v; }
    bool isVisible() const { return visible_; }

    void setHeightOffset(float alt) { heightOffset_ = glm::clamp(alt, 0.0f, 1.0f); }
    float getHeightOffset() const { return heightOffset_; }

    void setPointSize(float s) { pointSize_ = glm::clamp(s, 2.0f, 30.0f); }
    float getPointSize() const { return pointSize_; }

    bool isFetching() const { return fetching_; }
    void setFetching(bool f) { fetching_ = f; }
    int getProgress() const { return progress_; }
    void setProgress(int p) { progress_ = p; }
    float getDataIndicator() const { return dataIndicator_; }
    void setDataIndicator(float d) { dataIndicator_ = d; }

    int getStationCount() const { return (int)samples_.size(); }

    // Hit-test: dado el cursor en NDC (x,y en [-1,1]) y la matriz inversa de la
    // camara, devuelve el indice de la estacion mas cercana al punto del globo que
    // intersecta el rayo, o -1 si no hay ninguna dentro del umbral angular.
    int hitTest(float ndcX, float ndcY, const glm::mat4& view, const glm::mat4& projection,
                float radius) const;
    const StationPoint* getSample(int index) const;

    // Retorna el minimo/maximo de la magnitud activa (para normalizacion de color/label).
    void getActiveRange(float& minV, float& maxV) const;
    const std::string& getModeName() const;

    std::vector<StationPoint> samples_;
    std::vector<float> colormap_;

private:
    void rebuild();
    void buildColormap();
    float magnitudeOf(const StationPoint& s) const;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;
    int pointCount_ = 0;

    bool visible_ = false;
    int mode_ = 0;
    float heightOffset_ = 0.05f;   // ~318 km sobre el radio unitario
    float pointSize_ = 10.0f;
    bool fetching_ = false;
    int progress_ = 0;
    float dataIndicator_ = 0.0f;
    bool rebuildColors_ = true;
    bool pointColormapOverride_ = false;
    std::string pointColormapOverrideName_;
};
