#include "scene.hpp"

#include "d3dx12.h"
#include "util.hpp"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

extern int WIN_W, WIN_H;
void WaitForPreviousFrame();
extern ID3D12Device* g_device12;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern int g_frame_index;
extern ID3D12Resource* g_rendertargets[];
extern unsigned g_rtv_descriptor_size;
extern ID3D12CommandQueue* g_command_queue;
extern IDXGISwapChain3* g_swapchain;

/*
cbuffer CBPerObject : register(b0) {
  float4x4 M;
  float4x4 V;
  float4x4 P;
};

cbuffer CBPerScene : register(b1) {
  float3 dir_light;
  float4x4 lightPV;
  float4 cam_pos;
}
*/

DX12ChunksScene::DX12ChunksScene() {
  // Chunk pass and per-object constant buffer
  chunk_pass = new ChunkPass();
  chunk_pass->AllocateConstantBuffers(20);
  chunk_pass->InitD3D12();
  InitCommandList();
  InitResources();
  total_secs = 0.0f;
}

void DX12ChunksScene::InitCommandList() {
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
    command_allocator, nullptr, IID_PPV_ARGS(&command_list)));
  CE(command_list->Close());
}

void DX12ChunksScene::InitResources() {
  // Projection Matrix
  projection_matrix = DirectX::XMMatrixPerspectiveFovLH(
    60.0f * 3.14159f / 180.0f,
    WIN_W * 1.0f / WIN_H, 0.01f, 499.0f);

  // Camera
  camera = new Camera();
  camera->pos = glm::vec3(0, 0, 80);
  camera->lookdir = glm::vec3(0, 0, -1);
  camera->up = glm::vec3(0, 1, 0);

  // Directional Light
  dir_light = new DirectionalLight(glm::vec3(-1, -3, -1), glm::vec3(1, 3, -1));

  // Chunk
  chunk = new Chunk();
  chunk->LoadDefault();
  chunk->BuildBuffers(nullptr);

  chunk_index = new ChunkGrid("../climb/chr.vox");
  chunk_sprite = new ChunkSprite(chunk_index);

  // Per-Scene Constant buffer's resource
  // 0-255：给所有Chunks共享的Per-Scene Buffer
  // 256-511：给Backdrop使用的Per-Object Buffer
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(512)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&d_per_scene_cb)));

  // CBV descriptor heap
  D3D12_DESCRIPTOR_HEAP_DESC cbv_heap_desc{};
  cbv_heap_desc.NumDescriptors = 3;  // Per-object CB, Per-scene CB, Shadow Map SRV
  cbv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  cbv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CE(g_device12->CreateDescriptorHeap(&cbv_heap_desc, IID_PPV_ARGS(&cbv_heap)));
  cbv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // Per Scene CB view
  D3D12_CONSTANT_BUFFER_VIEW_DESC per_scene_cbv_desc{};
  per_scene_cbv_desc.BufferLocation = d_per_scene_cb->GetGPUVirtualAddress();
  per_scene_cbv_desc.SizeInBytes = 512;   // Must be a multiple of 256

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle1(cbv_heap->GetCPUDescriptorHandleForHeapStart());
  g_device12->CreateConstantBufferView(&per_scene_cbv_desc, handle1);

  // Per Object CB view
  D3D12_CONSTANT_BUFFER_VIEW_DESC per_obj_cbv_desc{};
  per_obj_cbv_desc.BufferLocation = chunk_pass->d_per_object_cbs->GetGPUVirtualAddress();
  per_obj_cbv_desc.SizeInBytes = 256;
  handle1.Offset(cbv_descriptor_size);
  g_device12->CreateConstantBufferView(&per_obj_cbv_desc, handle1);

  // Depth buffer
  D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
  depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
  depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
  depthOptimizedClearValue.DepthStencil.Stencil = 0;

  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Tex2D(
      DXGI_FORMAT_R32_TYPELESS, WIN_W, WIN_H, 1, 0, 1, 0, 
      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)),
    D3D12_RESOURCE_STATE_DEPTH_WRITE,
    &depthOptimizedClearValue,
    IID_PPV_ARGS(&depth_buffer)));

  // DSV descriptor heap
  D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc{};
  dsv_heap_desc.NumDescriptors = 1;
  dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  CE(g_device12->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap)));
  dsv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

  // DSV
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = { };
  dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
  dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
  g_device12->CreateDepthStencilView(depth_buffer, &dsv_desc, dsv_heap->GetCPUDescriptorHandleForHeapStart());

  // GBuffer RTV & Shadow Map SRV & RTV
  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = 2,  // GBuffer 与 ShadowMap
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };
    CE(g_device12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtv_heap)));

    D3D12_CLEAR_VALUE zero{};
    zero.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32G32B32A32_FLOAT, WIN_W, WIN_H, 1, 0, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)),
      D3D12_RESOURCE_STATE_RENDER_TARGET,
      &zero,
      IID_PPV_ARGS(&gbuffer)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart());
    g_device12->CreateRenderTargetView(gbuffer, nullptr, rtv_handle);

    D3D12_CLEAR_VALUE shadow_map_clear{};
    shadow_map_clear.Format = DXGI_FORMAT_R32_FLOAT;
    shadow_map_clear.Color[0] = 1.0f;

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32_TYPELESS, 512, 512, 1, 0, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      &shadow_map_clear,
      IID_PPV_ARGS(&shadow_map)));
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    handle1.Offset(cbv_descriptor_size);
    g_device12->CreateShaderResourceView(shadow_map, &srv_desc, handle1);

    D3D12_RENDER_TARGET_VIEW_DESC shadow_map_rtv_desc{};
    shadow_map_rtv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    shadow_map_rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_handle.Offset(cbv_descriptor_size);
    g_device12->CreateRenderTargetView(shadow_map, &shadow_map_rtv_desc, rtv_handle);
  }

  // Backdrop
  const float L = 80.0f, H = -35.0f;
  float backdrop_verts[] = {  // X, Y, Z, nidx, data, ao
    -L, H, L, 4, 44, 0,
     L, H, L, 4, 44, 0,
    -L, H, -L, 4, 44, 0,
     L, H, L, 4, 44, 0,
    L, H, -L, 4, 44, 0,
    -L, H, -L, 4, 44, 0,
  };
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(backdrop_verts))),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&backdrop_vert_buf)));
  UINT8* pData;
  CD3DX12_RANGE readRange(0, 0);
  CE(backdrop_vert_buf->Map(0, &readRange, (void**)&pData));
  memcpy(pData, backdrop_verts, sizeof(backdrop_verts));
  backdrop_vert_buf->Unmap(0, nullptr);

  backdrop_vbv.BufferLocation = backdrop_vert_buf->GetGPUVirtualAddress();
  backdrop_vbv.StrideInBytes = sizeof(float) * 6;
  backdrop_vbv.SizeInBytes = sizeof(backdrop_verts);
}

