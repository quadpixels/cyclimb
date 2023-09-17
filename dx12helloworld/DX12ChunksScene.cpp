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
  InitPipelineAndCommandList();
  InitResources();
  total_secs = 0.0f;
}

void DX12ChunksScene::InitPipelineAndCommandList() {
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
    command_allocator, nullptr, IID_PPV_ARGS(&command_list)));
  CE(command_list->Close());

  {
    ID3DBlob* error = nullptr;
    unsigned compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    D3DCompileFromFile(L"../shaders_hlsl/default_palette.hlsl", nullptr, nullptr,
      "VSMain", "vs_5_0", compile_flags, 0, &default_palette_VS, &error);
    if (error) printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));

    D3DCompileFromFile(L"../shaders_hlsl/default_palette.hlsl", nullptr, nullptr,
      "PSMainWithShadow", "ps_5_0", compile_flags, 0, &default_palette_PS, &error);
    if (error) printf("Error compiling PS: %s\n", (char*)(error->GetBufferPointer()));
  }

  // Root signature
  {
    CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

    CD3DX12_ROOT_PARAMETER1 rootParameters[3];
    rootParameters[0].InitAsConstantBufferView(0, 0,  // per-scene CB
      D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParameters[1].InitAsConstantBufferView(1, 0,  // per-object CB
      D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[2].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
    
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 4;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc;
    root_sig_desc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> signature, error;

    HRESULT hr = D3DX12SerializeVersionedRootSignature(&root_sig_desc,
      D3D_ROOT_SIGNATURE_VERSION_1_1,
      &signature, &error);
    if (signature == nullptr) {
      printf("Could not serialize root signature: %s\n",
        (char*)(error->GetBufferPointer()));
    }

    CE(g_device12->CreateRootSignature(0, signature->GetBufferPointer(),
      signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
    root_signature->SetName(L"Root signature");
  }

  D3D12_INPUT_ELEMENT_DESC input_element_desc[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 1, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 2, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {
    .pRootSignature = root_signature,
    .VS = CD3DX12_SHADER_BYTECODE(default_palette_VS),
    .PS = CD3DX12_SHADER_BYTECODE(default_palette_PS),
    .BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
    .SampleMask = UINT_MAX,
    .RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
    .DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT),
    .InputLayout = {
      input_element_desc,
      4
    },
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .NumRenderTargets = 2,
    .RTVFormats = {
      DXGI_FORMAT_R8G8B8A8_UNORM,
      DXGI_FORMAT_R32G32B32A32_FLOAT,
    },
    .DSVFormat = DXGI_FORMAT_D32_FLOAT,
    .SampleDesc = {
      .Count = 1,
    },
  };
  CE(g_device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state)));
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

  // Constant buffer
  chunk_pass = new ChunkPass();
  chunk_pass->AllocateConstantBuffers(1);

  // CBV descriptor heap
  D3D12_DESCRIPTOR_HEAP_DESC cbv_heap_desc{};
  cbv_heap_desc.NumDescriptors = 2;
  cbv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  cbv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CE(g_device12->CreateDescriptorHeap(&cbv_heap_desc, IID_PPV_ARGS(&cbv_heap)));
  cbv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // Per Scene CB view
  D3D12_CONSTANT_BUFFER_VIEW_DESC per_scene_cbv_desc{};
  per_scene_cbv_desc.BufferLocation = chunk_pass->cbs->GetGPUVirtualAddress();
  per_scene_cbv_desc.SizeInBytes = 256;   // Must be a multiple of 256

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle1(cbv_heap->GetCPUDescriptorHandleForHeapStart());
  g_device12->CreateConstantBufferView(&per_scene_cbv_desc, handle1);

  // Per Object CB view
  D3D12_CONSTANT_BUFFER_VIEW_DESC per_obj_cbv_desc{};
  per_obj_cbv_desc.BufferLocation = per_scene_cbv_desc.BufferLocation + 256;
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
}

void DX12ChunksScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, pipeline_state));

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);
  float bg_color[] = { 0.8f, 0.8f, 0.8f, 1.0f };
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));

  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_dsv(
    dsv_heap->GetCPUDescriptorHandleForHeapStart(), 0, dsv_descriptor_size);
  command_list->ClearDepthStencilView(handle_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  command_list->SetGraphicsRootSignature(root_signature);

  ID3D12DescriptorHeap* ppHeaps[] = { cbv_heap };
  command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

  D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f * WIN_W, 1.0f * WIN_H, 0.0f, 1.0f);
  D3D12_RECT scissor = CD3DX12_RECT(0, 0, long(WIN_W), long(WIN_H));
  command_list->RSSetViewports(1, &viewport);
  command_list->RSSetScissorRects(1, &scissor);
  
  command_list->OMSetRenderTargets(1, &handle_rtv, FALSE, &handle_dsv);
  command_list->SetGraphicsRootConstantBufferView(0, chunk_pass->cbs->GetGPUVirtualAddress());  // Per-scene CB
  command_list->SetGraphicsRootConstantBufferView(1, chunk_pass->cbs->GetGPUVirtualAddress() + 256);  // Per-object CB
  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list->IASetVertexBuffers(0, 1, &(chunk->d3d12_vertex_buffer_view));
  command_list->DrawInstanced(chunk->tri_count * 3, 1, 0, 0);

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PRESENT)));

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
  M *= DirectX::XMMatrixRotationAxis(rot_axis, total_secs * 3.14159f / 2.0f);

  DirectX::XMMATRIX V = camera->GetViewMatrix_D3D11();
  UpdatePerObjectCB(&M, &V, &projection_matrix);
}

void DX12ChunksScene::UpdatePerSceneCB(
  const DirectX::XMVECTOR* dir_light,
  const DirectX::XMMATRIX* lightPV,
  const DirectX::XMVECTOR* camPos) {
  CD3DX12_RANGE read_range(0, sizeof(PerSceneCB));
  char* ptr;
  CE(chunk_pass->cbs->Map(0, &read_range, (void**)&ptr));
  per_scene_cb.dir_light = *dir_light;
  per_scene_cb.lightPV = *lightPV;
  per_scene_cb.cam_pos = *camPos;
  memcpy(ptr, &per_scene_cb, sizeof(PerSceneCB));
  chunk_pass->cbs->Unmap(0, nullptr);
}

void DX12ChunksScene::UpdatePerObjectCB(
  const DirectX::XMMATRIX* M,
  const DirectX::XMMATRIX* V,
  const DirectX::XMMATRIX* P) {
  CD3DX12_RANGE read_range(sizeof(PerSceneCB), sizeof(PerObjectCB));
  char* ptr;
  CE(chunk_pass->cbs->Map(0, &read_range, (void**)&ptr));
  per_object_cb.M = *M;
  per_object_cb.V = *V;
  per_object_cb.P = *P;
  memcpy(ptr, &per_object_cb, sizeof(PerObjectCB));
  chunk_pass->cbs->Unmap(0, nullptr);
}