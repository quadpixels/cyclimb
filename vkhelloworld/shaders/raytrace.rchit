#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

#include "raycommon.glsl"

layout(location=0) rayPayloadInEXT hitPayload prd;
hitAttributeEXT vec2 attribs;

void main() {
  const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
  prd.hitValue = bary;
}