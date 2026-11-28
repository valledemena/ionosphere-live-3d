#version 400 core
in vec4 gColor;
in float vSide;
out vec4 fragColor;
void main() {
    float coverage = 0.78 + 0.22 * (1.0 - abs(vSide));
    fragColor = vec4(gColor.rgb * coverage, gColor.a);
}
