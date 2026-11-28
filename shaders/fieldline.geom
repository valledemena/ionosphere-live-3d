#version 400 core
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
uniform mat4 u_view, u_projection;
uniform float u_halfWidthNdc;
in vec4 vColor[];
out vec4 gColor;
out float vSide;
void main() {
    vec4 p0 = u_projection * u_view * gl_in[0].gl_Position;
    vec4 p1 = u_projection * u_view * gl_in[1].gl_Position;
    vec2 a = p0.xy / p0.w, b = p1.xy / p1.w;
    vec2 d = b - a;
    float len = length(d);
    float aspect = u_projection[1][1] / u_projection[0][0];
    vec2 n = (len > 1e-7) ? vec2(-d.y / aspect, d.x) * (u_halfWidthNdc / len)
                          : vec2(u_halfWidthNdc / aspect, 0.0);
    gColor = vColor[0];
    vSide = -1.0; gl_Position = p0 + vec4(n * p0.w * -1.0, 0.0, 0.0); EmitVertex();
    vSide =  1.0; gl_Position = p0 + vec4(n * p0.w,        0.0, 0.0); EmitVertex();
    vSide = -1.0; gl_Position = p1 + vec4(n * p1.w * -1.0, 0.0, 0.0); EmitVertex();
    vSide =  1.0; gl_Position = p1 + vec4(n * p1.w,        0.0, 0.0); EmitVertex();
    EndPrimitive();
}
