#include "vs_ps_incs.hlsli"

Texture2D DiffuseTexture : register(t0);
Texture2D AlphaTexture : register(t1);
SamplerState TextureSampler : register(s0);

PSOutput PSMain(VSOutput input) {
  PSOutput output;
  float4 a = AlphaTexture.Sample(TextureSampler, input.uv);
  if (a.r < 0.5) {
    discard;
  }
  output.color = DiffuseTexture.Sample(TextureSampler, input.uv);
  return output;
}
