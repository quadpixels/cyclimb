RWStructuredBuffer<uint> MyDebugBuffer : register(u0);
RaytracingAccelerationStructure Scene : register(t0, space0);

#define NV_SHADER_EXTN_SLOT u100
#define NV_SHADER_EXTN_REGISTER_SPACE space0
#include "../../dxrquestions/nvapi/nvHLSLExtns.h"

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
    float3 ray_origin;
    float tmin;
    float3 ray_dir;
    float tmax;
}

[shader("raygeneration")]
void MyRayGenShader()
{
    RayDesc ray;
    ray.Origin = ray_origin;
    ray.TMin = tmin;
    ray.Direction = ray_dir;
    ray.TMax = tmax;
    
    MyPayload payload;
    payload.t = -3.0;

    TraceRay(
        Scene,
        RAY_FLAG_NONE,
        0xFF,
        0,
        0,
        0,
        ray,
        payload
    );
    
    MyDebugBuffer[0] = asuint(payload.t);
}

[shader("miss")]
void MyMissShader(inout MyPayload payload)
{
    payload.t = -2.0;
}

[shader("closesthit")]
void MyClosestHitShader(inout MyPayload payload, in MyAttributes attr)
{
    if (NvRtIsLssHit())
    {
        payload.t = RayTCurrent();
    }
    else
    {
        payload.t = RayTCurrent();
    }
}