RaytracingAccelerationStructure g_scene   : register(t0, space0);
RWByteAddressBuffer             g_counter : register(u0);

struct PSInput {
    float4 position : SV_POSITION;
    float4 orig_pos : COLOR1;
    float4 color : COLOR;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR) {
    PSInput result;

    result.position = position;
    result.color = color;
    result.orig_pos = position;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET {
  RayDesc ray;
  ray.Origin = float3(0, 0, -1) + input.orig_pos.xyz;
  ray.TMin = 0;
  ray.TMax = 1000.0;
  ray.Direction = float3(0, 0, 1);
  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> q;
  q.TraceRayInline(g_scene, 0, 0xff, ray);
  q.Proceed();
  
  int counter = 0;
  g_counter.InterlockedAdd(0, 1, counter);
  
  if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
    float t = counter * 1.0 / 512.0 / 512.0;
    return lerp(float4(1.0, 0.2, 0.0, 1.0),
                float4(0.2, 0.0, 1.0, 1.0),
                t);
  } else {
    return input.color;
  }
}
