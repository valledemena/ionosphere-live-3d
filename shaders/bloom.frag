#version 400 core
in vec2 vUV;
uniform sampler2D u_texture;
uniform vec2 u_direction;
uniform vec2 u_resolution;
uniform float u_threshold;
uniform float u_intensity;
out vec4 fragColor;

void main() {
    // Extraer SOLO brillos (píxeles con luminancia alta): elimina el halo uniforme
    // que "agrandaba" el globo al difuminar toda la escena iluminada.
    vec3 bright = max(texture(u_texture, vUV).rgb - vec3(u_threshold), vec3(0.0));

    float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 result = bright * weight[0];
    vec2 texel = u_direction / u_resolution;
    for (int i = 1; i < 5; i++) {
        result += max(texture(u_texture, vUV + texel * float(i)).rgb - vec3(u_threshold), vec3(0.0)) * weight[i];
        result += max(texture(u_texture, vUV - texel * float(i)).rgb - vec3(u_threshold), vec3(0.0)) * weight[i];
    }
    fragColor = vec4(result * u_intensity, 1.0);
}
