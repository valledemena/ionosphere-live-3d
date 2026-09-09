#version 400 core
// M8 (plan 11.3): color por viabilidad del salto + pulso viajero.
in float vT;
in float vViable;
uniform float u_time;
out vec4 fragColor;
void main() {
    vec3 col = vec3(1.0, 0.35, 0.35);                    // cerrado (rojo)
    if (vViable > 0.75)      col = vec3(0.30, 1.00, 0.40);  // abierto (verde)
    else if (vViable > 0.25) col = vec3(1.00, 0.75, 0.30);  // marginal (ambar)
    float ph = fract(vT * 3.0 - u_time * 0.15);
    float pulse = 0.6 * exp(-40.0 * ph * ph);
    fragColor = vec4(col + vec3(pulse), 0.55);
}
