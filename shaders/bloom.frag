#version 400 core
in vec2 vUV;
uniform sampler2D u_texture;
uniform vec2 u_direction;
uniform vec2 u_resolution;
uniform float u_threshold;
uniform float u_intensity;
uniform int u_pass;   // M1: 0 = pasada H, 1 = pasada V (la ultima)
out vec4 fragColor;

// M1: bright-pass con soft-knee (smoothstep sobre la LUMINANCIA) en vez de
// resta dura por canal: sigue extrayendo SOLO brillos (sin el halo uniforme
// que "agrandaba" el globo al difuminar toda la escena iluminada), pero el
// peso entra suave alrededor del umbral: sin bordes duros ni popping al
// mover el threshold.
vec3 brightPass(vec3 c) {
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c * smoothstep(u_threshold, u_threshold + 0.25, l);
}

void main() {
    float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 result = brightPass(texture(u_texture, vUV).rgb) * weight[0];
    vec2 texel = u_direction / u_resolution;
    for (int i = 1; i < 5; i++) {
        result += brightPass(texture(u_texture, vUV + texel * float(i)).rgb) * weight[i];
        result += brightPass(texture(u_texture, vUV - texel * float(i)).rgb) * weight[i];
    }
    // M1: u_intensity SOLO en la pasada vertical (la final). Antes se
    // aplicaba en H y en V -> intensidad efectiva = u_intensity^2.
    if (u_pass == 1) result *= u_intensity;
    fragColor = vec4(result, 1.0);
}
