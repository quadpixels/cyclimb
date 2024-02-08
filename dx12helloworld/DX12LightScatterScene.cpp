#include "scene.hpp"

#include <assert.h>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "util.hpp"

using Microsoft::WRL::ComPtr;

extern int WIN_W, WIN_H;
extern ID3D12Device* g_device12;
extern int g_frame_index;
extern ID3D12CommandQueue* g_command_queue;
extern IDXGISwapChain3* g_swapchain;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern ID3D12Resource* g_rendertargets[];
extern unsigned g_rtv_descriptor_size;

void WaitForPreviousFrame();

DX12LightScatterScene::DX12LightScatterScene() {
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
    command_allocator, nullptr, IID_PPV_ARGS(&command_list)));
  CE(command_list->Close());

  ID3DBlob* error = nullptr;
  unsigned compile_flags = 0;
  D3DCompileFromFile(L"shaders/shaders_drawlight.hlsl", nullptr, nullptr,
    "VSMain", "vs_4_0", compile_flags, 0, &vs_drawlight, &error);
  if (error) {
    printf("Error compiling VS: %s\n", error->GetBufferPointer());
  }
  D3DCompileFromFile(L"shaders/shaders_drawlight.hlsl", nullptr, nullptr,
    "PSMain", "ps_4_0", compile_flags, 0, &ps_drawlight, &error);
  if (error) {
    printf("Error compiling PS: %s\n", error->GetBufferPointer());
  }

  // Root signature
  CD3DX12_ROOT_PARAMETER1 root_parameters[1];
  root_parameters[0].InitAsConstantBufferView(0, 0,  // PerSceneCB at b0
    D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);
  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc;
  root_sig_desc.Init_1_1(_countof(root_parameters), root_parameters, 0,
    NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
  ComPtr<ID3DBlob> signature;
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

  D3D12_INPUT_ELEMENT_DESC input_element_desc[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
  };
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {
    .pRootSignature = root_signature,
    .VS = CD3DX12_SHADER_BYTECODE(vs_drawlight),
    .PS = CD3DX12_SHADER_BYTECODE(ps_drawlight),
    .BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
    .SampleMask = UINT_MAX,
    .RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
    .DepthStencilState = {
      .DepthEnable = false,
      .StencilEnable = false,
    },
    .InputLayout = {
      input_element_desc, 2
    },
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .NumRenderTargets = 1,
    .RTVFormats = {
      DXGI_FORMAT_R8G8B8A8_UNORM,
    },
    .DSVFormat = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {
      .Count = 1,
    }
  };

  CE(g_device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state_drawlight)));

  // RTV heap and SRV heap
  D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
  srv_heap_desc.NumDescriptors = 2;
  srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CE(g_device12->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_heap)));
  srv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  rtv_heap_desc.NumDescriptors = 1;
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  CE(g_device12->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)));
  rtv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  // Lightmask
  D3D12_RESOURCE_DESC lightmask_desc{};
  lightmask_desc.MipLevels = 1;
  lightmask_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  lightmask_desc.Width = WIN_W;
  lightmask_desc.Height = WIN_H;
  lightmask_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  lightmask_desc.DepthOrArraySize = 1;
  lightmask_desc.SampleDesc.Count = 1;
  lightmask_desc.SampleDesc.Quality = 0;
  lightmask_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  float zero4[] = { 0, 0, 0, 0 };
  CD3DX12_CLEAR_VALUE clear_value(DXGI_FORMAT_R8G8B8A8_UNORM, zero4);
  
  hr = g_device12->CreateCommittedResource(
    &(keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT))),
    D3D12_HEAP_FLAG_NONE,
    &lightmask_desc,
    D3D12_RESOURCE_STATE_COPY_SOURCE,
    &clear_value,
    IID_PPV_ARGS(&lightmask));
  assert(SUCCEEDED(hr));
  
  // Lightmask's SRV
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Texture2D.MipLevels = 1;
  CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(srv_heap->GetCPUDescriptorHandleForHeapStart());
  g_device12->CreateShaderResourceView(lightmask, &srv_desc, srv_handle);

  // Lightmask's RTV
  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart());
  g_device12->CreateRenderTargetView(lightmask, nullptr, rtv_handle);

  // Vertex Buffer
  VertexUV verts[] = {
    {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f }, //  
    {  1.0f, -1.0f, 0.0f,  1.0f, 1.0f }, //  +-----------+ UV = (1, 0), NDC=(1, 1)
    { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f }, //  |           |
                                         //  |           |
    { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f }, //  |           |
    { -1.0f,  1.0f, 0.0f,  0.0f, 0.0f }, //  |           |
    {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f }, //  +-----------+ UV = (1, 1), NDC=(1,-1)

    {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f }, //  
    {  1.0f, -1.0f, 0.0f,  1.0f, 1.0f }, //  +-----------+ UV = (1, 0), NDC=(1, 1)
    { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f }, //  |           |
                                         //  |           |
    { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f }, //  |           |
    { -1.0f,  1.0f, 0.0f,  0.0f, 0.0f }, //  |           |
    {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f }, //  +-----------+ UV = (1, 1), NDC=(1,-1)
  };
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(verts))),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr, IID_PPV_ARGS(&vb_fsquad)));
  UINT8* ptr;
  CD3DX12_RANGE read_range(0, 0);
  CE(vb_fsquad->Map(0, &read_range, (void**)(&ptr)));
  memcpy(ptr, verts, sizeof(verts));
  vb_fsquad->Unmap(0, nullptr);
  // Vertex Buffer's VBV
  vbv_fsquad.BufferLocation = vb_fsquad->GetGPUVirtualAddress();
  vbv_fsquad.StrideInBytes = sizeof(VertexUV);
  vbv_fsquad.SizeInBytes = sizeof(verts);

  // PerScene CB
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(256)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&cb_drawlight)));
  // PerScene CB's CBV
  srv_handle.Offset(srv_descriptor_size);
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
  cbv_desc.BufferLocation = cb_drawlight->GetGPUVirtualAddress();
  cbv_desc.SizeInBytes = 256;  // must be a multiple of 256
  g_device12->CreateConstantBufferView(&cbv_desc, srv_handle);

  //
  elapsed_secs = 0;
}

