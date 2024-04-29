RWByteAddressBuffer buffer0 : register(u0);

[numthreads(1, 1, 1)]
void kernel1(uint3 tid : SV_DispatchThreadID) {
  buffer0.InterlockedAdd(0, 1);
}