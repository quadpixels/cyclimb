#include "MyScene.h"
#include <source_location>
#include <wchar.h>

#ifndef NDEBUG
#include "x64\Debug\CompiledShaders\culling_mode_shaders.hlsl.h"
#else
#include "x64\Release\CompiledShaders\culling_mode_shaders.hlsl.h"
#endif

static const wchar_t* RAY_FLAG_LABELS[] = {
   L"FORCE_OPAQUE",
   L"FORCE_NON_OPAQUE",
   L"CULL_BACK_FACING_TRI",
   L"CULL_FRONT_FACING_TRI",
   L"CULL_OPAQUE",
   L"CULL_NON_OPAQUE",
   L"SKIP_TRI",
   L"SKIP_PROCEDURAL",
};

static const uint32_t RAY_FLAG_MASKS[] = {
  0x01,
  0x02,
  0x10,
  0x20,
  0x40,
  0x80,
  0x100,
  0x200
};

static const wchar_t* INST_FLAG_LABELS[] = {
  L"TRIANGLE_CULL_DISABLE",
  L"TRIANGLE_FRONT_CCW",
  L"FORCE_OPAQUE",
  L"FORCE_NON_OPAQUE",
};

static const uint32_t INST_FLAG_MASKS[] = {
  0x1,
  0x2,
  0x4,
  0x8
};

static const int NUM_MENU_CHOICES = _countof(RAY_FLAG_LABELS) + _countof(INST_FLAG_LABELS);

#ifndef CE
#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}
#endif

