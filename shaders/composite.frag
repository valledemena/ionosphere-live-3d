#version 400 core
in vec2 vUV;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform bool u_bloomEnabled;
uniform float u_bloomStrength;
uniform bool u_toneMap;
uniform float u_exposure;
uniform bool u_grade;             // DW.25 V5a: OFF por defecto
uniform float u_grain;
uniform float u_vignette;
uniform float u_ca;
uniform float u_time;
uniform vec2 u_resolution;
out vec4 fragColor;

vec3 acesFilm(vec3 x) {
    // Aproximacion de Narkowicz, estandar en tiempo real.
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Misma cadena scene+bloom+ACES+sRGB que el camino base, para que la CA
// se aplique sobre color YA en LDR (nunca antes del tonemap).
vec3 gradeAt(vec2 uv) {
    vec3 col = texture(u_scene, uv).rgb;
    if (u_bloomEnabled) col += texture(u_bloom, uv).rgb * u_bloomStrength;
    if (u_toneMap) {
        col = acesFilm(col * u_exposure);
        col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));
    }
    return col;
}

void main() {
    vec3 scene = texture(u_scene, vUV).rgb;
    vec3 col = scene;
    if (u_bloomEnabled) {
        vec3 bloom = texture(u_bloom, vUV).rgb;
        // SUMA ADITIVA (no multiplicativa): añade glow real, no escala la escena.
        col += bloom * u_bloomStrength;
    }
    // Tonemapping sobre la SUMA (si se comprimieran por separado, el bloom
    // se saltaria la curva y seguiria quemando). Bypass exacto con u_toneMap.
    if (u_toneMap) {
        col = acesFilm(col * u_exposure);
        // M1: encode sRGB tras ACES. pow LOCAL (NO GL_FRAMEBUFFER_SRGB): el
        // pase categorico 3b y ImGui dibujan DESPUES del composite a pantalla
        // con colores ya pensados en sRGB; el flag global los romperia. Con
        // u_toneMap off el bypass sigue escribiendo lineal (exacto, para A/B).
        col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));
    }

    // DW.25 V5a: TODO en LDR, DESPUES del pow sRGB local. Con u_grade=false
    // no se ejecuta nada y la salida es funcionalmente identica al head b273f0e.
    if (u_grade) {
        if (u_ca > 0.0) {
            vec2 off = (vUV - 0.5) * u_ca;
            vec3 cR = gradeAt(vUV + off);
            vec3 cB = gradeAt(vUV - off);
            col.r = cR.r;
            col.b = cB.b;
        }
        if (u_vignette > 0.0) {
            float aspect = u_resolution.x / max(u_resolution.y, 1.0);
            float d = length((vUV - 0.5) * vec2(aspect, 1.0));
            col *= clamp(1.0 - u_vignette * smoothstep(0.35, 0.85, d), 0.0, 1.0);
        }
        if (u_grain > 0.0) {
            float n = fract(sin(dot(vUV * u_resolution + u_time,
                                    vec2(12.9898, 78.233))) * 43758.5453);
            col += (n - 0.5) * u_grain;
        }
        col = clamp(col, 0.0, 1.0);
    }
    fragColor = vec4(col, 1.0);
}
