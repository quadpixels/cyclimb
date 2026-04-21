#include "MyParisIvyLeafScene.h"
#include "MyFramework.h"

#include <source_location>

#ifndef NDEBUG
#include "x64/Debug/CompiledShaders/raytracing_shaders.hlsl.h"
#else
#include "x64/Release/CompiledShaders/raytracing_shaders.hlsl.h"
#endif

#ifndef CE
#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}
#endif

MyParisIvyLeafSceneDXR12OMM::MyParisIvyLeafSceneDXR12OMM(MyFramework* f) : MyScene(f) {
  const uint32_t num_pixels = f->WIN_H * f->WIN_W;
  const uint32_t cb_size = 256;  // Multiple of 256
  framework = f;
  const uint32_t num_verts = vertices.size();

  f->LoadTextureFromImage(&diffuse_texture, "textures/Paris_ivy_leaf_a_diff.png");
  f->LoadTextureFromImage(&alpha_texture, "textures/Paris_ivy_leaf_a_mask.png");

  f->CreateVertexBuffer(vertices, &vertex_buffer, &vbv);
  f->CreateNvapiEnabledGlobalRootSig(&global_rootsig, 2, 4, 1, false, 0);
  f->CreateRtOutputResource(&rt_output_resource);
  
  MyFramework::MyRtShaderListInfo info{};
  info.raygen_shader = L"MyRaygenShader";
  info.miss_shader = L"MyMissShader";
  D3D12_HIT_GROUP_DESC hg{};
  hg.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hg.ClosestHitShaderImport = L"MyClosestHitShader";
  hg.AnyHitShaderImport = L"MyAnyHitShader";
  hg.HitGroupExport = L"MyHitGroup";
  info.hit_groups = { hg };
  info.dxil_lib_bytecode = (void*)g_RaytracingShaders;
  info.dxil_lib_length = sizeof(g_RaytracingShaders);
  f->CreateMyRtPipeline(&my_rt_pipeline, global_rootsig, info);

  f->CreateBufferForCPUSideData(nullptr, cb_size, &my_cb_resource);

  f->BuildDummyDXR12OMM(vertex_buffer, sizeof(Vertex), &blas_result_omm, &tlas_result_omm);
  f->CreateCBVSRVUAVHeap(&cbvsrvuav_heap_omm, nullptr, 7);
  f->CreateUAVTexture2D(rt_output_resource, cbvsrvuav_heap_omm, 0);
  f->CreateUAVUintBuffer(my_debug_resource, num_pixels, sizeof(uint32_t), cbvsrvuav_heap_omm, 1);
  f->CreateSRVAccelerationStructure(tlas_result_omm, cbvsrvuav_heap_omm, 2);
  f->CreateSRVTexture2D(diffuse_texture, cbvsrvuav_heap_omm, 3);
  f->CreateSRVTexture2D(alpha_texture, cbvsrvuav_heap_omm, 4);
  f->CreateSRVBuffer(vertex_buffer, cbvsrvuav_heap_omm, 5, num_verts, sizeof(Vertex));
  f->CreateCBVBuffer(my_cb_resource, cbvsrvuav_heap_omm, 6, cb_size);
}

void MyParisIvyLeafSceneDXR12OMM::Render() {
  // Clear Scr
  D3D12_CPU_DESCRIPTOR_HANDLE handle_rtv = framework->GetCurrRenderTargetCPUDescriptor();
  float bg_color[] = { 0.8f, 0.9f, 0.9f, 1.0f };
  ID3D12CommandAllocator* command_allocator = framework->GetCommandAllocator();
  ID3D12GraphicsCommandList4* command_list = framework->GetGraphicsCommandList();
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));
  ID3D12Resource* rendertarget = framework->GetCurrentRenderTarget();
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);

  // RT
  ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  command_list->SetComputeRootSignature(global_rootsig);

  command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&cbvsrvuav_heap_omm);
  CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav;
  handle_uav = CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvsrvuav_heap_omm->GetGPUDescriptorHandleForHeapStart(), 0, framework->GetCBVSRVUAVDescriptorSize());
  command_list->SetComputeRootDescriptorTable(0, handle_uav);
  command_list->SetPipelineState1(my_rt_pipeline.rt_state_object);
  command_list->DispatchRays(&(my_rt_pipeline.dispatch_rays_desc));
  ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
  command_list->CopyResource(rendertarget, rt_output_resource);
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
  CE(command_list->Close());
  ID3D12CommandQueue* command_queue = framework->GetCommandQueue();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(framework->Present());
  framework->WaitForPreviousFrame();
}

void MyParisIvyLeafSceneDXR12OMM::Update(float secs) {
  
}

void MyParisIvyLeafSceneDXR12OMM::OnKeyDown(uint32_t k) {
  if (k == VK_SPACE) {
    
  }
  else if (k == 'O' || k == 'o') {
    
  }
  else if (k == 'd' || k == 'D') {  // Dump + Debug (map OMM's primidx to normal idx)
    
  }
}