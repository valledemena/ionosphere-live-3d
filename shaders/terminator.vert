#version 400 core
layout(location=0) in vec3 aPos;
uniform mat4 u_model, u_view, u_projection;
uniform vec3 u_sunDir;
out float vSide;

void main() {
    vec3 worldPos = vec3(u_model * vec4(aPos, 1.0));
    float dotSun = dot(normalize(worldPos), normalize(u_sunDir));
    vSide = step(0.0, dotSun);
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
}
