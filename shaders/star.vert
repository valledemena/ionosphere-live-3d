#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aMag;

uniform mat4 u_view, u_projection;
uniform float u_time;

out float vAlpha;

void main() {
    gl_Position = u_projection * u_view * vec4(aPos * 20.0, 1.0);
    vAlpha = 0.3 + 0.7 * (1.0 - aMag / 6.0);
    vAlpha *= 0.8 + 0.2 * sin(u_time * 0.5 + aPos.x * 10.0);
    gl_PointSize = max(1.0, (3.0 - aMag) * 1.5);
}
