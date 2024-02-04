struct PSInput
{
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD;
};

cbuffer PerSceneCB : register(b0)
{
  float WIN_W, WIN_H;
  float light_x, light_y, light_r;
  float4 light_color;
}

PSInput VSMain(float4 position : POSITION, float4 uv : TEXCOORD)
{
  PSInput result;
  result.position = position;
  result.uv = uv.xy * float2(WIN_W, WIN_H);
  return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
  float dx = input.position.x - light_x;
  float dy = input.position.y - light_y;
  float rsq = dx*dx + dy*dy;
  if (rsq < light_r * light_r) {
	return light_color;
  } else {
	discard;
	return float4(0,0,0,0);
  }
}
