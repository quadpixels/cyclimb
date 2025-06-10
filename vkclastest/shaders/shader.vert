#version 450

layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal;
layout(location=0) out vec3 fragColor;

layout(std430) struct Vertex {
	vec3 pos;
	vec3 normal;
};
layout(binding=0) readonly buffer VertexBuf {
	Vertex vertices[];
};

void main() {
    gl_Position = vec4(pos, 1);
    fragColor = vec3(0.5, 0.5, 0.5);
}