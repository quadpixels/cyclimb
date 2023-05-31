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
  void InitPipelineAndCommandList();
  ID3D12CommandAllocator* command_allocator;
  ID3D12GraphicsCommandList* command_list;

  ID3DBlob* VS, *PS;
  ID3D12RootSignature* root_signature;
  ID3D12PipelineState* pipeline_state;
};

#endif