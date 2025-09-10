RWTexture2D<float4> RenderTarget : register(u0);
RaytracingAccelerationStructure Scene : register(t0, space0);

#define NV_SHADER_EXTN_SLOT u100
#define NV_SHADER_EXTN_REGISTER_SPACE space0
#include "../nvapi/nvHLSLExtns.h"

struct MyAttributes
{
    float2 bary;
};

struct MyPayload
{
    float4 color;
    uint prim_idx;
};

[shader("raygeneration")]
void MyRayGenShader()
{
    uint2 dri = DispatchRaysIndex();
    float2 lerpValues = (float2) dri / (float2) DispatchRaysDimensions();
    
    MyPayload payload;
    payload.color = float4(lerpValues, 0, 1);
    payload.prim_idx = 0xFFFFFFFF;
    
    RayDesc ray;
    float2 uv = (lerpValues - 0.5) * 2.0;
    uv.y *= -1.0;
    ray.Origin = float3(uv, 5.0);
    ray.Direction = float3(0, 0, -1.0);
    ray.TMin = 0.01;
    ray.TMax = 10000.0;

    TraceRay(Scene,
    RAY_FLAG_NONE,
    0xFF,
    0,
    0,
    0,
    ray,
    payload);

    RenderTarget[dri] = payload.color;
}

[shader("miss")]
void MyMissShader(inout MyPayload payload)
{
    payload.color = float4(0.5, 0.5, 1.0, 1.0);
}

[shader("closesthit")]
void MyClosestHitShader(inout MyPayload payload, in MyAttributes attr)
{
    payload.color = float4(1.0, 1.0, 0.0, 1.0);
}