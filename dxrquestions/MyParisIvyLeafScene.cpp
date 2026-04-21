#include "MyParisIvyLeafScene.h"
#include "MyFramework.h"

#include <source_location>

#ifndef NDEBUG
#include "x64\Debug\g_PixelShaderIvyLeafTriangle.h"
#include "x64\Debug\g_VertexShaderIvyLeafTriangle.h"
#include "x64/Debug/CompiledShaders/raytracing_shaders.hlsl.h"
#else
#include "x64\Release\g_PixelShaderIvyLeafTriangle.h"
#include "x64\Release\g_VertexShaderIvyLeafTriangle.h"
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

MyParisIvyLeafScene::MyParisIvyLeafScene(MyFramework* f) : MyScene(f) {
  const uint32_t num_verts = vertices.size();
  const uint32_t num_pixels = f->WIN_H * f->WIN_W;
  const uint32_t cb_size = 256;  // Multiple of 256
  f->CreateNvapiEnabledGlobalRootSig(&global_rootsig, 2, 4, 1, false, 0);
  f->CreateRtOutputResource(&rt_output_resource);
  f->CreateBufferForUAVAccess(num_pixels * 4, &my_debug_resource);
  my_debug_resource->SetName(L"My debug resource");
  f->CreateCBVSRVUAVHeap(&cbvsrvuav_heap, nullptr, 7);
  f->CreateCBVSRVUAVHeap(&texture_srv_heap, nullptr, 2);
  f->CreateUAVTexture2D(rt_output_resource, cbvsrvuav_heap, 0);
  f->CreateUAVUintBuffer(my_debug_resource, num_pixels, sizeof(uint32_t), cbvsrvuav_heap, 1);
  f->CreateVertexBuffer(vertices, &vertex_buffer, &vbv);
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
  f->CreateHelloTriangleRootSig(&rast_rootsig);
  f->CreateMyPipelineState(&rast_pipeline, rast_rootsig);
  f->LoadTextureFromImage(&diffuse_texture, "textures/Paris_ivy_leaf_a_diff.png");
  f->LoadTextureFromImage(&alpha_texture, "textures/Paris_ivy_leaf_a_mask.png");
  f->CreateSRVTexture2D(diffuse_texture, texture_srv_heap, 0);
  f->CreateSRVTexture2D(alpha_texture, texture_srv_heap, 1);
  f->BuildBLAS(&blas_result, vertex_buffer, sizeof(Vertex), num_verts);
  f->BuildTLAS(&tlas_result, blas_result);
  f->CreateSRVAccelerationStructure(tlas_result, cbvsrvuav_heap, 2);
  f->CreateSRVTexture2D(diffuse_texture, cbvsrvuav_heap, 3);
  f->CreateSRVTexture2D(alpha_texture, cbvsrvuav_heap, 4);
  f->CreateSRVBuffer(vertex_buffer, cbvsrvuav_heap, 5, num_verts, sizeof(Vertex));
  f->CreateBufferForCPUSideData(nullptr, cb_size, &my_cb_resource);
  f->CreateCBVBuffer(my_cb_resource, cbvsrvuav_heap, 6, cb_size);
  f->CreateBufferForCPUAccess(num_pixels * sizeof(uint32_t), &my_debug_resource_cpu);

  // OMM stuff.
  if (f->IsOMMSupported()) {
    f->BuildDummyOMM(vertex_buffer, sizeof(Vertex), &blas_result_omm, &tlas_result_omm);
    f->CreateCBVSRVUAVHeap(&cbvsrvuav_heap_omm, nullptr, 7);
    f->CreateUAVTexture2D(rt_output_resource, cbvsrvuav_heap_omm, 0);
    f->CreateUAVUintBuffer(my_debug_resource, num_pixels, sizeof(uint32_t), cbvsrvuav_heap_omm, 1);
    f->CreateSRVAccelerationStructure(tlas_result_omm, cbvsrvuav_heap_omm, 2);
    f->CreateSRVTexture2D(diffuse_texture, cbvsrvuav_heap_omm, 3);
    f->CreateSRVTexture2D(alpha_texture, cbvsrvuav_heap_omm, 4);
    f->CreateSRVBuffer(vertex_buffer, cbvsrvuav_heap_omm, 5, num_verts, sizeof(Vertex));
    f->CreateCBVBuffer(my_cb_resource, cbvsrvuav_heap_omm, 6, cb_size);
  }
}

