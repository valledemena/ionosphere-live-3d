#version 400 core
// M6 (plan 9.1): single scattering Rayleigh/Mie con sombra dura del
// planeta. REEMPLAZO TOTAL de la cascara Fresnel pow(3) (dos colores
// constantes por sunAlign, sin profundidad optica) — aceptacion 6: es
// reemplazo, no adicion. El fallback loadFromSource de App.cpp es ESPEJO
// de este fichero (patron M1): m6_edits.py genera ambas cadenas desde el
// mismo contenido (sync por construccion, no copia a mano).
// Unidades normalizadas a radio terrestre = 1.0 (constantes del plan 9.1,
// sin convertir).
in vec3 vWorldPos;   // P1: mundo POST-escala — rd exacto

uniform vec3 u_sunDir;
uniform vec3 u_cameraPos;
uniform float u_intensity;   // slider 0.25-2 (plan 9.4, default 1.0)
uniform int u_lutMode;       // 0 = High (raymarch 14x5), 1 = LUT (12 + tabla)
uniform sampler2D u_lutTex;  // 256x64 GL_RG32F: (odR, odM) punto->sol (Atmosphere::init)

out vec4 fragColor;

// Constantes (UNIDADES R_tierra — no convertir). CRUZADAS con
// Atmosphere.cpp (bake de la LUT) y earth.frag (aerial perspective):
// mantener las tres copias sincronizadas.
const float PLANET_R = 1.0;
const float ATM_R    = 1.0255;               // tope ~162 km; = escala 1.0255f de App.cpp
const float H_R      = 8.5e-3;               // escala Rayleigh (8.5 km)
const float H_M      = 1.2e-2;               // escala Mie (12 km)
const vec3  BETA_R   = vec3(5.8, 13.5, 33.1) * 0.0105;   // m^-1 -> R_tierra
const float BETA_M   = 21.0 * 0.0105;
const float G_MIE    = 0.76;

const float PI = 3.14159265358979;

// P1: AMBOS radicales del rayo-esfera (el plan 9.1 daba el near — t de
// entrada; el far — t de salida de la atmosfera — es el que la marcha
// necesita).
float raySphereNear(vec3 ro, vec3 rd, float r) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r * r;
    float h = b * b - c;
    if (h < 0.0) return -1.0;
    return -b - sqrt(h);
}
float raySphereFar(vec3 ro, vec3 rd, float r) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r * r;
    float h = b * b - c;
    if (h < 0.0) return -1.0;
    return -b + sqrt(h);
}

// Extincion (plan 9.1): el 1.1 del Mie es calibracion de la demo. Misma
// formula en ambos tramos (sol->muestra y muestra->camara) y tambien tras
// el lookup de la LUT (que guarda od puros).
vec3 extinct(float odR, float odM) {
    return exp(-(BETA_R * odR + vec3(BETA_M * 1.1 * odM)));
}

// Fase Rayleigh (3/16pi)(1 + cos^2) y Mie Henyey-Greenstein (plan 9.1).
float phaseR(float c) { return (3.0 / (16.0 * PI)) * (1.0 + c * c); }
float phaseM(float c) {
    float g2 = G_MIE * G_MIE;
    return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * G_MIE * c, 1.5));
}

void main() {
    vec3 sun = normalize(u_sunDir);
    vec3 ro = u_cameraPos;
    vec3 rd = normalize(vWorldPos - u_cameraPos);   // exacto (P1)

    // La camara orbita SIEMPRE fuera de ATM_R (clamp [1.5, 20] R_tierra):
    // tEnter > 0 para todo rayo que toca la cascara (hemisferio trasero,
    // ver Atmosphere::render).
    float tEnter = max(raySphereNear(ro, rd, ATM_R), 0.0);
    float tExit  = raySphereFar(ro, rd, ATM_R);
    if (tExit <= tEnter) { fragColor = vec4(0.0); return; }
    // El disco del planeta lo mata el depth test (Tierra dibujada antes con
    // depth write ON); el clip analitico es defensivo (plan 9.2).
    float tPlanet = raySphereNear(ro, rd, PLANET_R);
    if (tPlanet >= 0.0) tExit = min(tExit, tPlanet);
    if (tExit <= tEnter) { fragColor = vec4(0.0); return; }

    int steps = (u_lutMode == 1) ? 12 : 14;   // plan 9.4: LUT = 12, High = 14
    float dt = (tExit - tEnter) / float(steps);

    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);
    float odR = 0.0, odM = 0.0;   // profundidad optica camara->muestra

    for (int i = 0; i < steps; i++) {
        vec3 p = ro + rd * (tEnter + (float(i) + 0.5) * dt);
        float h = max(length(p) - PLANET_R, 0.0);
        float dR = exp(-h / H_R);
        float dM = exp(-h / H_M);
        odR += dR * dt;
        odM += dM * dt;

        // Sombra dura del planeta (el detalle que vende el atardecer): sin
        // luz directa si el rayo muestra->sol toca el planeta.
        if (raySphereNear(p, sun, PLANET_R) < 0.0) {
            float sunOdR = 0.0, sunOdM = 0.0;
            if (u_lutMode == 1) {
                // LUT: mismos ejes que el bake de Atmosphere::init
                // (u = cos del cenit solar, v = altura normalizada).
                vec2 uv = vec2(dot(normalize(p), sun) * 0.5 + 0.5,
                               h / (ATM_R - PLANET_R));
                vec2 od = texture(u_lutTex, uv).rg;
                sunOdR = od.r;
                sunOdM = od.g;
            } else {
                // Marcha al sol: 5 muestras (plan 9.1) hasta salir de ATM_R.
                float tSun = max(raySphereFar(p, sun, ATM_R), 0.0);
                float ds = tSun / 5.0;
                for (int j = 0; j < 5; j++) {
                    vec3 q = p + sun * ((float(j) + 0.5) * ds);
                    float hq = max(length(q) - PLANET_R, 0.0);
                    sunOdR += exp(-hq / H_R) * ds;
                    sunOdM += exp(-hq / H_M) * ds;
                }
            }
            vec3 T = extinct(sunOdR, sunOdM) * extinct(odR, odM);
            sumR += T * (dR * dt);
            sumM += T * (dM * dt);
        }
    }

    float mu = dot(rd, sun);   // coseno del angulo de dispersion
    vec3 col = (sumR * BETA_R * phaseR(mu) + sumM * BETA_M * phaseM(mu))
               * u_intensity * 20.0;   // factor 20 calibrado en la demo (plan 9.1)
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    fragColor = vec4(col, lum);   // alpha = luminancia (plan 9.1)
}
