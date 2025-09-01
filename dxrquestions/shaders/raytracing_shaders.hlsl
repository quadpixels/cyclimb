RWTexture2D<float4> RenderTarget : register(u0);
RaytracingAccelerationStructure Scene : register(t0, space0);

Texture2D DiffuseTexture : register(t1);
Texture2D AlphaTexture : register(t2);
SamplerState TextureSampler : register(s0);

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
};

[shader("raygeneration")]
void MyRaygenShader()
{
  float2 lerpValues = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

  MyPayload payload;
  payload.color = float4(lerpValues, 0, 1);

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
  RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("miss")]
void MyMissShader(inout MyPayload payload)
{
  //
}

float2 GetQuadUV(in MyAttributes attr) {
  int pidx = PrimitiveIndex();
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
  float2 uv = GetQuadUV(attr);
  float alpha = AlphaTexture.SampleLevel(TextureSampler, uv, 0).x;
  if (alpha < 0.5) {
    IgnoreHit();
  }
}

[shader("closesthit")]
void MyClosestHitShader(inout MyPayload payload, in MyAttributes attr)
{
  float2 uv = GetQuadUV(attr);
  payload.color = DiffuseTexture.SampleLevel(TextureSampler, uv, 0);
}