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
