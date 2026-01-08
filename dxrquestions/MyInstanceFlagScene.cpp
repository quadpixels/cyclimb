#include "MyFramework.h"
#include "MyScene.h"

#include <source_location>

#ifndef CE
#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}
#endif

#ifndef NDEBUG
#include "x64\Debug\CompiledShaders\instance_flags_shaders.hlsl.h"
#else
#include "x64\Release\CompiledShaders\instance_flags_shaders.hlsl.h"
#endif


MyInstanceFlagScene::MyInstanceFlagScene(MyFramework* f) : MyScene(f) {
  printf("[MyInstanceFlagScene] ctor\n");
  glm::vec3 tri_verts[] = {
    { -0.5, -0.5, 0 },
    { -0.5, 0.5,  0 },
    { 0.5, -0.5, 0 }
  };
  // [Output UAV] [AS SRV] [PerScene CBV]
  framework->CreateBufferForCPUSideData(tri_verts, sizeof(tri_verts), &tri_verts_resource);
  framework->BuildBLAS(&blas_result, tri_verts_resource, sizeof(glm::vec3), 3);
  framework->BuildTLAS(&tlas_result, blas_result);
  framework->CreateGlobalRootSig(&global_rootsig, 1, 1, 1);
  framework->CreateRtOutputResource(&rt_output_resource);
  uint32_t cb_size = 256;
  framework->CreateBufferForCPUSideData(nullptr, cb_size, &perscene_cb);
  framework->CreateCBVSRVUAVHeap(&cbvsrvuav_heap, &cbvsrvuav_heap_cpu, 3);
  framework->CreateCBVBuffer(perscene_cb, cbvsrvuav_heap, 2, cb_size);
  framework->CreateUAVTexture2D(rt_output_resource, cbvsrvuav_heap, 0);
  framework->CreateSRVAccelerationStructure(tlas_result, cbvsrvuav_heap, 1);

  MyFramework::MyRtShaderListInfo info{};
  info.raygen_shader = L"MyRayGenShader";
  info.closest_hit_shader = L"MyClosestHitShader";
  info.miss_shader = L"MyMissShader";
  info.hitgroup_name = L"MyHitGroup";
  info.dxil_lib_bytecode = (void*)g_InstanceFlagsShaders;
  info.dxil_lib_length = sizeof(g_InstanceFlagsShaders);
  f->CreateMyRtPipeline(&my_rt_pipeline, global_rootsig, info);
}

void MyInstanceFlagScene::Update(float secs) {

}

void MyInstanceFlagScene::OnKeyDown(uint32_t k) {

}
void MyInstanceFlagScene::OnKeyUp(uint32_t k) {

}

void MyInstanceFlagScene::Render() {
  D3D12_CPU_DESCRIPTOR_HANDLE handle_rtv = framework->GetCurrRenderTargetCPUDescriptor();
  float bg_color[] = { 1.0f, 1.0f, 0.8f, 1.0f };
  ID3D12CommandAllocator* command_allocator = framework->GetCommandAllocator();
  ID3D12GraphicsCommandList4* command_list = framework->GetGraphicsCommandList();
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));
  ID3D12Resource* rendertarget = framework->GetCurrentRenderTarget();
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);

  {
    command_list->SetComputeRootSignature(global_rootsig);
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav;
    command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&cbvsrvuav_heap);
    handle_uav = CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvsrvuav_heap->GetGPUDescriptorHandleForHeapStart(), 0, framework->GetCBVSRVUAVDescriptorSize());
    command_list->SetComputeRootDescriptorTable(0, handle_uav);
    command_list->SetPipelineState1(my_rt_pipeline.rt_state_object);
    command_list->DispatchRays(&(my_rt_pipeline.dispatch_rays_desc));
  }
  ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
  command_list->CopyResource(rendertarget, rt_output_resource);
  ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
  CE(command_list->Close());

  ID3D12CommandQueue* command_queue = framework->GetCommandQueue();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(framework->Present());
  framework->WaitForPreviousFrame();
}