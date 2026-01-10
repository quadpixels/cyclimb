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
    float3 ro;
    int cam_mode; // 0=perspective, 1=
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
    float3 p0 = LssPositions[pidx * 2];
    float3 p1 = LssPositions[pidx * 2 + 1];
    float r0 = LssRadii[pidx * 2];
    float r1 = LssRadii[pidx * 2 + 1];
    
    uint3 dri = DispatchRaysIndex();
    float3 hit_p = ObjectRayOrigin() + ObjectRayDirection() * RayTCurrent(); // Assume no transform
    float ax = length(hit_p - p0);
    float bx = length(hit_p - p1);
    const float EPS = 1e-4;
    float u;
    if (abs(r0 - ax) < EPS)
    {
        u = 0;
    }
    else if (abs(r1 - bx) < EPS)
    {
        u = 1;
    }
    else
    {   
        float jx = sqrt(ax * ax - r0 * r0);
        float xk = sqrt(bx * bx - r1 * r1);
        u = (jx / (jx + xk));
    }
    
    float3 p = lerp(p0, p1, u).xyz;
    
    float3 n = normalize(hit_p - p);
    
    switch (viz_mode)
    {
        case 0:{
                RenderTarget[dri.xy] = float4(1, 1, 0, 1);
                break;
            }
        case 1:{
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
                RenderTarget[dri.xy] = float4(MapDistToColorRamp(RayTCurrent()), 1);
                break;
            }
    }
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
