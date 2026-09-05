#version 400 core
layout(location=0) in vec3 aPos;
uniform mat4 u_model, u_view, u_projection;

void main() {
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
}
