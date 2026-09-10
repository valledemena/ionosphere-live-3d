#version 400 core
// M8 (plan 11.3): ribbon camera-facing — la geometria CPU es una polilinea
// (aPos/aT/aViable/aTangent/aSide); el shader la engorda.
layout(location=0) in vec3 aPos;
layout(location=1) in float aT;
layout(location=2) in float aViable;
layout(location=3) in vec3 aTangent;
layout(location=4) in float aSide;
uniform mat4 u_view, u_projection;
uniform vec3 u_camPos;
uniform float u_halfWidth;   // M9b: ancho demo-verbatim 0.0075 (uniform desde render, constante)
out float vT;
out float vViable;
void main() {
    vT = aT;
    vViable = aViable;
    vec3 toCam = normalize(u_camPos - aPos);
    vec3 n = normalize(cross(aTangent, toCam));
    vec3 p = aPos + n * (u_halfWidth * aSide);
    gl_Position = u_projection * u_view * vec4(p, 1.0);
}
