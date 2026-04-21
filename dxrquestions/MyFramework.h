#pragma once

#include <stdint.h>

#include <vector>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <glm/glm.hpp>
#include <DirectXMath.h>

#include <nvapi.h>

#ifdef USE_IMGUI
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_dx12.h"
#endif

void ResourceBarrierTransition(ID3D12GraphicsCommandList4* cmdlist,
  ID3D12Resource* res,
  D3D12_RESOURCE_STATES from,
  D3D12_RESOURCE_STATES to);

class MyFramework;

size_t AlignUp(uint32_t s, uint32_t a);

void GlmMat4ToDirectXMatrixColMajor(DirectX::XMMATRIX* out, const glm::mat4& m);

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
  virtual void OnKeyUp(uint32_t k) {}

  MyFramework* framework;
};

class MyFramework {
public:
  // Shared by all scenes.
  void InitWindow();
  void InitDeviceAndCommandQ();
  void InitSwapchain();
  void InitSwapchain(uint32_t w, uint32_t h);
  bool InitNVAPI();
  void InitRayTracingValidation();
  bool IsOMMSupported();
#ifdef USE_IMGUI
  void InitImGUIForGLFW(GLFWwindow* w);
#endif
  ID3D12Device5* GetDevice();

  struct MyRtShaderListInfo {
    const wchar_t* raygen_shader{};
    const wchar_t* miss_shader{};
    
    std::vector<D3D12_HIT_GROUP_DESC> hit_groups;  // CHS + AS + IS

    void* dxil_lib_bytecode{};
    uint32_t dxil_lib_length{ 0 };
  };
  void Deinit();
  void SetHwnd(HWND h);

  // Resource creation helpers
  void CreateNvapiEnabledGlobalRootSig(ID3D12RootSignature** out_rootsig, uint32_t num_uav, uint32_t num_srv, uint32_t num_cbv,
    bool add_nvapi_uav, uint32_t nvapi_uav_idx);
  void CreateGlobalRootSig(ID3D12RootSignature** out_rootsig, uint32_t num_uav, uint32_t num_srv, uint32_t num_cbv);
  void CreateRtOutputResource(ID3D12Resource** out_res);
  void CreateRtOutputResource(ID3D12Resource** out_res, uint32_t w, uint32_t h);
  void CreateCBVSRVUAVHeap(ID3D12DescriptorHeap** h, ID3D12DescriptorHeap** h_cpu, uint32_t num_descriptors);
  uint32_t GetCBVSRVUAVDescriptorSize();
  void CreateUAVTexture2D(ID3D12Resource* res, ID3D12DescriptorHeap* h, uint32_t idx);
  void CreateUAVUintBuffer(ID3D12Resource* res,
    uint32_t num_elts, uint32_t stride,
    ID3D12DescriptorHeap* h, uint32_t idx);
  void CreateNullUAV(ID3D12DescriptorHeap* h, uint32_t idx);
  ID3D12CommandAllocator* GetCommandAllocator();
  ID3D12CommandQueue* GetCommandQueue();
  ID3D12GraphicsCommandList4* GetGraphicsCommandList();
  ID3D12Resource* GetCurrentRenderTarget();
  void CreateMyRtPipeline(MyRtPipeline* my_rt_pipeline,
    ID3D12RootSignature* global_rootsig, const struct MyRtShaderListInfo& my_shaders);
  void CreateMyRtPipeline(MyRtPipeline* my_rt_pipeline, ID3D12RootSignature* global_rootsig, const std::vector<struct MyRtShaderListInfo>& my_infos);
  void CreateComputePipeline(ID3D12PipelineState** pso, ID3D12RootSignature* root_sig, const void* shader_bytecode, uint32_t shader_bytecode_length);
  template<class T> void CreateVertexBuffer(std::vector<T>& verts, ID3D12Resource** res, D3D12_VERTEX_BUFFER_VIEW* vbv);
  void CreateBufferForCPUSideData(const void* data, uint32_t len, ID3D12Resource** res);
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
  void CreateLineDrawingPipeline();  // Slate UI
  
