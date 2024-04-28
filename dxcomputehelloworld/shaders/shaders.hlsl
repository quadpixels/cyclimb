cbuffer OverallCB : register(b0) {
  int num_triangles;
};

RWByteAddressBuffer buffer0 : register(u0);

[numthreads(1, 1, 1)]
void kernel1(uint3 tid : SV_DispatchThreadID) {
  int x;
  buffer0.InterlockedAdd(0, 1, x);
}