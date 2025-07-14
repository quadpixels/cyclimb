#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

#include "raycommon.glsl"

layout(location=0) rayPayloadInEXT hitPayload prd;
hitAttributeEXT vec2 attribs;


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
  prd.hitValue = vec3(0.5, 1.0, 0.4);
}