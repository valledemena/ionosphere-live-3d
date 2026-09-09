#version 400 core
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vFragPos;

uniform sampler2D u_texture;
uniform sampler2D u_nightTex;  // Black Marble (misma proyeccion equirectangular)
uniform vec3 u_sunDir;
uniform vec3 u_cameraPos;
uniform float u_night;   // night brightness 0..1 (slider, default 0.45)
uniform float u_time;    // segundos (deriva de nubes)
uniform float u_cloudAmt; // 0 = sin nubes, 1 = cubierto (slider, default 0.55)
uniform int u_atmoOn;     // M6 (plan 9.3): 1 = aerial perspective + sombra de nubes; 0 = camino pre-M6 exacto (aceptacion 6)

out vec4 fragColor;

// Ruido de valor 3D sobre la esfera (sin costuras, sin texturas).
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), u.x),
                   mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), u.x), u.y),
               mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), u.x),
                   mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), u.x), u.y), u.z);
}

float fbm(vec3 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { v += a * vnoise(p); p = p * 2.03 + vec3(1.7); a *= 0.5; }
    return v;
}

// --- M6 (plan 9.3): constantes de aerial perspective ----------------------
// CRUZADAS con shaders/atmosphere.frag y Atmosphere.cpp (mismo bake de LUT):
// unidades R_tierra, sin conversion — mantener las tres copias sincronizadas.
const float PLANET_R = 1.0;
const float H_R      = 8.5e-3;                     // escala Rayleigh (8.5 km)
const float H_M      = 1.2e-2;                     // escala Mie (12 km)
const vec3  BETA_R   = vec3(5.8, 13.5, 33.1) * 0.0105;   // m^-1 -> R_tierra
const float BETA_M   = 21.0 * 0.0105;

void main() {
    vec3 n = normalize(vNormal);
    vec3 sun = normalize(u_sunDir);
    vec3 viewDir = normalize(u_cameraPos - vFragPos);
    float ndl = dot(n, sun);

    // Day amount with a soft twilight band (~ -5..+15 deg around the terminator).
    float dayAmount = smoothstep(-0.08, 0.25, ndl);
    // Warm tint right at the terminator (sunrise/sunset).
    float band = smoothstep(-0.08, 0.02, ndl) * (1.0 - smoothstep(0.02, 0.35, ndl));

    vec3 texColor = texture(u_texture, vTexCoord).rgb;

    // Day: diffuse + slight ambient so shadowed relief is readable.
    vec3 dayCol = texColor * (0.12 + 1.05 * dayAmount);
    dayCol += vec3(0.38, 0.13, 0.03) * band * (0.35 + 0.65 * dayAmount);

    // Ocean mask from the day texture (blue-dominant pixels) for sun glitter.
    // Relaxed thresholds: JPEG compression narrows the blue margin.
    float ocean = smoothstep(0.008, 0.07, texColor.b - max(texColor.r, texColor.g));
    vec3 hv = normalize(sun + viewDir);
    float spec = pow(max(dot(n, hv), 0.0), 36.0) * ocean * dayAmount;
    dayCol += vec3(1.0, 0.95, 0.85) * spec * 0.7;

    // Night: real city lights (Black Marble) scaled by the slider, plus a faint
    // blue floor so oceans stay barely readable. Day is ~1.0*tex.
    vec3 nightTex = texture(u_nightTex, vTexCoord).rgb;
    vec3 nightCol = nightTex * (u_night * 2.5) + vec3(0.004, 0.007, 0.016);

    vec3 col = mix(nightCol, dayCol, dayAmount);

    // M6 (plan 9.3): sombra de nubes — la textura diurna como proxy de
    // cobertura (cov = luminancia ~nubes, umbral calibrable). Va ANTES del
    // mix fbm de nubes (orden sellado por MUSE). shadow = incidencia solar.
    if (u_atmoOn == 1) {
        const float COV_LO = 0.62, COV_HI = 0.88;
        float cov = smoothstep(COV_LO, COV_HI, dot(texColor, vec3(0.3333)));
        col *= 1.0 - 0.22 * cov * max(ndl, 0.0) * dayAmount;   // plan 9.3 tal cual
    }

    // Nubes procedurales: fbm sobre la direccion esferica (sin costuras),
    // deriva lenta con el tiempo. Iluminadas por el sol como la superficie.
    if (u_cloudAmt > 0.001) {
        float ca = cos(u_time * 0.005), sa = sin(u_time * 0.005);
        vec3 rp = vec3(ca * n.x - sa * n.z, n.y, sa * n.x + ca * n.z);
        float f = fbm(rp * 3.0);
        float t0 = 1.05 - u_cloudAmt * 0.6 + 0.03 * sin(u_time * 0.02 + n.y * 3.0);
        float cov = smoothstep(t0 - 0.18, t0 + 0.22, f) * u_cloudAmt;
        vec3 cloudLit = vec3(1.02, 1.0, 0.98) * (0.04 + 1.05 * dayAmount);
        cloudLit += vec3(0.45, 0.15, 0.03) * band * 0.8;   // atardecer en nubes
        col = mix(col, cloudLit, clamp(cov, 0.0, 0.9));
    }

    // M6 (plan 9.3): aerial perspective — atenuacion por profundidad optica
    // del camino camara->superficie (1 muestra en el punto medio: el camino
    // es corto) + inscatter aproximado (1 - exp(-tau)) * skyTint.
    if (u_atmoOn == 1) {
        vec3 mid = (u_cameraPos + vFragPos) * 0.5;
        float segLen = length(u_cameraPos - vFragPos);
        float hm = max(length(mid) - PLANET_R, 0.0);
        vec3 tau = (BETA_R * exp(-hm / H_R) + vec3(BETA_M * exp(-hm / H_M))) * segLen;
        vec3 atten = exp(-tau);
        // skyTint: azul de dia, naranja a ras del terminador (reusa la banda
        // de arriba); de noche no hay sol que dispersar.
        vec3 skyTint = mix(vec3(0.35, 0.55, 1.0), vec3(1.0, 0.45, 0.15), band);
        skyTint *= 0.04 + 0.96 * dayAmount;
        col = col * atten + (1.0 - atten) * skyTint;
    }

    fragColor = vec4(col, 1.0);
}
