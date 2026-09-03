#version 400 core
in vec2 vUV;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform bool u_bloomEnabled;
out vec4 fragColor;

void main() {
    vec3 scene = texture(u_scene, vUV).rgb;
    vec3 bloom = texture(u_bloom, vUV).rgb;
    vec3 col = scene;
    if (u_bloomEnabled) col += bloom * 0.4;
    fragColor = vec4(col, 1.0);
}