void DX12ChunksScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, chunk_pass->pipeline_state));

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);
  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv_gbuffer(
    rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    0, 0);
  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv_shadowmap(
    rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    1, g_rtv_descriptor_size);

  float bg_color[] = { 0.8f, 0.8f, 0.8f, 1.0f };
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));

  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  float zero4[] = { 0.0f, 0.0f, 0.0f, 0.0f };
  command_list->ClearRenderTargetView(handle_rtv_gbuffer, zero4, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    shadow_map,
    D3D12_RESOURCE_STATE_GENERIC_READ,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));
  float red1[] = { 1.0f, 0.0f, 0.0f, 0.0f };
  command_list->ClearRenderTargetView(handle_rtv_shadowmap, red1, 0, nullptr);

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_dsv(
    dsv_heap->GetCPUDescriptorHandleForHeapStart(), 0, dsv_descriptor_size);
  command_list->ClearDepthStencilView(handle_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  command_list->SetGraphicsRootSignature(chunk_pass->root_signature);

  ID3D12DescriptorHeap* ppHeaps[] = { cbv_heap };
  command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

  D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f * WIN_W, 1.0f * WIN_H, 0.0f, 1.0f);
  D3D12_RECT scissor = CD3DX12_RECT(0, 0, long(WIN_W), long(WIN_H));
  command_list->RSSetViewports(1, &viewport);
  command_list->RSSetScissorRects(1, &scissor);
  
  CD3DX12_GPU_DESCRIPTOR_HANDLE handle_srv(cbv_heap->GetGPUDescriptorHandleForHeapStart(), 2, cbv_descriptor_size);
  command_list->SetGraphicsRootDescriptorTable(2, handle_srv);
  D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
    handle_rtv, handle_rtv_gbuffer
  };
  command_list->OMSetRenderTargets(2, rtvs, FALSE, &handle_dsv);
  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list->SetGraphicsRootConstantBufferView(1, d_per_scene_cb->GetGPUVirtualAddress());  // Per-scene CB

  const int N = int(chunk_pass->chunk_instances.size());
  for (int i = 0; i < N; i++) {
    Chunk* c = chunk_pass->chunk_instances[i];
    D3D12_GPU_VIRTUAL_ADDRESS cbv0_addr = chunk_pass->d_per_object_cbs->GetGPUVirtualAddress() + 256 * i;
    command_list->SetGraphicsRootConstantBufferView(0, cbv0_addr);  // Per-object CB
    command_list->IASetVertexBuffers(0, 1, &(c->d3d12_vertex_buffer_view));
    command_list->DrawInstanced(chunk->tri_count * 3, 1, 0, 0);
  }

  // Draw Backdrop
  command_list->SetGraphicsRootConstantBufferView(0, d_per_scene_cb->GetGPUVirtualAddress() + sizeof(PerSceneCB));
  command_list->IASetVertexBuffers(0, 1, &(backdrop_vbv));
  command_list->DrawInstanced(6, 1, 0, 0);

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PRESENT)));
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    shadow_map,
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_GENERIC_READ)));

  CE(command_list->Close());
  g_command_queue->ExecuteCommandLists(1,
    (ID3D12CommandList* const*)&command_list);
  CE(g_swapchain->Present(1, 0));
  WaitForPreviousFrame();
}

