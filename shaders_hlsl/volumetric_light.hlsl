struct VSInput {
  float2 position: POSITION;
  float2 uv      : TEXCOORD;
};

struct VSOutput {
  float4 position : SV_POSITION;
  float2 uv       : TEXCOORD;
};

struct PSOutput {
  float4 color : SV_Target;
};

cbuffer CBPerScene : register(b0) {
  int spotlightCount;
  int forceAlwaysOn;
  float aspect, fovy;
  float4   cam_pos;
  float4x4 spotlightPV[16];
  float4   spotlightColors[16];
}

VSOutput VSMain(VSInput input) {
  VSOutput output;
  output.position.z = 0; output.position.w = 1.0f;
  output.position.xy = input.position.xy;
  output.uv = input.uv;
  return output;
}


Texture2D gbuffer;
SamplerState gbuffer_sampler;

float IsInSpotlight(float4 worldpos, int idx) {
  float4 frag = mul(spotlightPV[idx], worldpos);
  float3 xyz = frag.xyz / frag.w;
  //xyz.xyz = xyz.xyz * 0.5 + 0.5;
  //xyz.y = 1.0f - xyz.y; // MEGA HACK
  
  float THRESH = 1.0f;
  
  if (xyz.x >= -THRESH && xyz.x <= THRESH && xyz.y >= -THRESH && xyz.y <= THRESH) return 1.0f;
  else return 0.0f;
}

PSOutput PSMain(VSOutput input) {
  PSOutput output;
  output.color = float4(0, 0, 0, 0);
  
  float3 cp = cam_pos.xyz;
  
  if (true) {
    float4 worldpos = gbuffer.Sample(gbuffer_sampler, input.uv);
    worldpos.xyz /= worldpos.w;
    float3 dir = normalize(cp - worldpos.xyz);
    if (forceAlwaysOn == 1) {
      float dz = 1;
      float dy0 = tan(fovy / 2);
      float dx0 = dy0 / aspect;
      float u1 = 2 * (input.uv.x - 0.5f);
      float v1 = 2 * (0.5f - input.uv.y);
      dir = normalize(float3(-u1 * dx0, -v1 * dy0, -dz));
      worldpos.xyz = cp - dir * 200.0f;
    }
    float tmp_total = 0;
    float w_total = 0;
    for (int idx=0; idx<spotlightCount; idx++) {
      float tmp = 0.0f;
      float4 p   = worldpos;
      //dir = dir * -1;
      for (int i=0; i<48; i++) {
        if (1 == IsInSpotlight(p, idx))
          tmp += 0.4f;
        p.xyz = p.xyz + dir * 6.0f;
      }
      output.color.rgb += spotlightColors[idx] * tmp;
      w_total += tmp;
    }
    output.color.w = w_total / 12.0f;
  }
  
  //output.color = float4(gbuffer.Sample(gbuffer_sampler, input.uv).xyz, 0.2f);
  return output;
}