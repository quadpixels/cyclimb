#version 460

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec3 in_normal;

void main() {
    vec3 c = vec3(1, 1, 1);
    float dp = dot(in_normal, normalize(vec3(1, 2, 3)));
    c = c * (dp * 0.5 + 0.5);
    outColor = vec4(c, 1);
}