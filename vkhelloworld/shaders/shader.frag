#version 450

layout(location=0) in vec3 fragColor;
layout(location=1) in vec2 uv;

layout(location=0) out vec4 outColor;

layout(binding=0) uniform sampler2D texSampler;
layout(binding=1) uniform sampler2D texSampler1;

layout(std430) struct Vertex {
	vec3 pos;
	vec3 color;
	vec2 uv;
};
layout(binding=2) readonly buffer VertexBuf {
	Vertex vertices[];
};

void main() {
    vec4 alpha = texture(texSampler1, uv);
    if (alpha.x < 0.5) discard;
    outColor = texture(texSampler, uv); //vec4(fragColor, 1.0);;
}