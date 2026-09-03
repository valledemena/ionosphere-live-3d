#version 400 core
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vFragPos;

uniform sampler2D u_texture;
uniform vec3 u_sunDir;

out vec4 fragColor;

void main() {
    vec3 n = normalize(vNormal);
    float diff = max(dot(n, normalize(u_sunDir)), 0.15);
    vec3 texColor = texture(u_texture, vTexCoord).rgb;

    // Slight ambient
    vec3 ambient = texColor * 0.12;
    vec3 diffuse = texColor * diff;

    fragColor = vec4(ambient + diffuse, 1.0);
}
