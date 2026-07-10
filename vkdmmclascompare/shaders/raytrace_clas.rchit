#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_ray_tracing_position_fetch : require
#extension GL_NV_cluster_acceleration_structure : enable

#include "raycommon.glsli"

layout(location=0) rayPayloadInEXT hitPayload payload;
layout(binding=3) readonly buffer VertexBuf {
	float verts[];
};
layout(binding=4) readonly buffer IndexBuf {
	uint idxes[];
};

void main() {
  vec3 v0 = gl_HitTriangleVertexPositionsEXT[0];
  vec3 v1 = gl_HitTriangleVertexPositionsEXT[1];
  vec3 v2 = gl_HitTriangleVertexPositionsEXT[2];
  vec3 n = normalize(cross(v1 - v0, v2 - v0));
  float d = dot(n, normalize(vec3(1, 2, 3)));
  vec3 c = vec3(1, 1, 1);
  c = c * (d * 0.5 + 0.5);
  payload.hitValue = c;
}