void MyCullingScene::BuildOrRebuildAS() {
  // =============== Tri AS =====================
  const float x0 = 0.01f;
  std::vector<glm::vec3> tris = {
    { -1 - x0, 1 - x0, 0 },
    { -1 - x0, -1 - x0, 0 },
    { 1 - x0, -1 - x0, 0 },

    { -1 + x0, 1 + x0, 0 },
    { 1 + x0, 1 + x0, 0 },
    { 1 + x0, -1 + x0, 0 }
  };

  std::vector<glm::vec2> deltas = {
    { -0.205 + 0.5, 0 + 0.75 },
    { +0.205 + 0.5, 0 + 0.75 }
  };

  std::vector<float> scales = {
    0.2,
    0.2
  };

  ID3D12Resource* vertex_bufs[2];
  for (uint32_t i = 0; i < 2; i++) {
    std::vector<glm::vec3> vs;
    for (uint32_t j = 0; j < tris.size(); j++) {
      vs.push_back(tris[j] * scales[i] + glm::vec3(deltas[i], 0.0f));
    }
    framework->CreateBufferForCPUSideData(vs.data(), sizeof(vs[0]) * tris.size(), &(vertex_bufs[i]));
  }

  std::vector<ID3D12Resource*> bufs = { vertex_bufs[0], vertex_bufs[1] };
  std::vector<uint32_t> counts = { 6, 6 };
  std::vector<D3D12_RAYTRACING_GEOMETRY_FLAGS> gfs = { D3D12_RAYTRACING_GEOMETRY_FLAG_NONE, D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE };
  framework->BuildBLAS(&blas_result_tri, bufs, sizeof(glm::vec3), counts, gfs);

  // =============== Proc AS ====================
  std::vector<glm::vec3> aabb_lbubs = {
    { -1 - x0, -1 - x0, -0.01 },
    {  1 + x0,  1 + x0,  0.01 }
  };
  std::vector<glm::vec2> deltas1 = {
    { -0.21 + 0.5, 0.3 },
    { +0.21 + 0.5, 0.3 }
  };
  std::vector<ID3D12Resource*> aabb_bufs(2);
  for (uint32_t i = 0; i < 2; i++) {
    glm::vec3 lb = aabb_lbubs[0];
    glm::vec3 ub = aabb_lbubs[1];
    lb = lb * scales[i] + glm::vec3(deltas1[i], 0.0f);
    ub = ub * scales[i] + glm::vec3(deltas1[i], 0.0f);
    D3D12_RAYTRACING_AABB aabb = { lb.x, lb.y, lb.z, ub.x, ub.y, ub.z };
    framework->CreateBufferForCPUSideData(&aabb, sizeof(aabb), &(aabb_bufs[i]));
  }
  std::vector<uint32_t> aabb_counts = { 1, 1 };

  framework->BuildBLASProc(&blas_result_proc, aabb_bufs, sizeof(D3D12_RAYTRACING_AABB), aabb_counts, gfs);

  std::vector<D3D12_RAYTRACING_INSTANCE_DESC> inst_descs(2);
  ID3D12Resource* blases[] = { blas_result_tri, blas_result_proc };
  for (uint32_t i = 0; i < 2; i++) {
    inst_descs[i] = {};
    inst_descs[i].AccelerationStructure = blases[i]->GetGPUVirtualAddress();
    inst_descs[i].Flags = inst_flag;
    inst_descs[i].InstanceContributionToHitGroupIndex = i;
    inst_descs[i].InstanceID = 0;
    inst_descs[i].InstanceMask = 0xFF;
    inst_descs[i].Transform[0][0] = 1.0f;
    inst_descs[i].Transform[1][1] = 1.0f;
    inst_descs[i].Transform[2][2] = 1.0f;
  }

  std::vector<ID3D12Resource*> lss_pos_bufs(2);
  std::vector<ID3D12Resource*> lss_radii_bufs(2);
  if (has_nvapi) {
    for (uint32_t i = 0; i < 2; i++) {
      std::vector<glm::vec3> lss_poses = {
        { -0.1, -0.6, 0.0 },
        {  0.1, -0.4, 0.0 }
      };
      for (uint32_t j = 0; j < 2; j++) {
        lss_poses[j] += glm::vec3(deltas1[i], 0.0);
      }
      framework->CreateBufferForCPUSideData(lss_poses.data(), lss_poses.size() * sizeof(lss_poses[0]), &lss_pos_bufs[i]);
      std::vector<float> lss_radii = {
        0.1, 0.1
      };
      framework->CreateBufferForCPUSideData(lss_radii.data(), lss_radii.size() * sizeof(lss_radii[0]), &lss_radii_bufs[i]);
    }
    std::vector<uint32_t> vert_counts = { 2, 2 };
    std::vector<uint32_t> index_counts = { 2, 2 };
    std::vector<uint32_t> prim_counts = { 2, 2 };
    std::vector<NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE> endcap_modes(2, NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED);
    std::vector<NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT> prim_formats(2, NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_LIST);
    framework->BuildBLASLSS(&blas_result_lss, lss_pos_bufs, lss_radii_bufs, { nullptr, nullptr }, vert_counts, index_counts, prim_counts,
      endcap_modes, prim_formats,
      gfs);


    D3D12_RAYTRACING_INSTANCE_DESC d{};
    d.AccelerationStructure = blas_result_lss->GetGPUVirtualAddress();
    d.Flags = inst_flag;
    d.InstanceContributionToHitGroupIndex = 0;
    d.InstanceID = 0;
    d.InstanceMask = 0xFF;
    d.Transform[0][0] = 1.0f;
    d.Transform[1][1] = 1.0f;
    d.Transform[2][2] = 1.0f;
    inst_descs.push_back(d);
  }

  framework->BuildTLAS(&tlas_result, inst_descs);

  vertex_bufs[0]->Release();
  vertex_bufs[1]->Release();
  aabb_bufs[0]->Release();
  aabb_bufs[1]->Release();

  // Refresh SRV
  framework->CreateSRVAccelerationStructure(tlas_result, cbvsrvuav_heap, has_nvapi ? 2 : 1);
}

