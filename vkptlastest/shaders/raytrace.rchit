#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

layout(binding=0) uniform accelerationStructureEXT TLAS;
layout(binding=1, rgba32f) uniform image2D image;

#include "raycommon.glsli"

layout(location=0) rayPayloadInEXT hitPayload payload;

void main() {
  payload.hitValue = vec3(0.8, 0.3, 0.3);
}