RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);

struct Attributes
{
    float2 bary;
};

struct HitInfo
{
    float4 colorAndDistance;
};

struct Viewport
{
    float left;
    float top;
    float right;
    float bottom;
};

struct RayGenCB
{
    Viewport viewport;
    Viewport stencil;
};

ConstantBuffer<RayGenCB> rayGenCB : register(b0);

[shader("raygeneration")]
void RayGen()
{
    const float2 uv = DispatchRaysIndex().xy * 1.0 / DispatchRaysDimensions().xy;
    float2 xy =
    {
        lerp(rayGenCB.viewport.left, rayGenCB.viewport.right, uv.x),
        lerp(rayGenCB.viewport.top, rayGenCB.viewport.bottom, 1.0 - uv.y)
    };
    
    if (xy.x >= rayGenCB.stencil.left && xy.x <= rayGenCB.stencil.right &&
        xy.y >= rayGenCB.stencil.top && xy.y <= rayGenCB.stencil.bottom)
    {
        float4 ret = { xy.x, xy.y, 0, 1 };
        RayDesc ray;
        ray.Origin = float3(xy.x, xy.y, -1.0);
        ray.Direction = float3(0, 0, 1);
        ray.TMin = 0.001;
        ray.TMax = 10000.0;
        HitInfo payload = { float4(0, 0, 0, 1) };
        TraceRay(Scene,
            RAY_FLAG_NONE,
            ~0, 0, 1, 0, ray, payload);
    
        ret = payload.colorAndDistance;
        RenderTarget[DispatchRaysIndex().xy] = ret;
    }
    else
    {
        int xx = round(DispatchRaysIndex().x % 16);
        int yy = round(DispatchRaysIndex().y % 16);
        float4 c = { 1, 1, 0, 1 };
        if ((xx < 8 && yy < 8) || (xx >= 8 && yy >= 8))
        {
            c = float4(0.5, 0.5, 0.5, 1);
        }
        RenderTarget[DispatchRaysIndex().xy] = c;
    }
}

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    const float2 uv = DispatchRaysIndex().xy * 1.0 / DispatchRaysDimensions().xy;
    payload.colorAndDistance.x = lerp(0.9, 0.3, uv.y);
    payload.colorAndDistance.y = lerp(0.9, 0.3, uv.y);
    payload.colorAndDistance.z = 0.9;
}

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
    payload.colorAndDistance.x = attrib.bary.x;
    payload.colorAndDistance.y = attrib.bary.y;
    payload.colorAndDistance.z = 0;
}