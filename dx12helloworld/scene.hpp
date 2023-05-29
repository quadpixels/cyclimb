#ifndef _SCENE_HPP
#define _SCENE_HPP

#include <d3d12.h>
#include <D3Dcompiler.h>
#include "d3dx12.h"
#include <wrl/client.h>
#include <dxgi1_4.h>

class Scene {
public:
  virtual void Render() = 0;
  virtual void Update(float secs) = 0;
};

class DX12ClearScreenScene : public Scene {
public:
  DX12ClearScreenScene();
  void Render() override;
  void Update(float secs) override;
  static constexpr int FRAME_COUNT = 2;
private:
  void InitDeviceAndCommandQ();
  void InitSwapChain();
  void InitPipeline();
  void WaitForPreviousFrame();

  ID3D12Device* device;
  IDXGIFactory4* factory;
  ID3D12CommandQueue* command_queue;
  ID3D12CommandAllocator* command_allocator;
  ID3D12GraphicsCommandList* command_list;
  ID3D12Fence* fence;
  int fence_value = 0;
  HANDLE fence_event;

  IDXGISwapChain3* swapchain;
  ID3D12DescriptorHeap* rtv_heap;
  ID3D12Resource* rendertargets[FRAME_COUNT];
  unsigned rtv_descriptor_size;

  ID3DBlob* VS, *PS;
  ID3D12RootSignature* root_signature;
  ID3D12PipelineState* pipeline_state;

  int frame_index;
};

#endif