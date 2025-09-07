#include <stdint.h>

#include <vector>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <glm/glm.hpp>

class MyFramework;

// All the stuff needed to do dispatch rays
struct MyRtPipeline {
  D3D12_DISPATCH_RAYS_DESC dispatch_rays_desc{};
  ID3D12RootSignature* global_rootsig{};
  ID3D12StateObject* rt_state_object{};
  ID3D12StateObjectProperties* rt_state_object_props{};
  ID3D12Resource* rt_sbt{};
};

class MyScene {
public:
  MyScene(MyFramework* f) : framework(f) {}
  virtual void Render() = 0;
  virtual void Update(float secs) = 0;
  virtual void OnKeyDown(uint32_t k) = 0;

  MyFramework* framework;
};

class MyParisIvyLeafScene : public MyScene {
public:
  MyParisIvyLeafScene(MyFramework* f);
  void Render() override;
  void Update(float secs) override;
  void OnKeyDown(uint32_t k) override;

  struct MyConstantBufferStruct {
    uint32_t is_dump_debuginfo;
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
};

class MyFramework {
public:
  // Shared by all scenes.
  void InitWindow();
  void InitDeviceAndCommandQ();
  void InitSwapchain();
  void InitNVAPI();
  void InitRayTracingValidation();
  bool IsOMMSupported();
  ID3D12Device5* GetDevice();

  // Resource creation helpers
  void CreateRtGlobalRootSig(ID3D12RootSignature** out_rootsig);
  void CreateRtOutputResource(ID3D12Resource** out_res);
  void CreateCBVSRVUAVHeap(ID3D12DescriptorHeap** h, ID3D12DescriptorHeap** h_cpu, uint32_t num_descriptors);
  uint32_t GetCBVSRVUAVDescriptorSize();
  void CreateUAVTexture2D(ID3D12Resource* res, ID3D12DescriptorHeap* h, uint32_t idx);
  void CreateUAVUintBuffer(ID3D12Resource* res,
    uint32_t num_elts, uint32_t stride,
    ID3D12DescriptorHeap* h, uint32_t idx);
  ID3D12CommandAllocator* GetCommandAllocator();
  ID3D12CommandQueue* GetCommandQueue();
  ID3D12GraphicsCommandList4* GetGraphicsCommandList();
  ID3D12Resource* GetCurrentRenderTarget();
  void CreateMyRtPipeline(MyRtPipeline* my_rt_pipeline,
    ID3D12RootSignature* global_rootsig);
  template<class T> void CreateVertexBuffer(std::vector<T>& verts, ID3D12Resource** res, D3D12_VERTEX_BUFFER_VIEW* vbv);
  void CreateBufferForCPUSideData(void* data, uint32_t len, ID3D12Resource** res);
  void CreateBufferForUAVAccess(uint32_t len, ID3D12Resource** res);
  void CreateBufferForCPUAccess(uint32_t len, ID3D12Resource** res);
  void CreateBufferForAS(uint32_t len, ID3D12Resource** res);
  void CreateHelloTriangleRootSig(ID3D12RootSignature** out_rootsig);
  void CreateMyPipelineState(ID3D12PipelineState** pso, ID3D12RootSignature* root_sig);
  void LoadTextureFromImage(ID3D12Resource** res, const char* filename);
  void CreateSRVTexture2D(ID3D12Resource* res, ID3D12DescriptorHeap* h, uint32_t idx);
  void CreateSRVAccelerationStructure(ID3D12Resource* res, ID3D12DescriptorHeap* h, uint32_t idx);
  void CreateSRVBuffer(ID3D12Resource* res, ID3D12DescriptorHeap* h, uint32_t idx, uint32_t num_elts, uint32_t stride);
  void CreateCBVBuffer(ID3D12Resource* res, ID3D12DescriptorHeap* h, uint32_t idx, uint32_t len);
  
  void BuildDummyOMM(ID3D12Resource* vertex_buffer, uint32_t stride,
    ID3D12Resource** blas_result_omm, ID3D12Resource** tlas_result_omm);

  void BuildBLAS(ID3D12Resource** blas_result,
    ID3D12Resource* vertex_buffer, uint32_t stride, uint32_t vertex_count);  // Just 1 geom
  void BuildTLAS(ID3D12Resource** tlas_result,
    ID3D12Resource* blas_result);

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
  bool is_nvapi_available{ false };
  bool is_omm_available{ false };
  bool use_rt_validation{ false };

  uint32_t cbvsrvuav_descriptor_size{};

  void do_CreateBuffer(
    ID3D12Resource** res,
    uint32_t len,
    D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES state,
    D3D12_HEAP_TYPE heap_type
  );
  void do_CreateRootSig(
    ID3D12RootSignature** root_sig,
    uint32_t num_uav, uint32_t num_srv, uint32_t num_cbv,
    D3D12_ROOT_SIGNATURE_FLAGS flag,
    bool has_sampler
    );
};