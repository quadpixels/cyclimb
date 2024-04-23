cbuffer PerSceneCB : register(b0) {
  int WIN_W;
  int WIN_H;
};

struct Pixel {
  int value;
};

RWStructuredBuffer<Pixel> BufferOut : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
  int index = dispatchThreadID.y * WIN_W + dispatchThreadID.x;
  int r = int(dispatchThreadID.x * 1.0 / WIN_W * 255);
  int g = int(dispatchThreadID.y * 1.0 / WIN_H * 255);
  int v = (0xFF << 24) | (g << 8) | r;
  BufferOut[index].value = v;
}