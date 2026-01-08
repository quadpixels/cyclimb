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
  h_perscene_cb.cull_flag = 0xFF;
  printf("[MyInstanceFlagScene] ctor\n");
  glm::vec3 tri_verts[] = {
    { -0.5, -0.5, 0 },
    { -0.5, 0.5,  0 },
    { 0.5, -0.5, 0 },

    { 0.5, -0.5, 0 },
    { -0.5, 0.5,  0 },
    { 0.5, 0.5, 0 }
  };
  // [Output UAV] [AS SRV] [PerScene CBV]
  framework->CreateBufferForCPUSideData(tri_verts, sizeof(tri_verts), &tri_verts_resource);
  framework->BuildBLAS(&blas_result, tri_verts_resource, sizeof(glm::vec3), 6);

  // Grid
  float xmin = -0.95, xmax = 0.95, ymin = 0.9, ymax = -1.0;
  float xstep = (xmax - xmin) / 16, ystep = (ymax - ymin) / 16;
  const float L = 0.8f;
  float xscale = xstep / 1.0f * L, yscale = ystep / 1.0f * L;
  std::vector<D3D12_RAYTRACING_INSTANCE_DESC> inst_descs;
  for (uint32_t y = 0; y < 16; y++) {
    for (uint32_t x = 0; x < 16; x++) {
      float xcenter = (x + 0.5) / 16 * (xmax - xmin) + xmin;
      float ycenter = (y + 0.5) / 16 * (ymax - ymin) + ymin;
      uint32_t idx = x + y * 16;
      D3D12_RAYTRACING_INSTANCE_DESC desc{};
      desc.AccelerationStructure = blas_result->GetGPUVirtualAddress();
      desc.InstanceContributionToHitGroupIndex = 0;
      desc.InstanceID = idx;
      desc.InstanceMask = idx;
      desc.Transform[0][0] = xscale;
      desc.Transform[1][1] = yscale;
      desc.Transform[2][2] = 1;
      desc.Transform[0][3] = xcenter;
      desc.Transform[1][3] = ycenter;
      desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
      inst_descs.push_back(desc);
    }
  }

  framework->BuildTLAS(&tlas_result, blas_result, inst_descs);
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
  void* mapped;
  perscene_cb->Map(0, nullptr, &mapped);
  memcpy(mapped, &h_perscene_cb, sizeof(h_perscene_cb));
  perscene_cb->Unmap(0, nullptr);
}

void MyInstanceFlagScene::OnKeyDown(uint32_t k) {
  int delta = 0;
  switch (k) {
  case VK_UP:
    delta = 8; break;
  case VK_DOWN:
    delta = -8; break;
  case VK_LEFT:
    delta = -1; break;
  case VK_RIGHT:
    delta = 1; break;
  }
  if (delta != 0) {
    uint32_t f = (h_perscene_cb.cull_flag + delta) & 0xFF;
    h_perscene_cb.cull_flag = f;
    printf("Ray cull flag set to %u (0x%x)\n", f, f);
  }
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