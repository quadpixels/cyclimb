#include "scene.hpp"

#include "d3dx12.h"
#include "util.hpp"
#include <Windows.h>
#include <stdio.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

extern int WIN_W, WIN_H;
extern ID3D12Device* g_device12;
extern int g_frame_index;
extern ID3D12CommandQueue* g_command_queue;
extern IDXGISwapChain3* g_swapchain;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern ID3D12Resource* g_rendertargets[];
extern unsigned g_rtv_descriptor_size;

// Root Parameter的布局方式
const bool use_descriptor_table = true;

struct PerTriangleCB {
  DirectX::XMFLOAT2 pos;
};

void WaitForPreviousFrame();

DX12HelloTriangleScene::DX12HelloTriangleScene() {
  InitPipelineAndCommandList();
  InitResources();
}

void DX12HelloTriangleScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, pipeline_state));

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);
  float bg_color[] = { 0.8f, 1.0f, 0.8f, 1.0f };
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));

  D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f*WIN_W, 1.0f*WIN_H, -100.0f, 100.0f);
  D3D12_RECT scissor = CD3DX12_RECT(0, 0, long(WIN_W), long(WIN_H));

  command_list->SetGraphicsRootSignature(root_signature);

  ID3D12DescriptorHeap* ppHeaps[] = { cbv_heap };
  command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

  CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(cbv_heap->GetGPUDescriptorHandleForHeapStart(), 0, cbv_descriptor_size);

  // 如果使用Descriptor Table：就把Heap丢给Descriptor Table
  // 如果使用CBV：就把单个CBV丢给CBV
  if (use_descriptor_table) {
    command_list->SetGraphicsRootDescriptorTable(0, cbv_heap->GetGPUDescriptorHandleForHeapStart());
  }
  else {
    command_list->SetGraphicsRootConstantBufferView(0, cbvs->GetGPUVirtualAddress());
  }

  command_list->RSSetViewports(1, &viewport);
  command_list->RSSetScissorRects(1, &scissor);
  command_list->OMSetRenderTargets(1, &handle_rtv, FALSE, nullptr);
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
  command_list->DrawInstanced(3, 1, 0, 0);

  if (use_descriptor_table) {
    cbvHandle.Offset(cbv_descriptor_size);
    command_list->SetGraphicsRootDescriptorTable(0, cbvHandle);
  }
  else {
    command_list->SetGraphicsRootConstantBufferView(0, cbvs->GetGPUVirtualAddress() + 256);
  }
  command_list->DrawInstanced(3, 1, 0, 0);

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

void DX12HelloTriangleScene::Update(float secs) {
  CD3DX12_RANGE read_range(0, 0);
  char* ptr;
  CE(cbvs->Map(0, &read_range, (void**)&ptr));
  PerTriangleCB cbs[2];
  cbs[0].pos.x = -0.25;
  cbs[0].pos.y = 0;
  cbs[1].pos.x = 0.25;
  cbs[1].pos.y = 0;
  memcpy(ptr, &cbs[0], sizeof(cbs));
  memcpy(ptr + 256, &cbs[1], sizeof(cbs));
  cbvs->Unmap(0, nullptr);
}

void DX12HelloTriangleScene::InitPipelineAndCommandList() {
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
    command_allocator, nullptr, IID_PPV_ARGS(&command_list)));
  CE(command_list->Close());

  {
    ID3DBlob* error = nullptr;
    unsigned compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
      "VSMain", "vs_5_0", compile_flags, 0, &VS, &error);
    if (error) printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));

    D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
      "PSMain", "ps_5_0", compile_flags, 0, &PS, &error);
    if (error) printf("Error compiling PS: %s\n", (char*)(error->GetBufferPointer()));
  }

  {
    CD3DX12_ROOT_PARAMETER1 rootParameters[1];
    if (use_descriptor_table) {

      /*inline void InitAsDescriptorTable(
        UINT numDescriptorRanges,
        _In_reads_(numDescriptorRanges) const D3D12_DESCRIPTOR_RANGE1 * pDescriptorRanges,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
      {
        InitAsDescriptorTable(*this, numDescriptorRanges, pDescriptorRanges, visibility);
      }*/
      D3D12_DESCRIPTOR_RANGE1 dr{};
      dr.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
      dr.NumDescriptors = 1;
      dr.BaseShaderRegister = 0;  // 对应 "register(b0)"
      dr.RegisterSpace = 0;
      dr.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
      dr.OffsetInDescriptorsFromTableStart = 0;
      rootParameters[0].InitAsDescriptorTable(1, &dr, D3D12_SHADER_VISIBILITY_VERTEX);
    }
    else {
      rootParameters[0].InitAsConstantBufferView(0, 0,
        D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
    }

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc;
    root_sig_desc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL,
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
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {
    .pRootSignature = root_signature,
    .VS = CD3DX12_SHADER_BYTECODE(VS),
    .PS = CD3DX12_SHADER_BYTECODE(PS),
    .BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
    .SampleMask = UINT_MAX,
    .RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
    .DepthStencilState = {
      .DepthEnable = false,
      .StencilEnable = false,
    },
    .InputLayout = {
      input_element_desc,
      2
    },
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .NumRenderTargets = 1,
    .RTVFormats = {
      DXGI_FORMAT_R8G8B8A8_UNORM,
    },
    .DSVFormat = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {
      .Count = 1,
    },
  };

  CE(g_device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state)));
}

void DX12HelloTriangleScene::InitResources() {
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
    IID_PPV_ARGS(&vertex_buffer)));
  UINT8* pData;
  CD3DX12_RANGE readRange(0, 0);
  CE(vertex_buffer->Map(0, &readRange, (void**)&pData));
  memcpy(pData, verts, sizeof(verts));
  vertex_buffer->Unmap(0, nullptr);

  vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vertex_buffer_view.StrideInBytes = sizeof(Vertex);
  vertex_buffer_view.SizeInBytes = sizeof(verts);

  // CBV's heap
  D3D12_DESCRIPTOR_HEAP_DESC cbv_heap_desc = {};
  cbv_heap_desc.NumDescriptors = 2;  // Just 1 constant buffer
  cbv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  cbv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CE(g_device12->CreateDescriptorHeap(&cbv_heap_desc, IID_PPV_ARGS(&cbv_heap)));
  cbv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // CBV's resource
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(512)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&cbvs)));

  // CBV's resource view
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
  cbv_desc.BufferLocation = cbvs->GetGPUVirtualAddress();
  cbv_desc.SizeInBytes = 256;

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle1(cbv_heap->GetCPUDescriptorHandleForHeapStart(),
    0, cbv_descriptor_size);
  g_device12->CreateConstantBufferView(&cbv_desc, handle1);

  handle1.Offset(cbv_descriptor_size);
  cbv_desc.BufferLocation += 256;
  g_device12->CreateConstantBufferView(&cbv_desc, handle1);
}