  void BuildDummyOMM(ID3D12Resource* vertex_buffer, uint32_t stride,
    ID3D12Resource** blas_result_omm, ID3D12Resource** tlas_result_omm);
  void BuildDummyLSS(
    ID3D12Resource** blas_result,
    ID3D12Resource** tlas_result,
    ID3D12Resource* lss_pos_resource, ID3D12Resource* lss_pos_resource1,
    ID3D12Resource* lss_radii_resource,
    ID3D12Resource* lss_indices_list_resource,
    ID3D12Resource* lss_indices_successive_resource,
    uint32_t vert_count,
    NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE endcap_mode,  // none or chained
    NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT prim_format,  // list or successive
    int case_idx,
    bool is_update
  );
  void BuildDummyTriNVAPI(
    ID3D12Resource** blas_result,
    ID3D12Resource** tlas_result,
    ID3D12Resource* tri_verts_resource,
    uint32_t vert_count
  );
  void BuildDummyProcedural(
    ID3D12Resource** blas_result,
    ID3D12Resource** tlas_result,
    ID3D12Resource* proc_aabb_buffer,
    uint32_t aabb_count
  );
    

  void BuildBLAS(ID3D12Resource** blas_result, ID3D12Resource* vertex_buffer, uint32_t stride, uint32_t vertex_count, D3D12_RAYTRACING_GEOMETRY_FLAGS geom_flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE);     // Just 1 Geom
  void BuildBLAS(ID3D12Resource** blas_result, std::vector<ID3D12Resource*> vertex_buffers, uint32_t stride, std::vector<uint32_t> vertex_counts, std::vector<D3D12_RAYTRACING_GEOMETRY_FLAGS> geom_flags);  // Multiple Geoms
  void BuildBLASProc(ID3D12Resource** blas_result, ID3D12Resource* aabb_buffer, uint32_t stride, uint32_t aabb_count, D3D12_RAYTRACING_GEOMETRY_FLAGS geom_flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE);     // 1 Geom
  void BuildBLASProc(ID3D12Resource** blas_result, std::vector<ID3D12Resource*> aabb_buffers, uint32_t stride, std::vector<uint32_t> aabb_counts, std::vector<D3D12_RAYTRACING_GEOMETRY_FLAGS> geom_flags);  // Multiple Geoms
  void BuildTLAS(ID3D12Resource** tlas_result, ID3D12Resource* blas_result);
  void BuildTLAS(ID3D12Resource** tlas_result, const std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& inst_descs);
  void BuildBLASLSS(ID3D12Resource** blas_result, ID3D12Resource* lss_pos_resource, ID3D12Resource* lss_radii_resource, ID3D12Resource* lss_indices_resource,
    uint32_t vert_count, uint32_t index_count, uint32_t prim_count,
    NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE endcap_mode, NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT prim_format,
    D3D12_RAYTRACING_GEOMETRY_FLAGS geom_flags
  );
  void BuildBLASLSS(ID3D12Resource** blas_result, std::vector<ID3D12Resource*> lss_pos_resources, std::vector<ID3D12Resource*> lss_radii_resources, std::vector<ID3D12Resource*> lss_indices_resources,
    std::vector<uint32_t> vert_counts, std::vector<uint32_t> index_counts, std::vector<uint32_t> prim_counts,
    std::vector<NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE> endcap_modes, std::vector<NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT> prim_formats,
    std::vector<D3D12_RAYTRACING_GEOMETRY_FLAGS> geom_flags
  );
  void BuildBLASSphere(ID3D12Resource** blas_result, std::vector<ID3D12Resource*> sphere_pos_resources, std::vector<ID3D12Resource*> sphere_radii_resources, std::vector<ID3D12Resource*> sphere_indices_resources,
    std::vector<uint32_t> vert_counts, std::vector<uint32_t> index_counts,
    std::vector<D3D12_RAYTRACING_GEOMETRY_FLAGS> geom_flags
  );
  void BuildDummyDXR12OMM(ID3D12Resource* vertex_buffer, uint32_t stride,
    ID3D12Resource** blas_result_omm, ID3D12Resource** tlas_result_omm);

  constexpr static uint32_t WIN_W = 512, WIN_H = 512;
  constexpr static uint32_t FRAME_COUNT = 2;
  D3D12_CPU_DESCRIPTOR_HANDLE GetCurrRenderTargetCPUDescriptor();
  void WaitForPreviousFrame();
  HRESULT Present();

  ID3D12DescriptorHeap* imgui_heap{};
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
  bool is_lss_available{ false };
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
    bool has_sampler,
    bool add_nvapi_uav, uint32_t nvapi_uav_idx
    );
};