#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

#include "raycommon.glsl"

layout(location=0) rayPayloadInEXT hitPayload prd;
hitAttributeEXT vec2 attribs;

layout(binding=2) uniform RtPerSceneData {  // Keep in sync with raytrace.rgen
    mat4 InverseView;
    mat4 InverseProj;
    int IsUsingCluster;
    int ColoringMode;  // 0 = cluster, 1 = geometry
	int HighlightedClusterID;
};

layout(binding=3) readonly buffer VertexBuf {
	float vertexPositions[];
};
layout(binding=4) readonly buffer IndexBuf {
	uint indexes[];
};

#extension GL_EXT_spirv_intrinsics : require
// Note that `VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV::allowClusterAccelerationStructures` must
// be set to `VK_TRUE` to make this valid.
spirv_decorate(extensions = ["SPV_NV_cluster_acceleration_structure"], capabilities = [5437], 11, 5436) in int gl_ClusterIDNV_;

layout(binding=5) readonly buffer PerClusterIndexOffsets {
	uint indexOffsets[];
};
layout(binding=6) readonly buffer PerClusterVertexOffsets {
	uint vertexOffsets[];
};

vec3 GetVertex(uint index) {
    return vec3(
	    vertexPositions[index * 3],
		vertexPositions[index * 3 + 1],
		vertexPositions[index * 3 + 2]
	);
}

const vec3 PALETTE[] = {
    vec3(1.0, 1.0, 1.0),
    vec3(1.0, 0.5, 0.5),
    vec3(0.5, 1.0, 0.5),
    vec3(0.5, 0.5, 1.0),
    vec3(1.0, 1.0, 0.5),
    vec3(1.0, 0.5, 1.0),
    vec3(0.5, 1.0, 1.0)
};

void main() {
  uint pidx = gl_PrimitiveID;
  uint gidx = gl_GeometryIndexEXT;
  uint index = 0;
  uint vidx0, vidx1, vidx2;
  uint cidx = gl_ClusterIDNV_;
  if (IsUsingCluster != 0) {
    pidx += vertexOffsets[cidx] + indexOffsets[cidx];
	vidx0 = indexes[indexOffsets[cidx] * 3 + gl_PrimitiveID * 3] + vertexOffsets[cidx];
	vidx1 = indexes[indexOffsets[cidx] * 3 + gl_PrimitiveID * 3 + 1] + vertexOffsets[cidx];
	vidx2 = indexes[indexOffsets[cidx] * 3 + gl_PrimitiveID * 3 + 2] + vertexOffsets[cidx];
  } else {
	index = pidx * 3;
	vidx0 = indexes[index];
	vidx1 = indexes[index + 1];
	vidx2 = indexes[index + 2];
  }
  vec3 v0 = GetVertex(vidx0);
  vec3 v1 = GetVertex(vidx1);
  vec3 v2 = GetVertex(vidx2);
  
  vec3 n = normalize(cross(v1-v0, v2-v0));

  float dp = dot(n, normalize(vec3(0,1,0)));
  float x = dp * 0.5 + 0.5;

  if (HighlightedClusterID != -1 && IsUsingCluster != 0) {
    if (ColoringMode == 0) {
      if (HighlightedClusterID == cidx) {  // Highlight individual cluster
        prd.hitValue = vec3(0.0, 0.0, 1.0);
      } else {
        prd.hitValue = PALETTE[0];
      }
    } else {
      if (HighlightedClusterID == gidx) {  // Highlight individual cluster
        prd.hitValue = vec3(0.0, 0.0, 1.0);
      } else {
        prd.hitValue = PALETTE[0];
      }
    }
    prd.hitValue *= x;
    return;
  }

  uint col_idx = 0;
  if (IsUsingCluster != 0) {
    if (ColoringMode == 0) {
      col_idx = cidx % 7;
    } else {
      col_idx = gidx % 7;
    }
  }
  prd.hitValue = PALETTE[col_idx];
  prd.hitValue *= x;
}