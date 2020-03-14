struct VSInput {
  float3 position : SV_Position;
};

struct VSOutput {
  float4 position : SV_POSITION;
};

cbuffer CBPerObject : register(b0) {
  float4x4 lightSpaceMatrix;
  float4x4 M;
};

VSOutput VSMain(VSInput input) {
  VSOutput output;
  output.position = mul(lightSpaceMatrix, mul(M, float4(input.position, 1.0f)));
  return output;
}