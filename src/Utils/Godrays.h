#pragma once
// Espejo CPU del pase god rays (M'-2, ruling 142 sec.5). Puro: sin GL, sin
// red. Solo lo consume el TU (patron VolumeShadow del S-2). Misma matematica
// que godrays.frag: 12 taps radiales hacia el sol en UV con correccion de
// aspecto, pesos decay^i, mascara analitica (opcion B: solo la Tierra ocluye)
// y puerta CPU (skip si el sol no esta en pantalla o tras el limbo).
// Requiere glm (header-only, ya vendoreado en libs/).
#include <cmath>

#include <glm/glm.hpp>

namespace godrays {

inline constexpr int NTAPS = 12;
inline constexpr float DECAY = 0.90f;
inline constexpr float MAX_RADIUS = 0.45f;  // fraccion de alto de pantalla
inline constexpr float FALLOFF_K = 8.0f;
inline constexpr float EARTH_R = 1.0f;
inline constexpr float GAIN = 0.12f;  // ganancia global (post-ACES, display)
inline constexpr float SUN_R = 1.0f, SUN_G = 0.90f, SUN_B = 0.75f;  // sol calido

struct SunProj {
    float x = 0.0f, y = 0.0f;  // NDC
    bool behind = true;        // w <= 0 (detras de camara)
    bool offscreen = true;     // |x|>1 o |y|>1
};

// Sol lejano -> clip -> NDC (con w).
inline SunProj projectSun(const glm::mat4& viewProj, const glm::vec3& camPos,
                          const glm::vec3& sunDir) {
    const glm::vec4 clip = viewProj * glm::vec4(camPos + sunDir * 100.0f, 1.0f);
    SunProj s;
    s.behind = (clip.w <= 0.0f);
    if (!s.behind) {
        s.x = clip.x / clip.w;
        s.y = clip.y / clip.w;
        s.offscreen = (s.x < -1.0f || s.x > 1.0f || s.y < -1.0f || s.y > 1.0f);
    }
    return s;
}

// Test rayo-esfera (oclusora = Tierra r=1): true = el rayo cruza por delante.
inline bool rayHitsEarth(const glm::vec3& ro, const glm::vec3& rd) {
    const float b = glm::dot(ro, rd);
    const float c = glm::dot(ro, ro) - EARTH_R * EARTH_R;
    const float disc = b * b - c;
    if (disc < 0.0f) return false;
    return (-b + sqrtf(disc)) > 1e-4f;
}

// Peso del tap i (decay monotono).
inline float tapWeight(int i) { return powf(DECAY, (float)(i < 0 ? 0 : i)); }
inline float falloff(float dist) {
    float f = 1.0f / (1.0f + FALLOFF_K * dist);
    return f < 0.0f ? 0.0f : f;
}

// Mascara del tap: 0 si su rayo visual cruza la Tierra, 1 si va al cielo.
inline float tapMask(const glm::vec3& camPos, const glm::vec3& tapDir) {
    return rayHitsEarth(camPos, tapDir) ? 0.0f : 1.0f;
}

// Posicion UV del tap i entre el pixel y el sol (con correccion de aspecto
// y clamp al radio maximo). i=0 -> el propio pixel.
inline glm::vec2 tapUV(int i, glm::vec2 uv, glm::vec2 sunUV, float aspect) {
    const glm::vec2 auv(uv.x * aspect, uv.y);
    const glm::vec2 asun(sunUV.x * aspect, sunUV.y);
    const glm::vec2 d = asun - auv;
    const float L = glm::length(d);
    if (L < 1e-5f) return uv;
    const float stepLen = (L < MAX_RADIUS ? L : MAX_RADIUS) / 11.0f;
    const glm::vec2 auvTap = auv + (d / L) * (stepLen * (float)i);
    return glm::vec2(auvTap.x / aspect, auvTap.y);
}

// Puerta CPU: el pase NO se invoca si el sol esta detras/off-screen o tras
// el limbo (rayo camara->sol cruza la Tierra). Coste 0 por construccion.
inline bool gateSkip(const SunProj& s, bool sunBehindLimb) {
    return s.behind || s.offscreen || sunBehindLimb;
}

}  // namespace godrays
