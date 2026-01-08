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
    RenderTarget[dri] = float4(lerpValues, 0.0, 1.0);

    /*
    MyPayload payload;
    payload.color = float4(lerpValues, 0, 1);
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
    0xFF,
    0,
    0,
    0,
    ray,
    payload);

  // Render interpolated DispatchRaysIndex outside the stencil window
    uint2 dri = DispatchRaysIndex().xy;
    RenderTarget[dri] = payload.color;
    if (MyConstantBuffer.is_dump_debuginfo != 0)
    {
        uint idx = dri.y * DispatchRaysDimensions().x + dri.x;
        MyDebugBuffer[idx] = payload.prim_idx;
    }
*/
}

[shader("closesthit")]
void MyClosestHitShader(inout MyPayload payload, in MyAttributes attr)
{
    // NOP for now
}

[shader("miss")]
void MyMissShader(inout MyPayload payload)
{
  //
}