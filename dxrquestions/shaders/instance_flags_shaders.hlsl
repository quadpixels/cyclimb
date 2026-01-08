RWTexture2D<float4> RenderTarget : register(u0);
RaytracingAccelerationStructure Scene : register(t0, space0);

struct MyAttributes
{
    float2 bary;
};

struct MyPayload
{
    float4 color;
    uint prim_idx;
};


struct MyConstantBufferStruct
{
    uint cull_flag;
};
ConstantBuffer<MyConstantBufferStruct> MyConstantBuffer : register(b0);

[shader("raygeneration")]
void MyRayGenShader()
{
    float2 lerpValues = (float2) DispatchRaysIndex() / (float2) DispatchRaysDimensions();
    uint2 dri = DispatchRaysIndex().xy;
    uint idx = dri.y * DispatchRaysDimensions().x + dri.x;

    MyPayload payload;
    payload.color = float4(0.1, lerpValues.y * 0.1, 0.1, 1);
    payload.prim_idx = 0xFFFFFFFF;

    RayDesc ray;
    float2 uv = (lerpValues - 0.5) * 2.0;
    uv.y *= -1.0;
    ray.Origin = float3(uv, 1.0);
    ray.Direction = float3(0, 0, -1.0);
    ray.TMin = 0.01;
    ray.TMax = 10000.0;

    TraceRay(Scene,
    RAY_FLAG_NONE,
    MyConstantBuffer.cull_flag,
    0,
    0,
    0,
    ray,
    payload);

    RenderTarget[dri] = payload.color;
}

[shader("closesthit")]
void MyClosestHitShader(inout MyPayload payload, in MyAttributes attr)
{
    payload.color = float4(1, 1, 0, 1);
}

[shader("miss")]
void MyMissShader(inout MyPayload payload)
{
  //
}