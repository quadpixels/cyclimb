#pragma once

#include <d3d12.h>
#include <DirectXMath.h>

class Scene {
public:
  virtual void Render() = 0;
  virtual void Update(float secs) = 0;
};

class TriangleScene : public Scene {
public:
  struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
  };
  TriangleScene();
  void InitDX12Stuff();
  void CreateAS();

  void Render() override;
  void Update(float secs) override;

  ID3D12RootSignature* root_sig;
  ID3D12PipelineState* pipeline_state;
  ID3D12CommandAllocator* command_allocator;
  ID3D12GraphicsCommandList4* command_list;
  ID3D12Resource* vb_triangle;
  D3D12_VERTEX_BUFFER_VIEW vbv_triangle;

  ID3D12Resource* blas_scratch, * blas_result, * blas_instance;
  ID3D12Resource* tlas_scratch, * tlas_result, * tlas_instance;
};