void DX12ChunksScene::Update(float secs) {
  total_secs += secs;
  DirectX::XMVECTOR D = dir_light->GetDir_D3D11();
  DirectX::XMMATRIX PV = dir_light->GetPV_D3D11();
  DirectX::XMVECTOR pos = camera->GetPos_D3D11();
  UpdatePerSceneCB(&D, &PV, &pos);
  DirectX::XMMATRIX M = DirectX::XMMatrixIdentity();

  const float l = Chunk::size;
  M *= DirectX::XMMatrixTranslation(-l * 0.5f, -l * 0.5f, l * 0.5f);
  DirectX::XMVECTOR rot_axis;
  rot_axis.m128_f32[0] = 0.0f;
  rot_axis.m128_f32[1] = 1.0f;
  rot_axis.m128_f32[2] = 0.0f;
  M *= DirectX::XMMatrixRotationAxis(rot_axis, total_secs * 3.14159f / 2.0f);  // 绕着物体原点转
  M *= DirectX::XMMatrixTranslation(0.0f, -20.0f, 0.0f);  // 绕世界坐标转（？）

  DirectX::XMMATRIX V = camera->GetViewMatrix_D3D11();

  chunk_pass->StartPass();
  chunk->RecordRenderCommand_D3D12(chunk_pass, M, V, projection_matrix);
  M *= DirectX::XMMatrixTranslation(0.0f, 40.0f, 0.0f);
  chunk->RecordRenderCommand_D3D12(chunk_pass, M, V, projection_matrix);
  glm::mat3 orientation = glm::rotate(total_secs * 3.14159f / 2.0f, glm::vec3(0, 1, 0));
  chunk_index->RecordRenderCommand_D3D12(chunk_pass,
    glm::vec3(30, 0, 0), glm::vec3(1, 1, 1), orientation,
    chunk_index->Size() * 0.5f, V, projection_matrix);

  chunk_sprite->pos = glm::vec3(-30, 0, 0);
  chunk_sprite->RotateAroundLocalAxis(glm::vec3(0, 1, 0), secs * 180.0f / 2.0f);
  chunk_sprite->RecordRenderCommand_D3D12(chunk_pass, V, projection_matrix);
  
  chunk_pass->EndPass();
}

void DX12ChunksScene::UpdatePerSceneCB(
  const DirectX::XMVECTOR* dir_light,
  const DirectX::XMMATRIX* lightPV,
  const DirectX::XMVECTOR* camPos) {
  CD3DX12_RANGE read_range(0, sizeof(PerSceneCB));
  char* ptr;
  CE(d_per_scene_cb->Map(0, &read_range, (void**)&ptr));
  h_per_scene_cb.dir_light = *dir_light;
  h_per_scene_cb.lightPV = *lightPV;
  h_per_scene_cb.cam_pos = *camPos;
  memcpy(ptr, &h_per_scene_cb, sizeof(PerSceneCB));
  
  PerObjectCB backdrop_perobject_cb{};
  backdrop_perobject_cb.M = DirectX::XMMatrixIdentity();
  backdrop_perobject_cb.V = camera->GetViewMatrix_D3D11();
  backdrop_perobject_cb.P = projection_matrix;
  memcpy(ptr + sizeof(PerSceneCB), &backdrop_perobject_cb, sizeof(PerObjectCB));
  d_per_scene_cb->Unmap(0, nullptr);
}