MyCullingScene::MyCullingScene(MyFramework* f) : MyScene(f) {
  text_pass = new TextPass(framework->GetDevice(), framework->GetCommandQueue(), framework->GetGraphicsCommandList(), framework->GetCommandAllocator());
  text_pass->AllocateConstantBuffers(2048);
  text_pass->InitD3D12(nullptr);
  text_pass->InitFreetype();

  // NVAPI ?
  uint32_t nvapi_uav = 100;
  uint32_t nvapi_space = 0;
  NvAPI_Status status = NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(f->GetDevice(), nvapi_uav, nvapi_space);
  if (status != NVAPI_OK) {
    printf("Oh! error setting NVShaderExtnSlotSpaceLocalThread\n");
    has_nvapi = false;
  }

  // ============== RootSig and Pipeline =================
  if (has_nvapi)  // [Output UAV] [AS SRV] [PerScene CBV] 
    framework->CreateNvapiEnabledGlobalRootSig(&global_rootsig, 1, 1, 1, true, nvapi_uav);
  else            // [Output UAV] [NVAPI UAV] [AS SRV] [PerScene CBV]
    framework->CreateGlobalRootSig(&global_rootsig, 1, 1, 1);

  MyFramework::MyRtShaderListInfo info{};
  D3D12_HIT_GROUP_DESC hitgroup1{};
  hitgroup1.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hitgroup1.ClosestHitShaderImport = L"MyClosestHitShader";
  hitgroup1.HitGroupExport = L"MyHitGroup1";
  D3D12_HIT_GROUP_DESC hitgroup2{};
  hitgroup2.Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
  hitgroup2.ClosestHitShaderImport = L"MyClosestHitShader";
  hitgroup2.HitGroupExport = L"MyHitGroup2";
  hitgroup2.IntersectionShaderImport = L"MyIntersectionShader";
  info.hit_groups = { hitgroup1, hitgroup2 };
  info.raygen_shader = L"MyRayGenShader";
  info.miss_shader = L"MyMissShader";
  info.dxil_lib_bytecode = (void*)g_CullingSceneShaders;
  info.dxil_lib_length = sizeof(g_CullingSceneShaders);
  f->CreateMyRtPipeline(&my_rt_pipeline, global_rootsig, info);

  // DescriptorHeap
  framework->CreateRtOutputResource(&rt_output_resource);
  uint32_t cb_size = 256;
  framework->CreateBufferForCPUSideData(nullptr, cb_size, &perscene_cb);
  framework->CreateCBVSRVUAVHeap(&cbvsrvuav_heap, &cbvsrvuav_heap_cpu, has_nvapi ? 4 : 3);
  framework->CreateCBVBuffer(perscene_cb, cbvsrvuav_heap, has_nvapi ? 3 : 2, cb_size);
  framework->CreateUAVTexture2D(rt_output_resource, cbvsrvuav_heap, 0);
  if (has_nvapi) {
    framework->CreateNullUAV(cbvsrvuav_heap, 1);
  }

  BuildOrRebuildAS();
}

