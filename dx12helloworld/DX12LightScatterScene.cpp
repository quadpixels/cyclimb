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
  root_parameters[0].InitAsConstantBufferView(0, 0,
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

  CE(g_device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state)));
}

void DX12LightScatterScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, pipeline_state));

  float bg_color[] = { 1.0f, 0.8f, 0.8f, 1.0f };

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));

  D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f * WIN_W, 1.0f * WIN_H, -100.0f, 100.0f);
  D3D12_RECT scissor = CD3DX12_RECT(0, 0, long(WIN_W), long(WIN_H));

  command_list->SetGraphicsRootSignature(root_signature);

  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));
  CE(command_list->Close());
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(g_swapchain->Present(1, 0));
  WaitForPreviousFrame();
}

void DX12LightScatterScene::Update(float secs) {

}