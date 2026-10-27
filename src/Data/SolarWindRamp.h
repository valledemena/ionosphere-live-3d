#pragma once
#include <glm/glm.hpp>
#include <algorithm>

// Rampa de color del viento solar, portada literal de la demo web
// (IonosphereScene.tsx: SOLAR_WIND_STOPS + solarWindColor). Vive en un header
// porque la usan dos capas que deben pintar igual: las flechas sobre el globo
// (SolarWindLayer) y el rio de particulas (SolarWindRiver).
//
//   speedN  = sat((v - 250) / 650)
//   southN  = sat(-Bz / 15)
//   t       = sat(0.28 + 0.55*speedN - 0.16*southN + 0.13*seed)
//   bright  = 0.86 + 0.14*sat(density / 20)
//
// seed es un desfase [0,1) por elemento (la web usa i*0.61803398875 mod 1) para
// que las particulas/flechas no salgan todas del mismo color exacto.
namespace windramp {

struct Stop {
    float t;
    glm::vec3 c;
};

constexpr int kStopCount = 5;

// violeta -> magenta -> naranja -> amarillo -> blanco calido
inline const Stop* stops() {
    static const Stop kStops[kStopCount] = {
        { 0.00f, glm::vec3(0.45f, 0.22f, 0.95f) },
        { 0.32f, glm::vec3(0.95f, 0.25f, 0.80f) },
        { 0.55f, glm::vec3(1.00f, 0.48f, 0.16f) },
        { 0.78f, glm::vec3(1.00f, 0.78f, 0.28f) },
        { 1.00f, glm::vec3(1.00f, 0.95f, 0.78f) },
    };
    return kStops;
}

inline float saturate(float v) { return std::min(1.0f, std::max(0.0f, v)); }

inline glm::vec3 webColor(float speedKms, float bzNT, float densityCm3, float seed01) {
    const Stop* s = stops();
    const float speedN = saturate((speedKms - 250.0f) / 650.0f);
    const float southN = saturate(-bzNT / 15.0f);
    const float t = saturate(0.28f + 0.55f * speedN - 0.16f * southN + 0.13f * seed01);
    int i = 0;
    while (i < kStopCount - 2 && t > s[i + 1].t) i++;
    const float span = s[i + 1].t - s[i].t + 1e-6f;
    const float f = (t - s[i].t) / span;
    const float bright = 0.86f + 0.14f * saturate(densityCm3 / 20.0f);
    return (s[i].c + f * (s[i + 1].c - s[i].c)) * bright;
}

}  // namespace windramp
