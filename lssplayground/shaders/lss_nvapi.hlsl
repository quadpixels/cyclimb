RWTexture2D<float4> RenderTarget : register(u0);
RaytracingAccelerationStructure Scene : register(t0, space0);

#define NV_SHADER_EXTN_SLOT u999
#define NV_SHADER_EXTN_REGISTER_SPACE space0
#include "../../dxrquestions/nvapi/nvHLSLExtns.h"

#include "includes.hlsli"

struct MyPayload
{
    float t;
};

struct MyAttributes
{
    float2 bary;
};

cbuffer PerSceneCb : register(b0)
{
    float4x4 inverse_view;
    float4x4 inverse_proj;
    int cam_mode;  // 0=perspective, 1=orthogonal
}

[shader("raygeneration")]
void RayGen()
{
    uint3 dri = DispatchRaysIndex();
    uint3 drd = DispatchRaysDimensions();
    RenderTarget[dri.xy] = float4(0.1, 0.1, 0.1, 1);
    
    MyPayload payload;
    payload.t = -1;
    
    RayDesc ray;
    float2 d = (((DispatchRaysIndex().xy + 0.5f) / DispatchRaysDimensions().xy) * 2.f - 1.f);
    d.y *= -1;
    if (cam_mode == 0)
    {
        float3 target = TransformPosition(inverse_proj, float3(d.x, d.y, 1));
        float3 dir = TransformDirection(inverse_view, normalize(target));
        ray.Direction = dir;
        ray.Origin = TransformPosition(inverse_view, float3(0, 0, 0));
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
void ClosestHit(inout MyPayload payload, in MyAttributes attr)
{
    if (NvRtIsLssHit())
    {
        payload.t = RayTCurrent();
        float t = attr.bary.x;
        
        
        uint3 dri = DispatchRaysIndex();
        RenderTarget[dri.xy] = float4(1, 1, 0, 1);
    }
}