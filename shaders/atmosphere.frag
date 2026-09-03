#version 400 core
in vec3 vPos;

uniform vec3 u_sunDir;
uniform vec3 u_cameraPos;

out vec4 fragColor;

void main() {
    vec3 n = normalize(vPos);
    vec3 viewDir = normalize(u_cameraPos - vPos);
    float fresnel = 1.0 - max(dot(viewDir, n), 0.0);
    fresnel = pow(fresnel, 3.0);

    float sunAlign = max(dot(n, normalize(u_sunDir)), 0.0);
    vec3 dayColor = vec3(0.3, 0.6, 1.0);
    vec3 sunsetColor = vec3(1.0, 0.4, 0.1);
    vec3 col = mix(dayColor, sunsetColor, pow(1.0 - sunAlign, 5.0));
    col *= 0.5 + 0.5 * sunAlign;

    fragColor = vec4(col, fresnel * 0.6);
}
