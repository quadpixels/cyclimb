struct VSInput {
  float2 position : POSITION;
  float2 texcoord : TEXCOORD0;
};

struct VSOutput {
  float4 position : SV_POSITION;
  float2 texcoord : TEXCOORD;
};

struct PSOutput {
  float4 color : SV_Target;
};

cbuffer CB : register(b0) {
  float4 xyoffset_alpha;
};

Texture2D tex;
SamplerState theSampler;

VSOutput VSMain(VSInput input) {
  VSOutput output;
  output.position = float4(input.position/* + xyoffset_alpha.xy*/, 0.0f, 1.0f);
  output.texcoord = input.texcoord;
  return output;
}

PSOutput PSMain(VSOutput input) {
  PSOutput output;
  output.color = tex.Sample(theSampler, input.texcoord.xy);
  output.color.a = xyoffset_alpha.a;
  return output;
}