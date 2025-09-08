RWTexture2D<float4> RenderTarget : register(u0);
RWStructuredBuffer<uint> MyDebugBuffer : register(u1);
RaytracingAccelerationStructure Scene : register(t0, space0);

Texture2D DiffuseTexture : register(t1);
Texture2D AlphaTexture : register(t2);
SamplerState TextureSampler : register(s0);

struct MyConstantBufferStruct {
  uint is_dump_debuginfo;
  uint is_omm;
  uint omm_primidx0;  // 0x9d58180 or 0x9d58190
  uint omm_primidx1;
};
ConstantBuffer<MyConstantBufferStruct> MyConstantBuffer : register(b0);

struct VertexData
{
  float3 pos; int pad1;
  float3 color; int pad2;
  float2 uv;
  int mat_idx;
  int pad3;
};
StructuredBuffer<VertexData> Vertices : register(t3);

struct MyAttributes {
  float2 bary;
};

struct MyPayload {
  float4 color;
  uint prim_idx;
};

[shader("raygeneration")]
void MyRaygenShader()
{
  float2 lerpValues = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

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
  if (MyConstantBuffer.is_dump_debuginfo != 0) {
    uint idx = dri.y * DispatchRaysDimensions().x + dri.x;
    MyDebugBuffer[idx] = payload.prim_idx;
  }
}

[shader("miss")]
void MyMissShader(inout MyPayload payload)
{
  //
}

float2 GetQuadUV(in MyAttributes attr) {
  int pidx = PrimitiveIndex();
  if (MyConstantBuffer.is_omm) {
    if (pidx == MyConstantBuffer.omm_primidx0) {
      pidx = 0;
    }
    if (pidx == MyConstantBuffer.omm_primidx1) {
      pidx = 1;
    }
  }
  int vidx = pidx * 3;
  VertexData v0 = Vertices[vidx];
  VertexData v1 = Vertices[vidx + 1];
  VertexData v2 = Vertices[vidx + 2];
  float2 uv0 = v0.uv, uv1 = v1.uv, uv2 = v2.uv;
  float bu = attr.bary.x, bv = attr.bary.y;
  float2 uv = uv0 + (uv1 - uv0) * bu + (uv2 - uv0) * bv;
  return uv;
}

[shader("anyhit")]
void MyAnyHitShader(inout MyPayload payload, in MyAttributes attr)
{
  if (MyConstantBuffer.is_dump_debuginfo != 0) {
    payload.prim_idx = PrimitiveIndex();
  }
  float2 uv = GetQuadUV(attr);
  payload.color = float4(uv.x, uv.y, 1, 1);
  float alpha = AlphaTexture.SampleLevel(TextureSampler, uv, 0).x;
  if (alpha < 0.5) {
    IgnoreHit();
  }
  else {
    AcceptHitAndEndSearch();
  }
}

[shader("closesthit")]
void MyClosestHitShader(inout MyPayload payload, in MyAttributes attr)
{
  float2 uv = GetQuadUV(attr);
  payload.color = DiffuseTexture.SampleLevel(TextureSampler, uv, 0);
}