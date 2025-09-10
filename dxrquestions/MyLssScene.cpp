#include "MyFramework.h"

#include <assert.h>
#include <stdio.h>

#include <source_location>
#include <stdexcept>
#include <vector>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <DirectXMath.h>

#include <nvapi.h>

#ifndef NDEBUG
#include "x64\Debug\CompiledShaders\raytracing_shaders_lss.hlsl.h"
#else
#include "x64\Release\CompiledShaders\raytracing_shaders_lss.hlsl.h"
#endif

// https://stackoverflow.com/questions/65315241/how-can-i-fix-requires-l-value
template <class T>
static constexpr auto& keep(T&& x) noexcept {
  return x;
}

#ifndef CE
#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}
#endif

MyLssScene::MyLssScene(MyFramework* f) : MyScene(f) {
  uint32_t nvapi_uav = 100;
  uint32_t nvapi_space = 0;
  NvAPI_Status status = NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(f->GetDevice(), nvapi_uav, nvapi_space);
  if (status != NVAPI_OK) {
    printf("Oh! error setting NVShaderExtnSlotSpaceLocalThread\n");
    exit(0);
  }
  f->CreateRtGlobalRootSig(&global_rootsig, 1, 1, 1, true, nvapi_uav);
  f->CreateRtOutputResource(&rt_output_resource);
  f->CreateCBVSRVUAVHeap(&cbvsrvuav_heap, nullptr, 3);
  f->CreateUAVTexture2D(rt_output_resource, cbvsrvuav_heap, 0);
  MyFramework::MyRtShaderListInfo info{};
  info.raygen_shader = L"MyRayGenShader";
  info.closest_hit_shader = L"MyClosestHitShader";
  info.miss_shader = L"MyMissShader";
  info.hitgroup_name = L"MyHitGroup";
  info.dxil_lib_bytecode = (void*)g_RaytracingShadersLss;
  info.dxil_lib_length = sizeof(g_RaytracingShadersLss);
  f->CreateMyRtPipeline(&my_rt_pipeline, global_rootsig, info);
  f->CreateBufferForCPUSideData(lss_poses.data(),
    lss_poses.size() * sizeof(glm::vec3), &lss_pos_resource);
  f->CreateBufferForCPUSideData(lss_radii.data(),
    lss_radii.size() * sizeof(float), &lss_radii_resource);
  f->CreateBufferForCPUSideData(lss_indices.data(),
    lss_indices.size() * sizeof(uint32_t), &lss_indices_resource);
  f->BuildDummyLSS(
    &blas_result,
    &tlas_result,
    lss_pos_resource,
    lss_radii_resource,
    lss_indices_resource,
    2,  // vert count
    1,  // prim count
    2,  // index count, 1=TDR, 2=nsight says invalid ?
    NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED,
    NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_LIST);
  f->CreateSRVAccelerationStructure(tlas_result, cbvsrvuav_heap, 2);
  f->CreateNullUAV(cbvsrvuav_heap, 1);
}

void MyLssScene::Render() {
  D3D12_CPU_DESCRIPTOR_HANDLE handle_rtv = framework->GetCurrRenderTargetCPUDescriptor();
  float bg_color[] = { 1.0f, 1.0f, 0.8f, 1.0f };
  ID3D12CommandAllocator* command_allocator = framework->GetCommandAllocator();
  ID3D12GraphicsCommandList4* command_list = framework->GetGraphicsCommandList();
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));
  ID3D12Resource* rendertarget = framework->GetCurrentRenderTarget();
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);

  {  // if LSS supported
    ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->SetComputeRootSignature(global_rootsig);
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav;
    command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&cbvsrvuav_heap);
    handle_uav = CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvsrvuav_heap->GetGPUDescriptorHandleForHeapStart(), 0, framework->GetCBVSRVUAVDescriptorSize());
    command_list->SetComputeRootDescriptorTable(0, handle_uav);
    command_list->SetPipelineState1(my_rt_pipeline.rt_state_object);
    command_list->DispatchRays(&(my_rt_pipeline.dispatch_rays_desc));
    ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    command_list->CopyResource(rendertarget, rt_output_resource);
    ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
  }

  CE(command_list->Close());

  ID3D12CommandQueue* command_queue = framework->GetCommandQueue();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(framework->Present());
  framework->WaitForPreviousFrame();
}

void MyLssScene::Update(float secs) {

}

void MyLssScene::OnKeyDown(uint32_t k) {

}