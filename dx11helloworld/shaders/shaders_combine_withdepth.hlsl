// The Light Scattering algorithm is borrowed from here:
// http://fabiensanglard.net/lightScattering/index.php

struct PSInput
{
  float4 position_clipspace : SV_POSITION;
  float2 position_screenspace : COLOR; // to avoid repeating SV_POSITION.
  float2 uv : TEXCOORD;
};

cbuffer PerSceneCB : register(b0)
{
  float WIN_W, WIN_H;
  float light_x, light_y, light_r;
  float4 light_color;
  float global_alpha;
  float light_z;
}

Texture2D g_src1 : register(t0);
Texture2D g_src2 : register(t1);
Texture2D gbuffer : register(t2);
SamplerState g_sampler : register(s0);

PSInput VSMain(float4 position : POSITION, float4 uv : TEXCOORD)
{
  PSInput result;
  result.position_clipspace = position;
  result.position_screenspace = position * float2(WIN_W, WIN_H);
  result.uv = uv.xy;
  return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
  float4 rgba1 = g_src1.Sample(g_sampler, input.uv);
  
  float density = 1.0f;
  float decay = 0.95f;
  float weight = 1.0f;
  float exposure = 0.02f;
  int num_samples = 20;
  
  float2 light_clipspace = float2(light_x / WIN_W, light_y / WIN_H);
  float2 tex_coord = input.uv;
  float2 delta = input.uv - light_clipspace;
  delta = delta * (1.0f / num_samples * density);
  float illumination_decay = 1.0f;

  float dx = input.position_clipspace.x - light_x;
  float dy = input.position_clipspace.y - light_y;
  float rsq = dx*dx + dy*dy;
  int is_in_source = 0;
  if (rsq < light_r * light_r) { is_in_source = 1; }

  float4 rgba2 = float4(0.0f, 0.0f, 0.0f, 0.0f);
  {
    for (int i=0; i<num_samples && illumination_decay > 0; i++) {
      tex_coord -= delta;
      float4 sample = g_src2.Sample(g_sampler, tex_coord);
      float4 sample1 = g_src1.Sample(g_sampler, tex_coord);
      float  z = gbuffer.Sample(g_sampler, tex_coord).z;
      float  a = gbuffer.Sample(g_sampler, tex_coord).a;
      if (light_z > z && a != 0) { continue; }
      sample = sample * illumination_decay * weight;      
      rgba2 += sample;
      illumination_decay *= decay;
    }
  }
  {
    return (rgba1 + (rgba2*exposure)) * global_alpha;
  }
}
