#pragma once

#include "MyScene.h"

class MyParisIvyLeafScene : public MyScene {
public:
  MyParisIvyLeafScene(MyFramework* f);
  void Render() override;
  void Update(float secs) override;
  void OnKeyDown(uint32_t k) override;

  struct MyConstantBufferStruct {
    uint32_t is_dump_debuginfo;
    uint32_t is_omm;
    uint32_t omm_primidx0;  // 0x9d58180 or 0x9d58190
    uint32_t omm_primidx1;
  };
  MyConstantBufferStruct my_cb_cpu{};
  ID3D12Resource* my_cb_resource;
  ID3D12RootSignature* global_rootsig;
  ID3D12Resource* rt_output_resource;
  ID3D12Resource* my_debug_resource;
  ID3D12Resource* my_debug_resource_cpu;
  ID3D12DescriptorHeap* cbvsrvuav_heap, * texture_srv_heap;
  ID3D12DescriptorHeap* cbvsrvuav_heap_omm;
  ID3D12Resource* vertex_buffer;
  D3D12_VERTEX_BUFFER_VIEW vbv;
  MyRtPipeline my_rt_pipeline{};
  ID3D12RootSignature* rast_rootsig{};
  ID3D12PipelineState* rast_pipeline{};
  ID3D12Resource* blas_result{};
  ID3D12Resource* tlas_result{};
  ID3D12Resource* blas_result_omm{};  // OMM-enabled BLAS, built using NVAPI
  ID3D12Resource* tlas_result_omm{};  // OMM-enabled TLAS, built using NVAPI
  bool is_rt{ false };
  bool is_omm{ false };
  struct Vertex {
    alignas(16) glm::vec3 pos;
    alignas(16) glm::vec3 color;
    alignas(16) glm::vec2 uv; int mat_idx;
    int pad{};
  };
  ID3D12Resource* diffuse_texture, * alpha_texture;
  ID3D12Resource* omm_array_data_resource, * omm_desc_array_resource;

  std::vector<Vertex> vertices = {
    { { -0.5, -0.5, 0}, { 1, 0, 0 }, { 0, 1 }, -1 },
    { {  0.5,  0.5, 0}, { 0, 1, 0 }, { 1, 0 }, -1 },
    { { -0.5,  0.5, 0}, { 0, 0, 1 }, { 0, 0 }, -1 },

    { { -0.5, -0.5, 0}, { 1, 0, 0 }, { 0, 1 }, -1 },
    { {  0.5, -0.5, 0}, { 0, 0, 1 }, { 1, 1 }, -1 },
    { {  0.5,  0.5, 0}, { 0, 1, 0 }, { 1, 0 }, -1 },
  };

  enum PrimIdxMappingState {
    NotStarted,
    GetNonOMMResult,
    GetOMMResult,
    Done
  };
  PrimIdxMappingState prim_idx_mapping_state{ PrimIdxMappingState::NotStarted };
  std::vector<int> prim_idxes_non_omm;
  std::vector<int> prim_idxes_omm;
};

// Same as above, but using DXR 1.2's OMM rather than NVAPI's OMM
class MyParisIvyLeafSceneDXR12OMM : public MyScene {
public:
  MyParisIvyLeafSceneDXR12OMM(MyFramework* f);

  struct Vertex {
    alignas(16) glm::vec3 pos;
    alignas(16) glm::vec3 color;
    alignas(16) glm::vec2 uv; int mat_idx;
    int pad{};
  };

  struct MyConstantBufferStruct {
    uint32_t is_dump_debuginfo;
    uint32_t is_omm;
    uint32_t omm_primidx0;  // 0x9d58180 or 0x9d58190
    uint32_t omm_primidx1;
  };

  std::vector<Vertex> vertices = {
    { { -0.5, -0.5, 0}, { 1, 0, 0 }, { 0, 1 }, -1 },
    { {  0.5,  0.5, 0}, { 0, 1, 0 }, { 1, 0 }, -1 },
    { { -0.5,  0.5, 0}, { 0, 0, 1 }, { 0, 0 }, -1 },

    { { -0.5, -0.5, 0}, { 1, 0, 0 }, { 0, 1 }, -1 },
    { {  0.5, -0.5, 0}, { 0, 0, 1 }, { 1, 1 }, -1 },
    { {  0.5,  0.5, 0}, { 0, 1, 0 }, { 1, 0 }, -1 },
  };

  void Render() override;
  void Update(float secs) override;
  void OnKeyDown(uint32_t k) override;

  ID3D12RootSignature* global_rootsig;
  MyRtPipeline my_rt_pipeline{};

  ID3D12Resource* vertex_buffer;
  D3D12_VERTEX_BUFFER_VIEW vbv;
  ID3D12Resource* blas_result_omm{};  // OMM-enabled BLAS, built using DXR 1.2
  ID3D12Resource* tlas_result_omm{};  // OMM-enabled TLAS, built using DXR 1.2
  ID3D12Resource* rt_output_resource;
  ID3D12DescriptorHeap* cbvsrvuav_heap_omm;
  ID3D12Resource* my_debug_resource;
  ID3D12Resource* diffuse_texture, * alpha_texture;
  ID3D12Resource* my_cb_resource;
};