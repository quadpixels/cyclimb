#version 450

layout(location=0) in vec3 pos;
layout(location=0) out vec3 fragColor;

layout(binding=0) readonly buffer NormalsBuf {
	vec3 normals[];
};

layout(binding=1) uniform PerSceneData {
	mat4 M;
	mat4 V;
	mat4 P;
};

void main() {
    gl_Position = P * (V * (M * vec4(pos, 1)));
    fragColor = vec3(0.5, 0.5, 0.5);
}