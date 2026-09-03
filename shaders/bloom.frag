#version 400 core
in vec2 vUV;
uniform sampler2D u_texture;
uniform vec2 u_direction;
uniform vec2 u_resolution;
out vec4 fragColor;

void main() {
    float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 result = texture(u_texture, vUV).rgb * weight[0];
    vec2 texel = u_direction / u_resolution;
    for (int i = 1; i < 5; i++) {
        result += texture(u_texture, vUV + texel * float(i)).rgb * weight[i];
        result += texture(u_texture, vUV - texel * float(i)).rgb * weight[i];
    }
    fragColor = vec4(result, 1.0);
}