void DX12LightScatterScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, pipeline_state_drawlight));
  command_list->SetGraphicsRootSignature(root_signature);

  float bg_color[] = { 1.0f, 0.8f, 0.8f, 1.0f };

  CD3DX12_CPU_DESCRIPTOR_HANDLE swapchain_handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));

  D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f * WIN_W, 1.0f * WIN_H, -100.0f, 100.0f);
  D3D12_RECT scissor = CD3DX12_RECT(0, 0, long(WIN_W), long(WIN_H));
  command_list->ClearRenderTargetView(swapchain_handle_rtv, bg_color, 0, nullptr);

  CD3DX12_CPU_DESCRIPTOR_HANDLE lightmask_handle_rtv(
    rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    0, rtv_descriptor_size);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    lightmask,
    D3D12_RESOURCE_STATE_COPY_SOURCE,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));
  float zero4[] = { 0, 0, 0, 0 };
  command_list->ClearRenderTargetView(lightmask_handle_rtv, zero4, 0, nullptr);
  command_list->OMSetRenderTargets(1, &lightmask_handle_rtv, false, nullptr);
  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list->IASetVertexBuffers(0, 1, &vbv_fsquad);
  command_list->SetGraphicsRootConstantBufferView(0, cb_drawlight->GetGPUVirtualAddress());
  command_list->RSSetViewports(1, &viewport);
  CD3DX12_RECT scissorrect(0, 0, WIN_W, WIN_H);
  command_list->RSSetScissorRects(1, &scissorrect);
  command_list->DrawInstanced(6, 1, 0, 0);

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_COPY_DEST)));

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    lightmask,
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_COPY_SOURCE)));

  command_list->CopyResource(g_rendertargets[g_frame_index], lightmask);

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_COPY_DEST,
    D3D12_RESOURCE_STATE_PRESENT)));

  CE(command_list->Close());
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(g_swapchain->Present(1, 0));
  WaitForPreviousFrame();
}

void DX12LightScatterScene::Update(float secs) {
  elapsed_secs += secs;
  h_cb_drawlight.light_x = WIN_W * 0.5f + 120 * cos(elapsed_secs * 3.14159);
  h_cb_drawlight.light_y = WIN_H * 0.5f + 120 * sin(elapsed_secs * 3.14159);
  h_cb_drawlight.light_r = 100.0f;
  h_cb_drawlight.WIN_H = WIN_H * 1.0f;
  h_cb_drawlight.WIN_W = WIN_W * 1.0f;
  h_cb_drawlight.light_color.m128_f32[0] = 1.0f;
  h_cb_drawlight.light_color.m128_f32[1] = 1.0f;
  h_cb_drawlight.light_color.m128_f32[2] = 1.0f;
  h_cb_drawlight.light_color.m128_f32[3] = 1.0f;
  h_cb_drawlight.global_alpha = 1.0f;

  UINT8* ptr;
  CD3DX12_RANGE read_range(0, 0);
  cb_drawlight->Map(0, &read_range, (void**)&ptr);
  memcpy(ptr, &h_cb_drawlight, sizeof(h_cb_drawlight));
  cb_drawlight->Unmap(0, nullptr);
}