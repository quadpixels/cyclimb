#include <stdint.h>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>

class MyFramework;

class MyScene {
public:
  MyScene(MyFramework* f) : framework(f) {}
  virtual void Render() = 0;
  virtual void Update(float secs) = 0;

  ID3D12RootSignature* global_rootsig;
  ID3D12Resource* rt_output_resource;
  ID3D12DescriptorHeap* cbvsrvuav_heap;
  MyFramework* framework;
};

class MyParisIvyLeafScene : public MyScene {
public:
  MyParisIvyLeafScene(MyFramework* f);
  void Render() override;
  void Update(float secs) override;
};

class MyFramework {
public:
  // Shared by all scenes.
  void InitWindow();
  void InitDeviceAndCommandQ();
  void InitSwapchain();
  ID3D12Device5* GetDevice();

  void CreateRtPipeline(ID3D12RootSignature** out_rootsig);
  void CreateRtOutputResource(ID3D12Resource** out_res);
  void CreateCBVSRVUAVHeap(ID3D12DescriptorHeap** h, ID3D12DescriptorHeap** h_cpu, uint32_t num_descriptors);

  ID3D12CommandAllocator* GetCommandAllocator();
  ID3D12CommandQueue* GetCommandQueue();
  ID3D12GraphicsCommandList4* GetGraphicsCommandList();
  ID3D12Resource* GetCurrentRenderTarget();

  constexpr static uint32_t WIN_W = 512, WIN_H = 512;
  constexpr static uint32_t FRAME_COUNT = 2;
  D3D12_CPU_DESCRIPTOR_HANDLE GetCurrRenderTargetCPUDescriptor();
  void WaitForPreviousFrame();
  HRESULT Present();
protected:
  ID3D12Device5* device12{};
  IDXGIFactory4* dxgi_factory{};
  ID3D12CommandQueue* command_queue{};
  ID3D12CommandAllocator* command_allocator{};
  ID3D12GraphicsCommandList4* command_list{};
  IDXGISwapChain3* swapchain;
  ID3D12DescriptorHeap* rtv_heap{};
  uint32_t rtv_descriptor_size{};
  ID3D12Resource* rendertargets[FRAME_COUNT];
  uint32_t frame_index{};
  HWND hwnd{};
  uint32_t fence_value{};
  HANDLE fence_event;
  ID3D12Fence* fence{};

  uint32_t cbvsrvuav_descriptor_size{};
};