void MyCullingScene::Render() {
  D3D12_CPU_DESCRIPTOR_HANDLE handle_rtv = framework->GetCurrRenderTargetCPUDescriptor();
  float bg_color[] = { 0.1f, 0.1f, 0.1f, 1.0f };
  ID3D12CommandAllocator* command_allocator = framework->GetCommandAllocator();
  ID3D12GraphicsCommandList4* command_list = framework->GetGraphicsCommandList();
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));
  ID3D12Resource* rendertarget = framework->GetCurrentRenderTarget();
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);

  // DispatchRays
  {
    command_list->SetComputeRootSignature(global_rootsig);
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav;
    command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&cbvsrvuav_heap);
    handle_uav = CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvsrvuav_heap->GetGPUDescriptorHandleForHeapStart(), 0, framework->GetCBVSRVUAVDescriptorSize());
    command_list->SetComputeRootDescriptorTable(0, handle_uav);
    command_list->SetPipelineState1(my_rt_pipeline.rt_state_object);
    command_list->DispatchRays(&(my_rt_pipeline.dispatch_rays_desc));
  }
  static bool is_first_frame = true;
  if (!is_first_frame) {
    ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  }
  is_first_frame = false;
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
  command_list->CopyResource(rendertarget, rt_output_resource);
  ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
  CE(command_list->Close());
  ID3D12CommandQueue* command_queue = framework->GetCommandQueue();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  framework->WaitForPreviousFrame();

  // Text pass, new cmd list
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, text_pass->pipeline_state));
  text_pass->StartPass();
  command_list->SetGraphicsRootSignature(text_pass->root_signature);
  // TextPass's rendering procedure
  ID3D12DescriptorHeap* ppHeaps_textpass[] = { text_pass->srv_heap };
  wchar_t buf[50];

  {
    float y = 24.0f;
    wsprintf(buf, L"Ray & inst flag:");
    glm::vec3 textcolor(0, 1, 1);
    text_pass->AddText(buf, 12.0f, y, 1.0f, textcolor, glm::mat4(1));
    y += 24.0f;
    for (uint32_t i = 0; i < _countof(RAY_FLAG_LABELS); i++) {
      std::wstring s = RAY_FLAG_LABELS[i];
      if (h_perscene_cb.ray_flag & RAY_FLAG_MASKS[i]) {
        s += L" = 1";
        textcolor = glm::vec3(0, 1, 1);
      }
      else {
        s += L" = 0";
        textcolor = glm::vec3(0.3);
      }
      text_pass->AddText(s.c_str(), 12.0f, y, 1.0f, textcolor, glm::mat4(1));
      if (choice_idx == i) {
        text_pass->AddText(L">", 4.0f, y, 1.0f, glm::vec3(0,1,1), glm::mat4(1));
      }
      y += 16.0f;
    }

    y += 24.0f;
    
    for (uint32_t i = 0; i < _countof(INST_FLAG_LABELS); i++) {
      std::wstring s = INST_FLAG_LABELS[i];
      if (inst_flag & INST_FLAG_MASKS[i]) {
        s += L" = 1";
        textcolor = glm::vec3(0, 1, 1);
      }
      else {
        s += L" = 0";
        textcolor = glm::vec3(0.3);
      }
      text_pass->AddText(s.c_str(), 12.0f, y, 1.0f, textcolor, glm::mat4(1));
      if (choice_idx == i + _countof(RAY_FLAG_LABELS)) {
        text_pass->AddText(L">", 4.0f, y, 1.0f, glm::vec3(0, 1, 1), glm::mat4(1));
      }
      y += 16.0f;
    }

    float the_y = MyFramework::WIN_H * 0.65f;
    if (has_nvapi) {
      the_y += MyFramework::WIN_H * 0.25f;
    }
    text_pass->AddText(L"Geom Flag",
      MyFramework::WIN_W*0.75f - 40,
      the_y + 16, 1.0, glm::vec3(0,1,1), glm::mat4(1));
    text_pass->AddText(L"NONE",
      MyFramework::WIN_W * 0.6f - 1,
      the_y, 1.0, glm::vec3(0, 1, 1), glm::mat4(1));
    text_pass->AddText(L"FORCE_OPAQUE",
      MyFramework::WIN_W * 0.8f - 30,
      the_y, 1.0, glm::vec3(0, 1, 1), glm::mat4(1));
  }

  std::vector<std::wstring> infos;
  // Per spec:
  {
    // Ray flag: FORCE_OPAQUE, FORCE_NON_OPAQUE, CULL_OPAQUE, CULL_NON_OPAQUE are mutually exclusive
    uint32_t count = 0;
    count += bool(h_perscene_cb.ray_flag & D3D12_RAY_FLAG_FORCE_OPAQUE);
    count += bool(h_perscene_cb.ray_flag & D3D12_RAY_FLAG_FORCE_NON_OPAQUE);
    count += bool(h_perscene_cb.ray_flag & D3D12_RAY_FLAG_CULL_OPAQUE);
    count += bool(h_perscene_cb.ray_flag & D3D12_RAY_FLAG_CULL_NON_OPAQUE);
    if (count > 1) {
      infos.push_back(L"Ray flag: FORCE_OPAQUE, FORCE_NON_OPAQUE, CULL_OPAQUE,");
      infos.push_back(L"CULL_NON_OPAQUE are mutually exclusive.");
    }
  }

  // Per spec: CULL_FRONT_FACING and CULL_BACK_FACING are mutually exclusive
  {
    uint32_t count1 = 0;
    count1 += bool(h_perscene_cb.ray_flag & D3D12_RAY_FLAG_CULL_BACK_FACING_TRIANGLES);
    count1 += bool(h_perscene_cb.ray_flag & D3D12_RAY_FLAG_CULL_FRONT_FACING_TRIANGLES);
    if (count1 > 1) {
      infos.push_back(L"Ray flag: CULL_{FRONT,BACK}_FACING_TRIANGLE are");
      infos.push_back(L"mutually exclusive.");
    }
  }

  // Per spec: SKIP_TRIANGLE and SKIP_PROCEDURAL_PRIMITIVES are mutually exclusive
  {
    uint32_t count1 = 0;
    count1 += bool(h_perscene_cb.ray_flag & D3D12_RAY_FLAG_SKIP_TRIANGLES);
    count1 += bool(h_perscene_cb.ray_flag & D3D12_RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES);
    if (count1 > 1) {
      infos.push_back(L"Ray flag: SKIP_TRIANGLE and SKIP_PROCEDURAL_PRIMITIVES");
      infos.push_back(L"are mutually exclusive.");
    }
  }

  // Per spec: Instance's FORCE_OPAQUE and FORCE_NON_OPAQUE are mutually exclusive
  {
    uint32_t count1 = 0;
    count1 += bool(inst_flag & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE);
    count1 += bool(inst_flag & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE);
    if (count1 > 1) {
      infos.push_back(L"Instance flag: FORCE_OPAQUE and FORCE_NON_OPAQUE");
      infos.push_back(L"are mutually exclusive.");
    }
  }

  for (uint32_t i = 0; i < infos.size(); i++) {
    text_pass->AddText(infos[i].c_str(), 4.0f, 372 + 18 * i, 1.0f, glm::vec3(1, 0.5, 0.5), glm::mat4(1));
  }

  command_list->SetDescriptorHeaps(_countof(ppHeaps_textpass), ppHeaps_textpass);
  D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f * MyFramework::WIN_W, 1.0f * MyFramework::WIN_H, 0.0f, 1.0f);
  D3D12_RECT scissor = CD3DX12_RECT(0, 0, long(MyFramework::WIN_W), long(MyFramework::WIN_H));
  command_list->RSSetViewports(1, &viewport);
  command_list->RSSetScissorRects(1, &scissor);
  command_list->OMSetRenderTargets(1, &handle_rtv, false, nullptr);
  float blend_factor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
  command_list->OMSetBlendFactor(blend_factor);
  text_pass->RenderText(command_list);
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  CE(command_list->Close());
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(framework->Present());
  framework->WaitForPreviousFrame();
}

