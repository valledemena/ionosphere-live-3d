#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "SolarWindRiverSim.h"

// SolarWindRiver: rio de viento solar del demo web (IonosphereScene.tsx,
// "W-4 #4"), portado como capa propia.
//
// Son 400 particulas (GL_POINTS) que nacen en el lado dia, derivan antisolares
// a v * 60 (exageracion declarada, ver SolarWindRiverSim.h) y se abren
// lateralmente al pasar junto a la magnetopausa Shue-1998, que se evalua con
// el viento solar vivo. Al pasar x = -28 R_E se reciclan.
//
// Escala: 1 unidad = 1 R_E, sin compresion. El rio vive entre 18 y 28 R_E del
// centro, asi que con la camara pegada al globo (3 R_E) no se ve: hay que
// alejarla. El toggle de UI la aleja sola la primera vez.
//
// Dibujo: dos pasadas aditivas con el mismo VAO (nucleo 0.17 R_E / alpha 0.9 +
// halo 0.38 R_E / alpha 0.16), tamano de punto atenuado por distancia como el
// PointsMaterial con sizeAttenuation de three.js. El llamante pone el blend
// aditivo y el depth mask off; esta capa no toca ese estado.
//
// Datos: velocidad, densidad y Bz salen del adaptador de viento solar (nunca
// simulados). Sin muestra valida se conservan los defaults declarados del
// modulo (400 km/s, 5 cm^-3, Bz 0), los mismos que usa MagnetosphereLayer para
// no dibujar una cavidad absurda con Dp = 0.
class SolarWindRiver {
public:
    SolarWindRiver();
    ~SolarWindRiver();

    void init();
    void destroy();

    void setVisible(bool v);
    bool visible() const { return visible_; }
    void setInstanced(bool v);              // DW.23 V3a: flag OFF por defecto
    bool instanced() const { return instanced_; }
    void setTransformFeedback(bool v);      // DW.24 V3b: flag OFF por defecto
    bool transformFeedback() const { return tf_; }
    void setWindStateMode(bool v);          // DW.31 V12: flag OFF por defecto
    bool windStateMode() const { return windStateMode_; }
    static const char* windRegime(float speedKms);

    // Viento vivo. Cada valor no finito se ignora (conserva el ultimo valido).
    void setSolarWind(float speedKms, float densityCm3, float bzNT);
    void setSunDirection(const glm::vec3& sunWorld);

    // Anima el rio y sube las posiciones a GPU. dt en segundos (se acota a
    // 100 ms para que un paron del proceso no teletransporte las particulas).
    void update(float dt);

    // pixelScale = viewportHeight / (2*tan(fov/2)) para el tamano de punto.
    void render(const glm::mat4& view, const glm::mat4& projection, float pixelScale);

    int particleCount() const { return swriver::kCount; }
    float currentSpeed() const { return speedKms_; }
    float currentDensity() const { return densityCm3_; }
    float currentBz() const { return bzNT_; }

private:
    void refreshColors();
    void refreshRotation();
    void upload();
    void initTf();                          // DW.24: recursos de transform feedback
    void stepTf(float dt);
    void refreshTfColors();

    GLuint vao_ = 0;
    GLuint instVao_ = 0;      // DW.23: mismo VBO, divisor 1
    GLuint vbo_ = 0;
    GLuint shader_ = 0;
    GLuint instShader_ = 0;
    bool instanced_ = false;

    // DW.24 V3b: dos buffers de posiciones (marco solar) en ping-pong.
    bool windStateMode_ = false;
    bool tf_ = false;
    GLuint tfProg_ = 0;
    GLuint tfSimVao_[2] = {0, 0};
    GLuint tfPosBuf_[2] = {0, 0};
    GLuint tfColorBuf_ = 0;
    GLuint tfRenderVao_[2] = {0, 0};
    GLuint tfRenderPoints_ = 0;
    GLuint tfRenderInst_ = 0;
    int tfCur_ = 0;
    unsigned int tfFrame_ = 0;
    float tfColors_[swriver::kCount * 3] = {};

    swriver::RiverSim sim_;
    float verts_[swriver::kCount * 6] = {};   // pos(3) + color(3) mundo

    bool visible_ = false;
    bool colorsDirty_ = true;
    float speedKms_ = 400.0f;   // default declarado (igual que la magnetosfera)
    float densityCm3_ = 5.0f;
    float bzNT_ = 0.0f;
    glm::vec3 sun_ = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::mat3 rot_ = glm::mat3(1.0f);

    // Clave de color redondeada: no reescribir colores por ruido numerico.
    bool colorKeyValid_ = false;
    float keySpeed_ = 0.0f;
    float keyBz_ = 0.0f;
    float keyDensity_ = 0.0f;
};
