#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_NV_displacement_micromap : enable

#include "raycommon.glsli"

layout(location=0) rayPayloadInEXT hitPayload payload;
layout(binding=3) readonly buffer VertexBuf {
	float verts[];
};
layout(binding=4) readonly buffer IndexBuf {
	uint idxes[];
};

vec3 GetVertex(uint idx) {
	uint index = idxes[idx];
	return vec3(verts[index * 3],
	            verts[index * 3 + 1],
				verts[index * 3 + 2]);
}

vec3 GetHitNormalDMM()
{
    vec3 v0 = gl_HitMicroTriangleVertexPositionsNV[0];
    vec3 v1 = gl_HitMicroTriangleVertexPositionsNV[1];
    vec3 v2 = gl_HitMicroTriangleVertexPositionsNV[2];

    return normalize(cross(v1 - v0, v2 - v0));
}

void main() {
  vec3 n = GetHitNormalDMM();
  float d = dot(n, normalize(vec3(1, 2, 3)));
  vec3 c = vec3(1, 1, 1);
  c = c * (d * 0.5 + 0.5);
  payload.hitValue = c;
}