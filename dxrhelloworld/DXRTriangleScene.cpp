#include "scene.hpp"
#include <util.hpp>

#include <d3dx12.h>
#include <DirectXMath.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <dxcapi.h>

extern int WIN_W, WIN_H;
extern ID3D12Device5* g_device12;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern int g_rtv_descriptor_size;
extern int g_frame_index;
extern ID3D12Resource* g_rendertargets[];
extern ID3D12CommandQueue* g_command_queue;
extern IDXGISwapChain3* g_swapchain;


void WaitForPreviousFrame();

TriangleScene::TriangleScene() {
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, nullptr, IID_PPV_ARGS(&command_list)));
  CE(command_list->Close());

  // Root Sig
  D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
  rootsig_desc.NumParameters = 0;
  rootsig_desc.pParameters = nullptr;
  rootsig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  rootsig_desc.NumStaticSamplers = 0;

  ID3DBlob* sigblob;
  ID3DBlob* error;
  HRESULT hr;
  hr = D3D12SerializeRootSignature(
    &rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigblob, &error);
  assert(SUCCEEDED(hr));
  if (error) {
    printf("Error creating Raygen local root signature: %s\n", (char*)error->GetBufferPointer());
  }
  CE(g_device12->CreateRootSignature(0, sigblob->GetBufferPointer(), sigblob->GetBufferSize(), IID_PPV_ARGS(&root_sig)));
  sigblob->Release();

  // Shader (hello triangle)
  ID3DBlob* vs_blob, * ps_blob;
  UINT compile_flags = 0;
  CE(D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
    "VSMain", "vs_4_0", 0, 0, &vs_blob, &error));
  if (error) printf("Error creating VS: %s\n", (char*)(error->GetBufferPointer()));
  CE(D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
    "PSMain", "ps_4_0", 0, 0, &ps_blob, &error));
  if (error) printf("Error creating PS: %s\n", (char*)(error->GetBufferPointer()));

  // Input layout for PSO
  D3D12_INPUT_ELEMENT_DESC input_element_desc[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  // PSO
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = root_sig;
  pso_desc.VS.pShaderBytecode = vs_blob->GetBufferPointer();
  pso_desc.VS.BytecodeLength = vs_blob->GetBufferSize();
  pso_desc.PS.pShaderBytecode = ps_blob->GetBufferPointer();
  pso_desc.PS.BytecodeLength = ps_blob->GetBufferSize();
  pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pso_desc.DepthStencilState.DepthEnable = false;
  pso_desc.DepthStencilState.StencilEnable = false;
  pso_desc.InputLayout.NumElements = 2;
  pso_desc.InputLayout.pInputElementDescs = input_element_desc;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.SampleDesc.Count = 1;
  CE(g_device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state)));

  // vertex buffer
  Vertex verts[] = {
    { { 0.0f, 0.25f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
    { { 0.25f, -0.25f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
    { { -0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
  };
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(verts))),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&vb_triangle)));
  UINT8* pData;
  CD3DX12_RANGE readRange(0, 0);
  CE(vb_triangle->Map(0, &readRange, (void**)&pData));
  memcpy(pData, verts, sizeof(verts));
  vb_triangle->Unmap(0, nullptr);

  // VBV
  vbv_triangle.BufferLocation = vb_triangle->GetGPUVirtualAddress();
  vbv_triangle.StrideInBytes = sizeof(Vertex);
  vbv_triangle.SizeInBytes = sizeof(verts);
}

void TriangleScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);
  float bg_color[] = { 1.0f, 1.0f, 0.8f, 1.0f };
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);

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

void TriangleScene::Update(float secs) {
}