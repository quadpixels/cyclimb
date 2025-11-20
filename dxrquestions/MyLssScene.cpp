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

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
  case_idx = 1;
  switch (case_idx) {
  case 0: {
    lss_poses = {
      { 0.0, 0.0, 0.5 },
      { 0.0, 0.0, 0.0 },
      { 0.0, 0.0, -0.1 },
      { 0.0, 0, -1 },
    };
    lss_radii = {
      0.2, 0.2, 0.1, 0.1
    };
    lss_indices = { 0, 1, 1, 2, 2, 3 };
    lss_indices_successive = { 0, 1, 2 };
    break;
  }
  case 1: {
    const float L = 20000;
    lss_poses = {
      //{ 0, 0, -18 },
      //{ 0, 0, 7 },
      { 0, 0, -9 },
      { -L, 0, -9 - L },
    };
    lss_radii = {
      1, 8.0
      //30, 15
    };
    lss_indices = { 0, 1 };
    lss_indices_successive = { 0 };
    cam_elevation = 0;
    cam_azimuth = 0;// atan(0.7499);
    cam_mode = 1;
    //cam_pos = { -60, 0, -80 };
    cam_pos = { 0, 0, 100 };
    break;
  }
  
  }
  

  uint32_t nvapi_uav = 100;
  uint32_t nvapi_space = 0;
  NvAPI_Status status = NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(f->GetDevice(), nvapi_uav, nvapi_space);
  if (status != NVAPI_OK) {
    printf("Oh! error setting NVShaderExtnSlotSpaceLocalThread\n");
    exit(0);
  }
  // Layout: [output UAV] [Debug UAV] [NVAPI UAV] [AS SRV] [PerScene CBV]
  f->CreateRtGlobalRootSig(&global_rootsig, 2, 1, 1, true, nvapi_uav);
  f->CreateRtOutputResource(&rt_output_resource);
  f->CreateBufferForCPUSideData(nullptr, AlignUp(sizeof(PerSceneCB), 256), &per_scene_cb);
  f->CreateCBVSRVUAVHeap(&cbvsrvuav_heap, nullptr, 5);
  f->CreateCBVBuffer(per_scene_cb, cbvsrvuav_heap, 4, AlignUp(sizeof(PerSceneCB), 256));
  f->CreateUAVTexture2D(rt_output_resource, cbvsrvuav_heap, 0);
  f->CreateBufferForUAVAccess(sizeof(uint32_t) * 6, &my_debug_resource);
  f->CreateUAVUintBuffer(my_debug_resource, 6, sizeof(uint32_t), cbvsrvuav_heap, 1);
  f->CreateBufferForCPUAccess(6 * sizeof(uint32_t), &my_debug_resource_cpu);
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
  std::vector<glm::vec3> lss_poses1 = lss_poses;
  for (uint32_t i = 0; i < lss_poses1.size(); i++) {
      lss_poses1[i].y += 0.5;
  }
  f->CreateBufferForCPUSideData(lss_poses1.data(),
    lss_poses1.size() * sizeof(glm::vec3), &lss_pos_resource1);
  f->CreateBufferForCPUSideData(lss_radii.data(),
    lss_radii.size() * sizeof(float), &lss_radii_resource);
  f->CreateBufferForCPUSideData(lss_indices.data(),
    lss_indices.size() * sizeof(uint32_t), &lss_indices_list_resource);
  f->CreateBufferForCPUSideData(lss_indices_successive.data(),
    lss_indices_successive.size() * sizeof(uint32_t), &lss_indices_successive_resource);
  f->BuildDummyLSS(
    &blas_result,
    &tlas_result,
    lss_pos_resource,
    lss_pos_resource1,
    lss_radii_resource,
    lss_indices_list_resource,
    lss_indices_successive_resource,
    lss_poses.size(),  // vert count
    NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_NONE,
    NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_LIST,
    case_idx);
  f->CreateSRVAccelerationStructure(tlas_result, cbvsrvuav_heap, 3);
  f->CreateNullUAV(cbvsrvuav_heap, 2);
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
    ResourceBarrierTransition(command_list, my_debug_resource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
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
    ResourceBarrierTransition(command_list, my_debug_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    command_list->CopyResource(my_debug_resource_cpu, my_debug_resource);
    ResourceBarrierTransition(command_list, my_debug_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
  }

  CE(command_list->Close());

  ID3D12CommandQueue* command_queue = framework->GetCommandQueue();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(framework->Present());
  framework->WaitForPreviousFrame();

  {
    float* mapped;
    my_debug_resource_cpu->Map(0, nullptr, (void**)&mapped);
    printf("Ray in center, o=(%g,%g,%g), d=(%g,%g,%g)\n",
      mapped[0], mapped[1], mapped[2], mapped[3], mapped[4], mapped[5]);
    my_debug_resource_cpu->Unmap(0, nullptr);
  }
}

void MyLssScene::Update(float delta_secs) {
    cam_azimuth += delta_secs * (float)rot_axes[1];
    cam_elevation -= delta_secs * (float)rot_axes[0];

    glm::mat4 rot(1);
    rot = glm::rotate(rot, cam_azimuth, glm::vec3(0, 1, 0));
    rot = glm::rotate(rot, cam_elevation, glm::vec3(1, 0, 0));
    cam_lookdir = glm::mat3(rot) * glm::vec3(0, 0, -1);

    static float secs{0};
    secs += delta_secs;
    PerSceneCB cb{};

    glm::mat4 proj;
    float W = (float)(MyFramework::WIN_W);
    float H = (float)(MyFramework::WIN_H);
    cam_view_matrix = glm::lookAt(cam_pos, cam_pos + cam_lookdir, cam_up);
    if (cam_mode == 1) {
        proj = glm::ortho(-W / 30, W / 30, -H / 30, H / 30, -0.01f, -499.0f);
    }
    else if (cam_mode == 0) {
        proj = glm::perspectiveRH_ZO(glm::radians(60.0f),
            1.0f * W / H, -0.01f, -49999.0f) * (1.0f);
    }

    cam_pos += glm::mat3(rot) * glm::vec3(1, 0, 0) * (float)(axes[0]) * delta_secs;
    cam_pos += glm::mat3(rot) * glm::vec3(0, 1, 0) * (float)(axes[1]) * delta_secs;
    cam_pos += glm::mat3(rot) * glm::vec3(0, 0, 1) * (float)(axes[2]) * delta_secs;

    glm::mat4 inv_view = glm::inverse(cam_view_matrix);
    glm::mat4 inv_proj = glm::inverse(proj);
    GlmMat4ToDirectXMatrixColMajor(&cb.inverse_view, inv_view);
    GlmMat4ToDirectXMatrixColMajor(&cb.inverse_proj, inv_proj);
    cb.viz_mode = viz_mode;
    cb.cam_mode = cam_mode;

    void* mapped{};
    per_scene_cb->Map(0, nullptr, &mapped);
    memcpy(mapped, (const void*)&cb, sizeof(PerSceneCB));
}

void MyLssScene::OnKeyDown(uint32_t k) {
    switch (k) {
        case 'W': {
            axes[2] = -1; break;
        }
        case 'S': {
            axes[2] = 1; break;
        }
        case 'A': {
            axes[0] = -1; break;
        }
        case 'D': {
            axes[0] = 1; break;
        }
        case 'Q': {
            axes[1] = -1; break;
        }
        case 'E': {
            axes[1] = 1; break;
        }
        case 'I': {
            rot_axes[0] = 1; break;
        }
        case 'K': {
            rot_axes[0] = -1; break;
        }
        case 'J': {
            rot_axes[1] = 1; break;
        }
        case 'L': {
            rot_axes[1] = -1; break;
        }
        case '0': {
            cam_elevation = 0; break;
        }
        case 219: {  // '['
            printf("Using perspective camera\n");
            //viz_mode = 1; break;
            cam_mode = 0;
            break;
        }
        case 221: {  // ']'
            printf("Using orthogonal camera\n");
            //viz_mode = 2;
            cam_mode = 1;
            break;
        }
    }
}

void MyLssScene::OnKeyUp(uint32_t k) {
    switch (k) {
        case 'W':
        case 'S': {
            axes[2] = 0; break;
        }
        case 'A':
        case 'D': {
            axes[0] = 0; break;
        }
        case 'Q':
        case 'E': {
            axes[1] = 0; break;
        }
        case 'I':
        case 'K': {
            rot_axes[0] = 0; break;
        }
        case 'J':
        case 'L': {
            rot_axes[1] = 0; break;
        }
    }
}