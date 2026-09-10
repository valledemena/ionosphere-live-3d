#version 400 core
// M8 (plan 11.3): ribbon camera-facing — la geometria CPU es una polilinea
// (aPos/aT/aViable/aTangent/aSide); el shader la engorda.
// M9c (P1): el uniform entra como semiancho GEOmetrico = halfWidth demo
// (hfc::ribbonHalfWidth) x span (hfc::ribbonSpan) desde render() — el
// ribbon cubre nucleo+aura con el MISMO strip y vertex count; aSide pasa
// como vSide (fraccion del borde geometrico) y el FRAG recorta el perfil.
layout(location=0) in vec3 aPos;
layout(location=1) in float aT;
layout(location=2) in float aViable;
layout(location=3) in vec3 aTangent;
layout(location=4) in float aSide;
uniform mat4 u_view, u_projection;
uniform vec3 u_camPos;
uniform float u_halfWidth;   // M9c: semiancho GEOmetrico = demo 0.0075 (hfc::ribbonHalfWidth) x span (hfc::ribbonSpan)
out float vT;
out float vViable;
out float vSide;             // M9c: -1..+1 respecto al borde geometrico (u_halfWidth)
void main() {
    vT = aT;
    vViable = aViable;
    vSide = aSide;
    vec3 toCam = normalize(u_camPos - aPos);
    vec3 n = normalize(cross(aTangent, toCam));
    vec3 p = aPos + n * (u_halfWidth * aSide);
    gl_Position = u_projection * u_view * vec4(p, 1.0);
}
