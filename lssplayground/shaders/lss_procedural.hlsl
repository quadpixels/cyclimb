RWTexture2D<float4> RenderTarget : register(u0);
RaytracingAccelerationStructure Scene : register(t0, space0);
StructuredBuffer<float3> LssPositions : register(t1);
StructuredBuffer<float> LssRadii : register(t2);

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
void RayGen()
{
    uint3 dri = DispatchRaysIndex();
    uint3 drd = DispatchRaysDimensions();
    RenderTarget[dri.xy] = float4(dri.xy * 1.0f / drd.xy, 0, 1);
    
    MyPayload payload;
    payload.t = -1;
    
    RayDesc ray;
    float2 d = (((DispatchRaysIndex().xy + 0.5f) / DispatchRaysDimensions().xy) * 2.f - 1.f);
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
    RenderTarget[dri.xy] = float4(r0 * 0.3f, r1 * 0.3f, 0, 1);
}

[shader("intersection")]
void Intersection()
{
    MyAttributes attr;
    uint pidx = PrimitiveIndex();
    float3 c0 = LssPositions[pidx * 2];
    float3 c1 = LssPositions[pidx * 2 + 1];
    float r0 = LssRadii[pidx * 2];
    float r1 = LssRadii[pidx * 2 + 1];
    ReportHit(1.0f, 0, attr);
}
