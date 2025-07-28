#include "vs_ps_incs.hlsli"

PSOutput PSMain(VSOutput input) {
  PSOutput output;
  output.color = input.color;
  return output;
}
