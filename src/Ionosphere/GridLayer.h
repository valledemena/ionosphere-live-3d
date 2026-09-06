#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct GridData {
    int width = 0;
    int height = 0;
    std::vector<float> values;
    double timestamp = 0.0;
    bool valid = false;
};

// GridLayer: capa superficial esferica que renderiza una rejilla 2D de valores
// (TEC, hmF2, NmF2, ...) sobre el globo. Parametrizable via:
//   - setColormap(nombre)  -> viridis, inferno, plasma, turbo
//   - setRange(min, max)   -> normalizacion fija (si no se fija, auto min/max)
//   - setHeightOffset(alt) -> radio = 1.0 + alt (pasado como radio del planeta)
class GridLayer {
public:
    GridLayer();
    ~GridLayer();

    void init();
    void updateFromGrid(const GridData& grid);
    // Geometria de alturas reales + color de valores (Fase 1, plan
    // volume-derived-shells): r = 1 + hKm/6371 (+ heightOffset_ de ajuste).
    // heights en km, mismo layout; se interpola bilineal igual que values.
    void updateFromGrids(const GridData& values, const GridData& heights);
    void render(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    // Parametros
    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }

    float getHeightOffset() const { return heightOffset_; }
    void setHeightOffset(float alt) {
        alt = glm::clamp(alt, 0.0f, 1.0f);
        if (alt != heightOffset_) { heightOffset_ = alt; meshDirty_ = true; }
    }

    // Alias de compatibilidad con la antigua TecLayer.
    float getAltitude() const { return heightOffset_; }
    void setAltitude(float alt) { setHeightOffset(alt); }

    float getOpacity() const { return opacity_; }
    void setOpacity(float o) { opacity_ = glm::clamp(o, 0.0f, 1.0f); }

    // Colormap por nombre: "viridis", "inferno", "plasma", "turbo", "aurora"
    void setColormap(const std::string& name);
    // Relieve 3D (estilo imagen volumetric): desplaza cada vertice en radial
    // segun el valor normalizado: r = 1 + heightOffset + displace * n.
    // 0 = esfera perfecta. Tipico 0.02-0.06 para tormentas visibles.
    void setDisplace(float a);
    float getDisplace() const { return displaceAmp_; }
    // Relieve centrado: desplaza (valor - midRango)/rango en vez de
    // valor-normalizado (para hmF2: alturas reales sobre/bajo la media,
    // dos tormentas con igual TEC pero distinta hmF2 ya no se ven iguales).
    void setDisplaceCentered(bool c);
    const std::string& getColormapName() const { return colormapName_; }
    // Paleta activa (256 entradas RGB 0..1) para dibujar leyendas sincronizadas.
    const std::vector<float>& getColormap() const { return colormap_; }
    // Rango mostrado (fijo si setRange, si no min/max del ultimo grid).
    void getDisplayRange(float& minV, float& maxV) const { minV = lastMin_; maxV = lastMax_; }

    // Rango fijo de normalizacion. Si no se llama, se usa min/max de los datos.
    void setRange(float minV, float maxV) { hasFixedRange_ = true; rangeMin_ = minV; rangeMax_ = maxV; }
    void clearRange() { hasFixedRange_ = false; }

    const GridData& getGrid() const { return grid_; }

private:
    void createMesh();
    void updateTexture();
    void buildColormap(const std::string& name);
    // Valor normalizado 0..1 del grid en coords UV de malla (bilineal, lon con wrap).
    float sampleNorm(float u, float v) const;
    // Valor crudo (sin normalizar) en coords UV.
    float sampleRaw(float u, float v) const;
    // Recalcula posiciones de vertices (mismo orden que createMesh).
    void buildPositions(std::vector<float>& verts) const;
    void updatePositions();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLuint texture_ = 0;
    GLuint shader_ = 0;

    int indexCount_ = 0;
    bool visible_ = false;
    float heightOffset_ = 0.02f;
    float opacity_ = 1.0f;

    bool hasFixedRange_ = false;
    float rangeMin_ = 0.0f;
    float rangeMax_ = 1.0f;

    GridData grid_;
    std::vector<unsigned char> textureData_;
    std::vector<float> colormap_;
    std::string colormapName_ = "viridis";
    float lastMin_ = 0.0f, lastMax_ = 1.0f, rangeMid_ = 0.5f;
    float displaceAmp_ = 0.0f;
    bool displaceCentered_ = false;
    bool absoluteHeights_ = false;  // updateFromGrids: geometria de heightGrid_
    GridData heightGrid_;

    bool meshDirty_ = true;
    bool textureDirty_ = true;
};

// Alias de compatibilidad: GloTecAdapter y App.cpp usan TECGrid (mismo layout).
typedef GridData TECGrid;
