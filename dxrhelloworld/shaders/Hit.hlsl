#include "Common.hlsl"

struct STriVertex {
    float3 vertex;
    float4 color;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) {
    float3 barycentrics =
        float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    uint vertId = 3 * PrimitiveIndex();
    float3 v0 = BTriVertex[vertId + 0].vertex;
    float3 v1 = BTriVertex[vertId + 1].vertex;
    float3 v2 = BTriVertex[vertId + 2].vertex;
    float3 v0v1 = v1-v0, v0v2 = v2 - v0;
    float3 n = normalize(cross(v0v1, v0v2));
    float3 c = float3(1, 1, 1);
    float x = clamp(dot(normalize(c), n), 0.2, 1);
    
    float3 hitColor = BTriVertex[vertId + 0].color * barycentrics.x +
                      BTriVertex[vertId + 1].color * barycentrics.y +
                      BTriVertex[vertId + 2].color * barycentrics.z;
    hitColor *= x;

    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}
