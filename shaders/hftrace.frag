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
    // M9 (paridad demo, literales shaders.ts:450/465-467): cursor 0.22
    // ciclos/s (recorrido 4.5 s), 2.5 pulsos en arco, envolvente
    // 0.9*exp(-36*(p-0.5)^2), ganancia 0.9, alpha 0.9. Direccion
    // TX->RX intacta: la fase sigue siendo (arco*k - t*v).
    float p = fract(u_time * 0.22 - vT * 2.5);
    float pulse = 0.9 * exp(-36.0 * (p - 0.5) * (p - 0.5));
    fragColor = vec4(col + vec3(pulse), 0.9);
}
