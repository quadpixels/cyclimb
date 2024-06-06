#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <DirectXMath.h>

#include "camera.hpp"
#include "textrender.hpp"

#include <string>

class Scene {
public:
  virtual void Render() = 0;
  virtual void Update(float secs) = 0;
};

class MoreTrianglesScene : public Scene {
public:
  struct Vertex {
    float x, y, z;
  };
  struct Viewport
  {
    float left;
    float top;
    float right;
    float bottom;
  };
  struct RayGenConstantBuffer
  {
    Viewport viewport;
    Viewport stencil;
  };
  struct Mat3x4 {
    float m[3][4];
  };

  MoreTrianglesScene();
  void Render() override;
  void Update(float secs) override;

  ID3D12CommandAllocator* command_allocator;
  ID3D12GraphicsCommandList4* command_list;
  ID3D12RootSignature* global_rootsig;
  ID3D12RootSignature* local_rootsig;

  // RT PSO
  ID3D12StateObject* rt_state_object;
  ID3D12StateObjectProperties* rt_state_object_props;

  // RT SRV descriptor heap
  ID3D12DescriptorHeap* srv_uav_heap;
  int srv_uav_descriptor_size;

  // Triangle geometry
  ID3D12Resource* vertex_buffer;
  ID3D12Resource* index_buffer;

  // AABB proc geometry
  ID3D12Resource* proc_aabb_buffer;

  // Anyhit triangle geometry
  ID3D12Resource* anyhit_vertex_buffer;
  
  // Building AS
  ID3D12Resource* as_scratch;
  ID3D12Resource* tlas;
  ID3D12Resource* blas0;  // Triangle
  ID3D12Resource* blas1;  // Procedural
  ID3D12Resource* blas2;  // Anyhit

  ID3D12Resource* transform_matrices0;

  // SBT
  ID3D12Resource* raygen_sbt_storage;
  ID3D12Resource* miss_sbt_storage;
  ID3D12Resource* hit_sbt_storage;

  // Output
  ID3D12Resource* rt_output_resource;
};

class TriangleScene : public Scene {
public:
  struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
  };
  struct TriSceneCB {
    int WIN_W, WIN_H, use_counter;
  };
  TriangleScene();

  void InitDX12Stuff();
  void CreateAS();
  void Render() override;
  void Update(float secs) override;

  ID3D12RootSignature* root_sig;
  ID3D12CommandAllocator* command_allocator;
  ID3D12GraphicsCommandList4* command_list;
  ID3D12PipelineState* pipeline_state;

  ID3D12Resource* vb_triangle;
  D3D12_VERTEX_BUFFER_VIEW vbv_triangle;

  ID3D12Resource* blas_scratch, * blas_result;
  ID3D12Resource* tlas_scratch, * tlas_result, * tlas_instance;
  ID3D12Resource* px_counter;
  ID3D12DescriptorHeap* srv_uav_heap;
  int srv_uav_descriptor_size;

  ID3D12Resource* cb_scene;
  bool use_counter = true;
  void ToggleUseCounter() {
    use_counter = !use_counter;
  }
};

class ObjScene : public Scene {
public:
  char axes[6]; // Camera movement

  struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
  };
  struct RayGenCB {
    DirectX::XMMATRIX inverse_view;
    DirectX::XMMATRIX inverse_proj;
  };
  struct TransformCB {  // MVP matrix for raster view
    DirectX::XMMATRIX M, V, P;
  };
  ObjScene();
  void InitDX12Stuff();
  void LoadModel();
  void CreateAS();
  void CreateRaytracingPipeline();
  void CreateRaytracingOutputBufferAndSRVs();
  void CreateShaderBindingTable();

  void ToggleIsRaster() {
    is_raster = !is_raster;
  }
  void Render() override;
  void Update(float secs) override;

  ID3D12RootSignature* root_sig;
  ID3D12PipelineState* pipeline_state;
  ID3D12CommandAllocator* command_allocator;
  ID3D12GraphicsCommandList4* command_list;
  ID3D12CommandAllocator* command_allocator1; // for showing text
  ID3D12GraphicsCommandList* command_list1;   // for showing text
  ID3D12Resource* vb_obj;
  D3D12_VERTEX_BUFFER_VIEW vbv_obj;
  ID3D12Resource* vb_triangle;
  D3D12_VERTEX_BUFFER_VIEW vbv_triangle;
  unsigned num_verts;

  ID3D12Resource* blas_scratch, * blas_result;
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

  // Constant Buffer for RayGen (inverse view and proj matrices)
  ID3D12Resource* raygen_cb;

  // For raster view (MVP matrices)
  ID3D12Resource* raster_cb;
  ID3D12DescriptorHeap* dsv_heap;
  int dsv_descriptor_size;
  ID3D12Resource* depth_map;

  // Time measurement
  ID3D12QueryHeap* query_heap;
  ID3D12Resource* timestamp_resource;

  bool is_raster;
  bool inited = false;
  Camera* camera;
  TextPass* text_pass;
  std::wstring status_string;
};