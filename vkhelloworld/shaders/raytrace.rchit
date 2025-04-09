#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

#include "raycommon.glsl"

layout(location=0) rayPayloadInEXT hitPayload prd;
hitAttributeEXT vec2 attribs;

layout(binding=2) uniform sampler2D texSampler;
layout(binding=3) uniform sampler2D texSampler1;

struct Vertex {
	vec3 pos;
	vec3 color;
	vec2 uv;
};
layout(std430, binding=4) readonly buffer VertexBuf {
	Vertex vertices[];
};

void main() {
  // read UV
  uint idx = gl_InstanceID * 3;
  vec2 uv0 = vertices[idx  ].uv;
  vec2 uv1 = vertices[idx+1].uv;
  vec2 uv2 = vertices[idx+2].uv;

  const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
  const vec2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

  prd.hitValue = texture(texSampler, uv).xyz;
}