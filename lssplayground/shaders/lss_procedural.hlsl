RWTexture2D<float4> RenderTarget : register(u0);
RaytracingAccelerationStructure Scene : register(t0, space0);
StructuredBuffer<float3> LssPositions : register(t1);
StructuredBuffer<float> LssRadii : register(t2);

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
    payload.t = RayTCurrent();
    uint pidx = PrimitiveIndex();
    float3 c0 = LssPositions[pidx * 2];
    float3 c1 = LssPositions[pidx * 2 + 1];
    float r0 = LssRadii[pidx * 2];
    float r1 = LssRadii[pidx * 2 + 1];
    
    uint3 dri = DispatchRaysIndex();
    //RenderTarget[dri.xy] = float4(r0 * 0.3f, r1 * 0.3f, 0, 1);
    RenderTarget[dri.xy] = float4(1, 1, 0, 1);
}

[shader("intersection")]
void Intersection()
{
    MyAttributes attr;
    //ReportHit(1, 0, attr);
    //return;
    uint pidx = PrimitiveIndex();
    float3 c0 = LssPositions[pidx * 2];
    float3 c1 = LssPositions[pidx * 2 + 1];
    float r0 = LssRadii[pidx * 2];
    float r1 = LssRadii[pidx * 2 + 1];
    
    float3 ro = ObjectRayOrigin();
    float3 rd = ObjectRayDirection();
    float tmax = RayTCurrent();
    float tmin = RayTMin();
    
    float thit;
    
    bool hit = IntersectLSS(c0, r0, c1, r1, ro, rd, tmin, tmax, thit);
    if (hit)
    {
        ReportHit(thit, 0, attr);
    }
}
