#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Ionosphere/GridLayer.h"  // GridData (TEC para muestreo local)

// FaradayLayer: vectores 3D (GL_LINES) sobre la esfera que visualizan la
// rotacion de Faraday ionosferica. Cada vector nace radial y se inclina en
// azimut proporcional al angulo de rotacion local (deg a 100 MHz, un salto).
// Longitud ~ |rotacion|, color divergente: rojo = rotacion alta / azul = baja.
// Mismo convenio esferico que GridLayer/IonosondeLayer/SolarWindLayer.
class FaradayLayer {
public:
    FaradayLayer();
    ~FaradayLayer();

    void init();
    // Reconstruye los vectores con el TEC LOCAL bajo cada punto (grid 72x72
    // estilo GloTEC: fila 0 = sur, col 0 = lon -180). Si el grid no es valido
    // usa fallbackTecu. La rotacion local es Omega REAL IGRF a 100 MHz
    // (referencia FARADAY_REF_FREQ_HZ) via helper compartido stationOmegaRad.
    //   sunWorld: se conserva el parametro (compatibilidad de llamada); la
    //   modulacion dia/noche la lleva el TEC real del grid (parches dayF/eqF
    //   eliminados: hoy doble-contaban).
    //   mode: 0=longitud por valor, 1=longitud fija + color por valor.
    void updateData(const GridData& tecGrid, float fallbackTecu, const glm::vec3& sunWorld, int mode);
    // Muestreo bilineal del grid (misma convencion que GridLayer): referencia
    // para la cadena TEC de App (hilo principal). Devuelve fallback si el
    // grid no es valido.
    static float sampleGridTec(const GridData& g, float latDeg, float lonDeg, float fallback);
    void render(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    void setVisible(bool v) { visible_ = v; }
    bool isVisible() const { return visible_; }
    void setHeightOffset(float alt) { heightOffset_ = glm::clamp(alt, 0.0f, 0.15f); }
    float getHeightOffset() const { return heightOffset_; }
    void setScale(float s) { scale_ = glm::clamp(s, 0.0f, 5.0f); rebuild_ = true; }
    float getScale() const { return scale_; }
    int getVectorCount() const { return vectorCount_; }

private:
    void rebuild();
    glm::vec3 latLonToPos(float latDeg, float lonDeg, float r) const;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shader_ = 0;
    int vectorCount_ = 0;

    bool visible_ = false;
    float heightOffset_ = 0.03f;
    float scale_ = 1.0f;
    GridData tecGrid_;
    float fallbackTecu_ = 15.0f;
    glm::vec3 sunWorld_ = glm::vec3(1.0f, 0.0f, 0.0f);
    int mode_ = 0;
    bool rebuild_ = true;
};
