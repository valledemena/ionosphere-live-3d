#version 400 core
in float vAlpha;
out vec4 fragColor;

void main() {
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float d = dot(c, c);
    float a = smoothstep(1.0, 0.0, d) * vAlpha;
    fragColor = vec4(1.0, 1.0, 0.95, a);
}
