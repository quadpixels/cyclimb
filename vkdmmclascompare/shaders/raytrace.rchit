#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

#include "raycommon.glsli"

layout(location=0) rayPayloadInEXT hitPayload payload;

void main() {
  payload.hitValue = vec3(1, 1, 0);
}