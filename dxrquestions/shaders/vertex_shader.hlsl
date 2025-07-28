#include "vs_ps_incs.hlsli"

VSOutput VSMain(VSInput input) {
  VSOutput output;
  output.position = float4(input.aPos, 1.0);
  output.color    = input.aColor;
  return output;
}
