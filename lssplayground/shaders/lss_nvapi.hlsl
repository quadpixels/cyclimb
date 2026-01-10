RWTexture2D<float4> RenderTarget : register(u0);
RWStructuredBuffer<float> MyDebugBuffer : register(u1);

#define NV_SHADER_EXTN_SLOT u999
#define NV_SHADER_EXTN_REGISTER_SPACE space0
#include "../../dxrquestions/nvapi/nvHLSLExtns.h"

RaytracingAccelerationStructure Scene : register(t0, space0);

// In case NvRtLssObjectPositionsAndRadii does not work
StructuredBuffer<float3> LssPositions : register(t1);
StructuredBuffer<float> LssRadii : register(t2);


#include "includes.hlsli"

struct MyPayload
{
    float hitDistance;
};

struct MyAttributes
{
    float2 bary;
};

cbuffer PerSceneCb : register(b0)
{
    float4x4 inverse_view;
    float4x4 inverse_proj;
    float3 ro;
    int cam_mode; // 0=perspective, 1=orthogonal
    float3 rd;
    int viz_mode;
}

[shader("raygeneration")]
void RayGen()
{
    uint3 dri = DispatchRaysIndex();
    uint3 drd = DispatchRaysDimensions();
    RenderTarget[dri.xy] = float4(0.1, 0.1, 0.1, 1);
    
    MyPayload payload;
    payload.hitDistance = -1;
    
    RayDesc ray;
    float2 d = (((dri.xy + 0.5f) / drd.xy) * 2.f - 1.f);
    d.y *= -1;
    if (cam_mode == 0)
    {
        float3 target = TransformPosition(inverse_proj, float3(d.x, d.y, 1));
        float3 dir = TransformDirection(inverse_view, normalize(target));
        ray.Direction = dir;
        ray.Origin = TransformPosition(inverse_view, float3(0, 0, 0));
        
        if (dri.x == drd.x / 2 && dri.y == drd.y / 2)  // This center ray must use EXACTLY the same ro and rd
        {
            ray.Direction = rd;
            ray.Origin = ro;
            MyDebugBuffer[0] = float(dri.x);
            MyDebugBuffer[1] = float(dri.y);
            MyDebugBuffer[2] = float(ro.x);
            MyDebugBuffer[3] = float(ro.y);
            MyDebugBuffer[4] = float(ro.z);
            MyDebugBuffer[5] = float(rd.x);
            MyDebugBuffer[6] = float(rd.y);
            MyDebugBuffer[7] = float(rd.z);
        }
    }
    ray.TMin = 0.0;
    ray.TMax = 1e20;
    TraceRay(Scene,
        RAY_FLAG_NONE,
        0xFF,
        0,
        0,
        0,
        ray,
        payload
    );
}

[shader("miss")]
void Miss(inout MyPayload payload)
{
  //
}

[shader("closesthit")]
void ClosestHit(inout MyPayload payload, in MyAttributes attr : SV_IntersectionAttributes)
{
    if (NvRtIsLssHit())
    {
        const float2x4 lssObjectPositionsAndRadii = NvRtLssObjectPositionsAndRadii();
        payload.hitDistance = RayTCurrent();
        uint3 dri = DispatchRaysIndex();
        float u = attr.bary.x;
        const float3 p0 = LssPositions[0];
        const float3 p1 = LssPositions[1];
        //const float3 p0 = lssObjectPositionsAndRadii[0].xyz;
        //const float3 p1 = lssObjectPositionsAndRadii[1].xyz;
        float3 p = lerp(p0, p1, u).xyz;
        float3 hit_p = ObjectRayOrigin() + ObjectRayDirection() * RayTCurrent(); // Assume no transform
        float3 n = normalize(hit_p - p);
        
        switch (viz_mode)
        {
            case 0: {
                RenderTarget[dri.xy] = float4(1, 1, 0, 1);
                break;
            }
            case 1: {
                float dnl = dot(n, normalize(float3(1, 1, 1)));
                dnl = dnl * 0.5 + 0.5;
                RenderTarget[dri.xy] = float4(dnl, dnl, dnl, 1);
                break;
            }
            case 2:{
                RenderTarget[dri.xy] = float4(n * 0.5 + 0.5, 1);
                break;
            }
            case 3:{
                RenderTarget[dri.xy] = float4(MapDistToColorRamp(payload.hitDistance), 1.0);
                break;
            }
        }
    }
}