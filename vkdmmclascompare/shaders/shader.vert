#version 450

layout(location=0) in vec3 pos;

layout(binding=0) uniform PerSceneData {
    mat4 M;
    mat4 V;
    mat4 P;
};

void main() {
    gl_Position = P * (V * (M * vec4(pos, 1)));
}