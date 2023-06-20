struct VSInput {
  float3 aPos   : POSITION;
  float4 aColor : COLOR0;
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
  output.color    = input.aColor;
  return output;
}

PSOutput PSMain(VSOutput input) {
  PSOutput output;
  output.color = input.color;
  return output;
}
