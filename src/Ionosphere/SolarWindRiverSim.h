#pragma once
#include "Data/ShueMath.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Rio de viento solar: las 400 particulas del demo web (IonosphereScene.tsx,
// bloque "W-4 #4"), sin OpenGL para poder testearlas.
//
// Marco de trabajo: SOLAR, +x hacia el Sol, 1 unidad = 1 R_E. El llamante rota
// el resultado a espacio de mundo con la direccion solar viva.
//
// Reglas (portadas tal cual, incluida la exageracion declarada):
//   nacimiento   x = 18 - rand*43          -> x en (-25, 18]
//                (y,z) = sqrt(rand)*12 repartidos en un circulo
//   deriva       x -= (v / 6371) * 60 * dt
//   obstaculo    si r < r_Shue(theta)*0.98 se escala (y,z) por ese factor
//   reciclado    si x < -28 vuelve a nacer en x = 18
//
// El factor x60 es una EXAGERACION VISUAL declarada en la web (a 400 km/s el
// flujo real cruzaria 43 R_E en 11 min). La fisica esta en la superficie de
// Shue (shuemath), que es la que decide cuanto se desvia el flujo; el color si
// es fisico (velocidad, Bz y densidad reales).
//
// El clamp escala SOLO (y,z), no x: la particula conserva su posicion a lo
// largo del flujo y se abre lateralmente alrededor de la cavidad. Es el
// comportamiento de la web, no un error de port.
//
// Determinismo: PRNG mulberry32 con semilla fija (la web usa Math.random). Con
// la misma secuencia de dt y viento el rio es reproducible.
namespace swriver {

constexpr int kCount = 400;
constexpr float kSpawnX = 18.0f;        // boca del rio, lado dia
constexpr float kSpawnSpan = 43.0f;     // x = 18 - rand*43
constexpr float kSpawnRadius = 12.0f;   // radio maximo del chorro al nacer
constexpr float kRecycleX = -28.0f;     // se recicla al pasar la cola
constexpr float kExaggeration = 60.0f;  // x60 declarado (ver cabecera)
constexpr float kKmPerEarthRadius = 6371.0f;
constexpr float kClearance = 0.98f;     // 98% del radio de Shue
constexpr uint32_t kSeed = 0x5EED1963u;

constexpr float kPi = 3.14159265358979323846f;

// LCG determinista mulberry32, identico al de la demo web (y al de
// WestFordRing): aritmetica de 32 bits con envolvente, salida en [0,1).
struct Mulberry32 {
    uint32_t a = kSeed;

    float next() {
        a += 0x6d2b79f5u;
        uint32_t t = a;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return (float)((t ^ (t >> 14)) / 4294967296.0);
    }
};

// Desplazamiento por frame en R_E (v[km/s] -> R_E/s * dt * 60 declarado).
inline float stepLength(float dt, float speedKms) {
    if (!(dt > 0.0f) || !(speedKms > 0.0f)) return 0.0f;
    return (speedKms / kKmPerEarthRadius) * kExaggeration * dt;
}

// Factor de empuje radial (>1 si la particula esta dentro de la cavidad) que se
// aplica a (y,z). 1 = ya esta fuera, no se toca. theta se mide desde +x (sol),
// igual que en la web.
inline float obstaclePush(float x, float y, float z, float bzNT, float dpNPa) {
    const float r = std::sqrt(x * x + y * y + z * z);
    if (!(r > 1e-6f)) return 1.0f;
    const float c = std::min(1.0f, std::max(-1.0f, x / r));
    const float rmp = shuemath::radius(std::acos(c), bzNT, dpNPa) * kClearance;
    return (r < rmp) ? (rmp / r) : 1.0f;
}

class RiverSim {
public:
    RiverSim() { reset(kSeed); }

    // Reinicia las 400 particulas con la distribucion inicial de la web.
    void reset(uint32_t seed = kSeed) {
        rng_.a = seed;
        for (int i = 0; i < kCount; ++i) respawn(i);
    }

    // Avanza el rio. dt en segundos (el llamante lo acota), viento en sus
    // unidades habituales (km/s, cm^-3, nT). dt <= 0 no mueve nada.
    void step(float dt, float speedKms, float densityCm3, float bzNT) {
        const float dist = stepLength(dt, speedKms);
        if (dist <= 0.0f) return;
        const float dp = shuemath::dynamicPressure(densityCm3, speedKms);
        for (int i = 0; i < kCount; ++i) {
            float* p = &pos_[i * 3];
            p[0] -= dist;
            const float push = obstaclePush(p[0], p[1], p[2], bzNT, dp);
            if (push > 1.0f) {
                p[1] *= push;
                p[2] *= push;
            }
            if (p[0] < kRecycleX) respawn(i);
        }
    }

    const float* positions() const { return pos_; }
    static constexpr int count() { return kCount; }

    // Solo para tests: coloca una particula a mano.
    void setParticle(int i, float x, float y, float z) {
        if (i < 0 || i >= kCount) return;
        pos_[i * 3] = x;
        pos_[i * 3 + 1] = y;
        pos_[i * 3 + 2] = z;
    }

private:
    void respawn(int i) {
        float* p = &pos_[i * 3];
        p[0] = kSpawnX - rng_.next() * kSpawnSpan;
        const float a = rng_.next() * 2.0f * kPi;
        const float rr = std::sqrt(rng_.next()) * kSpawnRadius;
        p[1] = std::cos(a) * rr;
        p[2] = std::sin(a) * rr;
    }

    float pos_[kCount * 3] = {};
    Mulberry32 rng_;
};

}  // namespace swriver
