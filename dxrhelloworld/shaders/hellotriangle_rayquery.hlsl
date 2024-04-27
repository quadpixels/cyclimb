RaytracingAccelerationStructure g_scene : register(t0);

struct PSInput {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR) {
    PSInput result;

    result.position = position;
    result.color = color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET {
  RayDesc ray;
  ray.Origin = input.position + float4(0, 0, -1, 0);
  ray.TMin = 0.1;
  ray.TMax = 1000.0;
  ray.Direction = float3(0, 0, 1);
  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> q;
  q.TraceRayInline(g_scene, 0, 0xff, ray);
  q.Proceed();

  if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
    return float4(1.0, 1.0, 0.0, 1.0);
  } else {
    return input.color;
  }
}
