#include "vs_ps_incs.hlsli"

VSOutput VSMain(VSInput input) {
  VSOutput output;
  output.position = float4(input.aPos, 1.0);
  output.color    = float4(input.aColor, 1.0);
  output.uv = input.uv;
  return output;
}