void MyCullingScene::Update(float secs) {
  if (is_as_dirty) {
    BuildOrRebuildAS();
    is_as_dirty = false;
  }

  void* mapped;
  perscene_cb->Map(0, nullptr, &mapped);
  memcpy(mapped, &h_perscene_cb, sizeof(h_perscene_cb));
  perscene_cb->Unmap(0, nullptr);
}

void MyCullingScene::OnKeyUp(uint32_t k) {

}

void MyCullingScene::do_ChangeChoice(int delta) {
  if (choice_idx == -1) {
    if (delta > 0) {
      choice_idx = 0;
    }
    else {
      choice_idx = NUM_MENU_CHOICES - 1;
    }
  }
  else {
    choice_idx += delta;
    if (choice_idx >= NUM_MENU_CHOICES) choice_idx = -1;
    else if (choice_idx < 0) choice_idx = -1;
  }
}

void MyCullingScene::do_ChangeOption(int delta) {
  if (choice_idx == -1) return;
  if (choice_idx < _countof(RAY_FLAG_LABELS)) {
    uint32_t m = RAY_FLAG_MASKS[choice_idx];
    if (delta < 0) {
      h_perscene_cb.ray_flag &= (~m);
    }
    else {
      h_perscene_cb.ray_flag |= m;
    }
  }
  else if (choice_idx <= NUM_MENU_CHOICES) {
    uint32_t m = INST_FLAG_MASKS[choice_idx - _countof(RAY_FLAG_LABELS)];
    if (delta < 0) {
      inst_flag &= (~m);
    }
    else {
      inst_flag |= m;
    }
    is_as_dirty = true;
  }
}

void MyCullingScene::OnKeyDown(uint32_t k) {
  switch (k) {
  case VK_UP:
    do_ChangeChoice(-1); break;
  case VK_DOWN:
    do_ChangeChoice(1); break;
  case VK_LEFT:
    do_ChangeOption(-1);
    break;
  case VK_RIGHT:
    do_ChangeOption(1);
    break;
  }
}