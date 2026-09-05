#version 400 core
in vec2 vUV;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform bool u_bloomEnabled;
uniform float u_bloomStrength;
out vec4 fragColor;

void main() {
    vec3 scene = texture(u_scene, vUV).rgb;
    vec3 col = scene;
    if (u_bloomEnabled) {
        vec3 bloom = texture(u_bloom, vUV).rgb;
        // SUMA ADITIVA (no multiplicativa): añade glow real, no escala la escena.
        col += bloom * u_bloomStrength;
    }
    fragColor = vec4(col, 1.0);
}
