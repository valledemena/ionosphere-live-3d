#version 400 core
in float vSide;
out vec4 fragColor;

void main() {
    // Terminator line: faint orange line at the day/night boundary
    float edge = 1.0 - abs(vSide - 0.5) * 2.0;
    edge = pow(edge, 8.0); // sharpen
    vec3 col = vec3(1.0, 0.5, 0.2) * edge * 0.6;
    float alpha = edge * 0.4;
    fragColor = vec4(col, alpha);
}
