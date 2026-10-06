#version 400 core
// God rays screen-space (M'-2, ruling 142 sec.5): 12 taps radiales desde el
// pixel hacia el sol proyectado, con mascara analitica (opcion B: solo la
// Tierra ocluye) y falloff por distancia. Pase fullscreen TRAS el composite
// (post-ACES, "cine"): lee la color resuelta y suma el aporte aditivo.
// Sin componente temporal. Espejo CPU en src/Utils/Godrays.h (misma formula).
in vec2 vUV;
uniform sampler2D u_scene;     // color resuelta del composite
uniform vec2 u_sunUV;          // sol en UV 0..1 (puerta CPU: en pantalla)
uniform vec3 u_sunColor;       // calido, constante nombrada en App
uniform float u_gain;          // ganancia global
uniform float u_decay;         // decaimiento exponencial (12 taps)
uniform float u_radius;        // radio maximo (fraccion de alto)
uniform float u_falloffK;      // caida con la distancia al sol
uniform float u_aspect;        // w/h
uniform mat4 u_invViewProj;
uniform vec3 u_camPos;
out vec4 fragColor;

bool raySphere(vec3 ro, vec3 rd, float r, out float t0, out float t1) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r * r;
    float disc = b * b - c;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    t0 = -b - s;
    t1 = -b + s;
    return true;
}

void main() {
    vec3 base = texture(u_scene, vUV).rgb;
    vec2 auv = vec2(vUV.x * u_aspect, vUV.y);
    vec2 asun = vec2(u_sunUV.x * u_aspect, u_sunUV.y);
    vec2 d = asun - auv;
    float L = length(d);
    float fall = 1.0 / (1.0 + u_falloffK * L);
    vec3 rays = vec3(0.0);
    if (L > 1e-5) {
        vec2 dir = d / L;
        float stepLen = min(L, u_radius) / 11.0;
        for (int i = 0; i < 12; i++) {
            vec2 auvTap = auv + dir * (stepLen * float(i));
            vec2 tapUV = vec2(auvTap.x / u_aspect, auvTap.y);
            // Unproject del tap (plano lejano) -> rayo visual -> mascara
            // analitica: 0 si cruza la Tierra, 1 si va al cielo.
            vec4 clip = vec4(tapUV * 2.0 - 1.0, 1.0, 1.0);
            vec4 world = u_invViewProj * clip;
            vec3 tdir = normalize(world.xyz / world.w - u_camPos);
            float t0, t1;
            float mask = 1.0;
            if (raySphere(u_camPos, tdir, 1.0, t0, t1) && t1 > 1e-4) mask = 0.0;
            float w = pow(u_decay, float(i));
            rays += u_sunColor * (w * mask);
        }
    }
    fragColor = vec4(base + u_gain * fall * rays, 1.0);
}
