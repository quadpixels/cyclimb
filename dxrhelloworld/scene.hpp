#pragma once

#include <d3d12.h>
#include <dxcapi.h>
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
  void CreateRaytracingPipeline();
  void CreateRaytracingOutputBufferAndSRVs();
  void CreateShaderBindingTable();

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

  // RT pipeline
  IDxcBlob* raygen_library, * miss_library, * hit_library;
  D3D12_EXPORT_DESC raygen_export, miss_export, hit_export;
  D3D12_DXIL_LIBRARY_DESC raygen_lib_desc, miss_lib_desc, hit_lib_desc;
  ID3D12RootSignature* raygen_rootsig, * miss_rootsig, * hit_rootsig;
  D3D12_HIT_GROUP_DESC hitgroup_desc;
  ID3D12RootSignature* dummy_global_rootsig, * dummy_local_rootsig;
  ID3D12StateObject* rt_state_object;
  ID3D12StateObjectProperties* rt_state_object_props;

  // RT output buffer
  ID3D12Resource* rt_output_resource;
  ID3D12DescriptorHeap* srv_uav_heap;

  // SBT
  ID3D12Resource* rt_sbt_storage;

  bool is_raster;
};