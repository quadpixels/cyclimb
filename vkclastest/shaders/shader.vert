#version 460

layout(location=0) in vec3 pos;
layout(location=0) out vec3 fragColor;
layout(location=1) out flat uint clusterId;

layout(binding=1) uniform PerSceneData {
	mat4 M;
	mat4 V;
	mat4 P;
};

void main() {
    gl_Position = P * (V * (M * vec4(pos, 1)));
	clusterId = gl_BaseInstance;
}