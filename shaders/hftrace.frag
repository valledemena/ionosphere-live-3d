#version 400 core
// M8 (plan 11.3): color por viabilidad del salto + pulso viajero.
in float vT;
in float vViable;
in float vSide;              // M9c: -1..+1 del borde geometrico (perfil del tubo)
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
    // M9fix: mezcla MULTIPLICATIVA col*(0.55+pulse), verbatim demo
    // (shaders.ts:465-467): el cursor hereda el color del tramo; la
    // aditiva col+pulse clipeaba a blanco (1.0+0.9) siempre.
    float p = fract(u_time * 0.22 - vT * 2.5);
    // M9d: envolvente unitaria del cursor (pulse = 0.9*env — la MISMA
    // exp, cero uniforms y cero exp nuevas; twins hfc::haloPulse*).
    float env = exp(-36.0 * (p - 0.5) * (p - 0.5));
    float pulse = 0.9 * env;
    // M9c (P1, paridad demo): perfil transversal tubo+halo — s = |vSide|
    // (fraccion del borde geometrico). Nucleo pleno con pedestal 1.35
    // hasta s=0.45 (±0.0068), hombro AA 0.45->0.55 que reemplaza el corte
    // duro M9b (el borde ±0.0075 cae en s=0.5 con 0.85), pedestal 0.35 y
    // aura hasta s=1 (0 = borde geometrico ±0.015 invisible). Twin C++:
    // hfc::traceProfile (tests seccion O). Anclas 0.85/0.26/0 del sello
    // exactas; f(0)=1.35 (correccion aritmetica: el pedestal del halo
    // vive DENTRO del nucleo — sin el, la aura saltaria 0->0.35 en 0.55).
    float s = abs(vSide);
    float core = 1.0 - smoothstep(0.45, 0.55, s);
    float halo = 0.35 * (1.0 - smoothstep(0.55, 1.0, s));
    // M9d (neon ambiental, peticion de DAVID tras la fair shot M9c): el
    // halo RESPIRA con la envolvente del cursor — haloEff =
    // halo*(0.6+0.8*env) (twins hfc::haloPulseFloor/haloPulseGain):
    // x0.6 en reposo (aura tenue), x1.4 al paso de la cabeza (aura
    // encendida que la envuelve). El nucleo NO respira: el cuerpo del
    // tubo queda estable. Twin: hfc::traceProfileBreath(s, env); el
    // perfil estatico M9c (traceProfile) = breath con env = 0.5 exacto.
    float haloEff = halo * (0.6 + 0.8 * env);
    float prof = core + haloEff;
    // M9c (P2): cabeza del pulso — aditivo azul-blanco ACOTADO a la cima
    // (|p-0.5| < 0.12, hfc::pulseHeadBoost) x nucleo transversal (s < 0.45,
    // hfc::pulseHeadGate): el pico lee cian-blanquecino como la demo;
    // fuera de la cabeza add = 0 y el arco queda puro teniendo (M9fix).
    float boost = 1.0 - smoothstep(0.0, 0.12, abs(p - 0.5));
    float gate  = 1.0 - smoothstep(0.15, 0.45, s);
    vec3 add = vec3(0.85, 0.92, 1.0) * (0.55 * boost * gate);
    // M9d: derrame neon de la cabeza — luz TENIDA por el color del tramo
    // (M9fix: nunca blanca) que se sale del tubo por todo el span con
    // caida lineal (twin hfc::glowSpill = 0.30*boost*(1-s)). En rgb Y en
    // alpha: el ribbon mezcla (SRC_ALPHA, ONE) — dst recibe rgb*alpha, y
    // un spill solo-rgb quedaria atenuado a ~0.03 en la falda (alpha
    // ~0.33 ahi); con alpha propio la falda al paso de la cabeza llega a
    // ~3-4x M9c. Collar estrecho (boost) sobre respiracion ancha (env):
    // las dos escalas del neon de la demo.
    float spill = 0.30 * boost * (1.0 - s);
    fragColor = vec4(col * ((0.55 + pulse) * prof + spill) + add,
                     0.9 * prof + spill);
}