void MyParisIvyLeafScene::Render() {
  // Clear screen

  D3D12_CPU_DESCRIPTOR_HANDLE handle_rtv = framework->GetCurrRenderTargetCPUDescriptor();
  float bg_color[] = { 0.8f, 1.0f, 1.0f, 1.0f };
  ID3D12CommandAllocator* command_allocator = framework->GetCommandAllocator();
  ID3D12GraphicsCommandList4* command_list = framework->GetGraphicsCommandList();
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));
  ID3D12Resource* rendertarget = framework->GetCurrentRenderTarget();
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  if (is_rt) {
    ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (my_cb_cpu.is_dump_debuginfo) {
      ResourceBarrierTransition(command_list, my_debug_resource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    command_list->SetComputeRootSignature(global_rootsig);
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav;
    if (!is_omm) {
      command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&cbvsrvuav_heap);
      handle_uav = CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvsrvuav_heap->GetGPUDescriptorHandleForHeapStart(), 0, framework->GetCBVSRVUAVDescriptorSize());
    }
    else {
      command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&cbvsrvuav_heap_omm);
      handle_uav = CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvsrvuav_heap_omm->GetGPUDescriptorHandleForHeapStart(), 0, framework->GetCBVSRVUAVDescriptorSize());
    }
    command_list->SetComputeRootDescriptorTable(0, handle_uav);
    command_list->SetPipelineState1(my_rt_pipeline.rt_state_object);
    command_list->DispatchRays(&(my_rt_pipeline.dispatch_rays_desc));
    ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    command_list->CopyResource(rendertarget, rt_output_resource);
    ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    if (my_cb_cpu.is_dump_debuginfo) {
      ResourceBarrierTransition(command_list, my_debug_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
      command_list->CopyResource(my_debug_resource_cpu, my_debug_resource);
      ResourceBarrierTransition(command_list, my_debug_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    }
  }
  else {
    command_list->SetPipelineState(rast_pipeline);
    command_list->SetGraphicsRootSignature(rast_rootsig);
    command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&texture_srv_heap);
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle_srv(texture_srv_heap->GetGPUDescriptorHandleForHeapStart());
    command_list->SetGraphicsRootDescriptorTable(0, handle_srv);
    D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f * (framework->WIN_W), 1.0f * (framework->WIN_H), 0.0f, 1.0f);
    D3D12_RECT scissor = CD3DX12_RECT(0, 0, long(framework->WIN_W), long(framework->WIN_H));
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor);
    command_list->OMSetRenderTargets(1, &handle_rtv, false, nullptr);
    float blend_factor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    command_list->OMSetBlendFactor(blend_factor);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 1, &vbv);
    command_list->DrawInstanced(6, 1, 0, 0);
    ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  }
  CE(command_list->Close());

  ID3D12CommandQueue* command_queue = framework->GetCommandQueue();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(framework->Present());
  framework->WaitForPreviousFrame();

  if (my_cb_cpu.is_dump_debuginfo) {
    uint32_t* mapped;
    my_debug_resource_cpu->Map(0, nullptr, (void**)&mapped);
    printf("Print something\n");
    const uint32_t npixels = framework->WIN_H * framework->WIN_W;
    const uint32_t stepsize = npixels / 100;
    for (uint32_t i = 0; i < npixels; i += stepsize) {
      printf("[%u] = %x\n", i, mapped[i]);
    }
    if (prim_idx_mapping_state == PrimIdxMappingState::GetNonOMMResult) {
      prim_idxes_non_omm.clear();
      prim_idxes_omm.clear();
      for (uint32_t i = 0; i < npixels; i += stepsize) {
        prim_idxes_non_omm.push_back(mapped[i]);
      }
      prim_idx_mapping_state = PrimIdxMappingState::GetOMMResult;
      is_omm = true;
    }
    else if (prim_idx_mapping_state == PrimIdxMappingState::GetOMMResult) {
      for (uint32_t i = 0; i < npixels; i += stepsize) {
        prim_idxes_omm.push_back(mapped[i]);
      }
      int mapped_miss = -999, mapped_prim0 = -999, mapped_prim1 = -999;
      for (uint32_t i = 0; i < prim_idxes_omm.size(); i++) {
        int pidx_nonomm = prim_idxes_non_omm[i];
        int pidx_omm = prim_idxes_omm[i];
        auto do_update_map = [&](int& mapped_pidx, int target) {
          printf("nonomm=%d  omm=%d  tgt=%d\n",
            pidx_nonomm, pidx_omm, target);
          if (pidx_omm == -1) return;  // HACK
          if (pidx_nonomm == target) {
            if (mapped_pidx == -999) {
              mapped_pidx = pidx_omm;
            }
            else {
              if (mapped_pidx != pidx_omm) {
                mapped_pidx = -998;
              }
            }
          }
          };
        do_update_map(mapped_miss, -1);
        do_update_map(mapped_prim0, 0);
        do_update_map(mapped_prim1, 1);
      }
      printf("[mapping] miss=0x%x, prim0=0x%x, prim1=0x%x\n",
        mapped_miss, mapped_prim0, mapped_prim1);
      my_cb_cpu.omm_primidx0 = mapped_prim0;
      my_cb_cpu.omm_primidx1 = mapped_prim1;
      prim_idx_mapping_state = PrimIdxMappingState::NotStarted;
    }
    my_debug_resource_cpu->Unmap(0, nullptr);
    if (prim_idx_mapping_state == PrimIdxMappingState::NotStarted) {
      my_cb_cpu.is_dump_debuginfo = false;
    }
  }
}

void MyParisIvyLeafScene::Update(float secs) {
  void* mapped;
  my_cb_cpu.is_omm = is_omm;
  my_cb_resource->Map(0, nullptr, &mapped);
  memcpy(mapped, &my_cb_cpu, sizeof(MyConstantBufferStruct));
  my_cb_resource->Unmap(0, nullptr);
}

void MyParisIvyLeafScene::OnKeyDown(uint32_t k) {
  if (k == VK_SPACE) {
    is_rt = !is_rt;
  }
  else if (k == 'O' || k == 'o') {
    is_omm = !(is_omm);
    printf("is_omm = %d\n", is_omm);
  }
  else if (k == 'd' || k == 'D') {  // Dump + Debug (map OMM's primidx to normal idx)
    is_omm = false;
    my_cb_cpu.is_dump_debuginfo = true;
    prim_idx_mapping_state = PrimIdxMappingState::GetNonOMMResult;
  }
}