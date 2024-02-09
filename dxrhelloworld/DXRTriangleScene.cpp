#include "scene.hpp"
#include <util.hpp>
#include <d3dx12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>

extern ID3D12Device5* g_device12;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern int g_rtv_descriptor_size;
extern int g_frame_index;
extern ID3D12Resource* g_rendertargets[];
extern ID3D12CommandQueue* g_command_queue;
extern IDXGISwapChain3* g_swapchain;

void WaitForPreviousFrame();

TriangleScene::TriangleScene() : root_sig(nullptr) {
  // Root signature (empty)
  CD3DX12_ROOT_SIGNATURE_DESC root_sig_desc;
  root_sig_desc.Init(0, nullptr, 0, nullptr,
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
  ID3DBlob* signature, * error;
  CE(D3D12SerializeRootSignature(
    &root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
  CE(g_device12->CreateRootSignature(
    0, signature->GetBufferPointer(), signature->GetBufferSize(),
    IID_PPV_ARGS(&root_sig)));

  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));

  // Shader (hello triangle)
  ID3DBlob* vs_blob, * ps_blob;
  UINT compile_flags = 0;
  CE(D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
    "VSMain", "vs_4_0", 0, 0, &vs_blob, &error));
  if (error) printf("Error creating VS: %s\n", (char*)(error->GetBufferPointer()));
  CE(D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
    "PSMain", "ps_4_0", 0, 0, &ps_blob, &error));
  if (error) printf("Error creating PS: %s\n", (char*)(error->GetBufferPointer()));

  // Input layout
  D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
      {
        "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
      },
      {
        "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
      }
  };

  // PSO state
  // Describe and create the graphics pipeline state object (PSO).
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
  psoDesc.pRootSignature = root_sig;
  psoDesc.VS = CD3DX12_SHADER_BYTECODE(vs_blob);
  psoDesc.PS = CD3DX12_SHADER_BYTECODE(ps_blob);
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.DepthStencilState.DepthEnable = FALSE;
  psoDesc.DepthStencilState.StencilEnable = FALSE;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  psoDesc.SampleDesc.Count = 1;
  CE(g_device12->CreateGraphicsPipelineState(
    &psoDesc, IID_PPV_ARGS(&pipeline_state)));

  // Command list
  CE(g_device12->CreateCommandList(
    0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
    pipeline_state, IID_PPV_ARGS(&command_list)));
  CE(command_list->Close());

  // Vertex buffer and VBV
  {
    Vertex triangleVerts[] = {
      {{0.0f, 0.25f, 0.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
      {{0.25f, -0.25f, 0.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
      {{-0.25f, -0.25f, 0.0f}, {1.0f, 0.0f, 1.0f, 1.0f}}
    };
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(triangleVerts))),
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&vb_triangle)));
    UINT8* ptr;
    CD3DX12_RANGE read_range(0, 0);
    CE(vb_triangle->Map(0, &read_range, (void**)(&ptr)));
    memcpy(ptr, triangleVerts, sizeof(triangleVerts));
    vb_triangle->Unmap(0, nullptr);
    vbv_triangle.BufferLocation = vb_triangle->GetGPUVirtualAddress();
    vbv_triangle.StrideInBytes = sizeof(Vertex);
    vbv_triangle.SizeInBytes = sizeof(triangleVerts);
  }
}

void TriangleScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, pipeline_state));
  command_list->SetGraphicsRootSignature(root_sig);

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