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

cbuffer CBPerScene : register(b0) {
  float4 screensize;
  float4x4 transform;
  float4x4 projection;
  float4 textcolor;
};

Texture2D charTex;
SamplerState charTexSampler;

VSOutput VSMain(VSInput input) {
  VSOutput output;
  output.texcoord = input.texcoord;
  
  const float fovy = 30.0f * 3.14159f / 180.0f, Z = 10.0f;
  const float half_yext = Z * tan(fovy);
  float half_xext = half_yext / screensize.y * screensize.x;
  
  float2 xy = input.position.xy / screensize.xy;
  xy.y = 1.0 - xy.y;
  xy = xy * 2.0f - float2(1.0f, 1.0f);
  xy = xy * float2(half_xext, half_yext);
  output.position = mul(projection, mul(transform, float4(xy, -Z, 1.0)));
  return output;
}

PSOutput PSMain(VSOutput input) {
  float c = charTex.Sample(charTexSampler, input.texcoord.xy).r;
  PSOutput output;
  output.color = float4(textcolor.xyz, c);
  return output;
}