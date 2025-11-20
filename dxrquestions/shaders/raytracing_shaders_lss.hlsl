RWTexture2D<float4> RenderTarget : register(u0);
RWStructuredBuffer<uint> MyDebugBuffer : register(u1);
RaytracingAccelerationStructure Scene : register(t0, space0);

cbuffer PerSceneCb : register(b0)
{
    float4x4 inverse_view;
    float4x4 inverse_proj;
    int viz_mode;
    int cam_mode;  // 0 = perspective, 1 = orthogonal
};

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

// My boilerplates
float3 TransformPosition(float4x4 m, float3 x)
{
    float4 x4 = float4(x, 0.0f);
    x4 = mul(m, x4);
    x4.x += m[0][3]; // [Col] [Row]
    x4.y += m[1][3];
    x4.z += m[2][3];
    return x4.xyz;
}

float3 TransformDirection(float4x4 m, float3 x)
{
    return (mul(m, float4(x, 0.0f))).xyz;
}

[shader("raygeneration")]
void MyRayGenShader()
{
    uint2 dri = DispatchRaysIndex();
    float2 lerpValues = (float2) dri / (float2) DispatchRaysDimensions();
    
    MyPayload payload;
    payload.color = float4(lerpValues, 0, 1);
    payload.prim_idx = 0xFFFFFFFF;
    
    RayDesc ray;
    //float2 uv = (lerpValues - 0.5) * 2.0;
    //uv.y *= -1.0;
    //ray.Origin = float3(uv, 0.2);
    //ray.Direction = float3(0, 0, -1.0);
    
    float2 d = (((DispatchRaysIndex().xy + 0.5f) / DispatchRaysDimensions().xy) * 2.f - 1.f);
    if (cam_mode == 0)
    {
        float3 target = TransformPosition(inverse_proj, float3(d.x, d.y, 1));
        float3 dir = TransformDirection(inverse_view, normalize(target));
        ray.Direction = dir;
        ray.Origin = TransformPosition(inverse_view, float3(0, 0, 0));
    }
    else
    {
        float z_ndc_near = 0.0;
        float3 cam_near = mul(inverse_proj, float4(d, z_ndc_near, 1.0)).xyz;
        
        //float3 target = TransformPosition(inverse_proj, float3(d.x, d.y, 0));
        //ray.Origin = TransformPosition(inverse_view, float3(d.x, d.y, 1));
        //float3 dir = TransformDirection(inverse_view, normalize(float3(0, 0, -1)));
        //ray.Direction = dir;
        ray.Origin = mul(inverse_view, float4(cam_near, 1.0)).xyz;
        ray.Direction = normalize(mul(inverse_view, float4(0, 0, -1, 0)).xyz);
    }
    
    
    if (dri.x == 128 && dri.y == 128)
    {
        MyDebugBuffer[0] = asuint(ray.Origin.x);
        MyDebugBuffer[1] = asuint(ray.Origin.y);
        MyDebugBuffer[2] = asuint(ray.Origin.z);
        MyDebugBuffer[3] = asuint(ray.Direction.x);
        MyDebugBuffer[4] = asuint(ray.Direction.y);
        MyDebugBuffer[5] = asuint(ray.Direction.z);
    }
    
    ray.TMin = 0.0;
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

float MyMap(float x, float start1, float stop1, float start2, float stop2) {
    return start2 + (x-start1) / (stop1-start1) * (stop2-start2);
}

[shader("closesthit")]
void MyClosestHitShader(inout MyPayload payload, in MyAttributes attr)
{
    const float3 palette[] =
    {
        { 1, 0.5, 0.5 },
        { 0.5, 1, 0.5 },
        { 0.7, 0.8, 1 }
    };
    if (NvRtIsLssHit())
    {
        if (viz_mode == 1)
        {

            float3 c = palette[PrimitiveIndex() % 3];
            if (GeometryIndex() > 0)
            {
                c += (1.0 - c) * 0.2;
            }
            payload.color = float4(c, 1.0);
        }
        else
        {
            float t = RayTCurrent();
            if (t > 1.0)
            {
                t = 1.0;
            }
            payload.color = float4(t, t, t, 1.0);
        }
    }
    else
    {
        payload.color = float4(1.0, 1.0, 0.0, 1.0);
    }
}