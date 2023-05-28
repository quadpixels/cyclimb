struct VSInput {
  float3 aPos   : POSITION;
  float3 aColor : COLOR0;
};

struct VSOutput {
  float4 position : SV_POSITION;
  float4 color : COLOR;
};

struct PSOutput {
  float4 color : SV_Target;
};

VSOutput VSMain(VSInput input) {
  VSOutput output;
  output.position = float4(input.aPos,   1.0);
  output.color    = float4(input.aColor, 1.0);
  return output;
}

PSOutput PSMain(VSOutput input) {
  PSOutput output;
  output.color = input.color;
  return output;
}