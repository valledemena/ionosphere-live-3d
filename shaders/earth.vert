#version 400 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexCoord;

uniform mat4 u_model, u_view, u_projection;

out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vFragPos;

void main() {
    vNormal = mat3(u_model) * aNormal;
    vTexCoord = aTexCoord;
    vFragPos = vec3(u_model * vec4(aPos, 1.0));
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
}
