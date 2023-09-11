#include "scene.hpp"

#include "d3dx12.h"
#include "util.hpp"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

void WaitForPreviousFrame();
extern ID3D12Device* g_device12;

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

struct PerObjectCB {
  DirectX::XMMATRIX M, V, P;
};

struct PerSceneCB {
  DirectX::XMVECTOR dir_light;
  DirectX::XMMATRIX lightPV;
  DirectX::XMVECTOR cam_pos;
};

DX12ChunksScene::DX12ChunksScene() {
  InitPipelineAndCommandList();
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
    D3DCompileFromFile(L"shaders/default_palette.hlsl", nullptr, nullptr,
      "VSMain", "vs_5_0", compile_flags, 0, &default_palette_VS, &error);
    if (error) printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));

    D3DCompileFromFile(L"shaders/default_palette.hlsl", nullptr, nullptr,
      "PSMain", "ps_5_0", compile_flags, 0, &default_palette_PS, &error);
    if (error) printf("Error compiling PS: %s\n", (char*)(error->GetBufferPointer()));
  }

  // Root signature
  {
    CD3DX12_DESCRIPTOR_RANGE1 ranges[1];

    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[0].InitAsConstantBufferView(0, 0,
      D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParameters[1].InitAsConstantBufferView(1, 0,
      D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    
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
    .DepthStencilState = {
      .DepthEnable = false,
      .StencilEnable = false,
    },
    .InputLayout = {
      input_element_desc,
      4
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

void DX12ChunksScene::InitResources() {
}

void DX12ChunksScene::Render() {
}

void DX12ChunksScene::Update(float secs) {
}

