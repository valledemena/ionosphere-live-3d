#version 400 core
in vec2 vUV;
uniform sampler2D u_current;
uniform sampler2D u_history;
uniform float u_alpha;
uniform vec2 u_texel;
out vec4 fragColor;
void main() {
    vec3 cur = texture(u_current, vUV).rgb;
    vec3 hist = texture(u_history, vUV).rgb;
    // Sin clamp vecinal: 2 fetches en vez de 10. El ghosting se controla
    // con reset de historia (dato, replay, camara, resize) y alpha alto.
    fragColor = vec4(mix(hist, cur, u_alpha), 1.0);
}
