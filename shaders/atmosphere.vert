#version 400 core
layout(location=0) in vec3 aPos;

uniform mat4 u_model, u_view, u_projection;

// M6 (P1, plan 9.2): posicion en MUNDO post-escala. El frag necesita el
// rd exacto: vPos = aPos (pre-escala) dejaba el viewDir torcido ~1% contra
// u_cameraPos. Mismo patron que el vert del VolumeRenderer.
out vec3 vWorldPos;

void main() {
    vWorldPos = vec3(u_model * vec4(aPos, 1.0));
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
}
