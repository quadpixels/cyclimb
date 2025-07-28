#include <stdint.h>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>

class MyFramework {
public:
  void InitWindow();
  void InitDeviceAndCommandQ();
private:
  constexpr static uint32_t WIN_W = 512, WIN_H = 512;
  constexpr static uint32_t FRAME_COUNT = 2;
  ID3D12Device5* device12{};
  IDXGIFactory4* dxgi_factory{};
  ID3D12CommandQueue* command_queue{};
  IDXGISwapChain3* swapchain;
  ID3D12DescriptorHeap* rtv_heap{};
  uint32_t rtv_descriptor_size{};
  ID3D12Resource* rendertargets[FRAME_COUNT];
  HWND hwnd{};
  uint32_t fence_value{};
  HANDLE fence_event;
  ID3D12Fence* fence{};
};