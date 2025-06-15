#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

#include "raycommon.glsl"

layout(location=0) rayPayloadInEXT hitPayload prd;
hitAttributeEXT vec2 attribs;

layout(binding=3) readonly buffer VertexBuf {
	float vertexPositions[];
};
layout(binding=4) readonly buffer IndexBuf {
	uint indexes[];
};

vec3 GetVertex(uint index) {
    return vec3(
	    vertexPositions[index * 3],
		vertexPositions[index * 3 + 1],
		vertexPositions[index * 3 + 2]
	);
}

void main() {
  uint pidx = gl_PrimitiveID;
  vec3 v0 = GetVertex(indexes[pidx * 3]);
  vec3 v1 = GetVertex(indexes[pidx * 3 + 1]);
  vec3 v2 = GetVertex(indexes[pidx * 3 + 2]);
  
  vec3 n = normalize(cross(v1-v0, v2-v0));

  float dp = dot(n, normalize(vec3(0,1,0)));
  float x = dp * 0.5 + 0.5;

  prd.hitValue = vec3(x, x, x);
}