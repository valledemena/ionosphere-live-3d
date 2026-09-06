#version 400 core
in vec2 vUV;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform bool u_bloomEnabled;
uniform float u_bloomStrength;
uniform bool u_toneMap;
uniform float u_exposure;
out vec4 fragColor;

vec3 acesFilm(vec3 x) {
    // Aproximacion de Narkowicz, estandar en tiempo real.
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
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
    if (u_toneMap) col = acesFilm(col * u_exposure);
    fragColor = vec4(col, 1.0);
}
