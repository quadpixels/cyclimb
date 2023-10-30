#include "scene.hpp"

#include "d3dx12.h"
#include "util.hpp"

#include <wrl/client.h>
#include <d3dcompiler.h>

extern ID3D12Device* g_device12;
using Microsoft::WRL::ComPtr;

extern ID3D12Resource* g_rendertargets[];
extern int g_frame_index;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern unsigned g_rtv_descriptor_size;
extern ID3D12CommandQueue* g_command_queue;
extern IDXGISwapChain3* g_swapchain;

void WaitForPreviousFrame();

DX12TextScene::DX12TextScene() {
  InitCommandList();
  InitResources();
  InitFreetype();
  CreateChar();
}

void DX12TextScene::InitCommandList() {
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
    command_allocator, nullptr, IID_PPV_ARGS(&command_list)));
  CE(command_list->Close());
}

void DX12TextScene::InitResources() {
  // 0.5. SRV heap
  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
      .NumDescriptors = 1024,  // Maximum 1024 distinct chars. Hopefully we only use this many
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };
    CE(g_device12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srv_heap)));
  }
   
  // 1. Shader
  {
    ID3DBlob* error = nullptr;
    unsigned compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    D3DCompileFromFile(L"shaders/textrender.hlsl", nullptr, nullptr,
      "VSMain", "vs_5_0", compile_flags, 0, &VS, &error);
    if (error) printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));

    D3DCompileFromFile(L"shaders/textrender.hlsl", nullptr, nullptr,
      "PSMain", "ps_5_0", compile_flags, 0, &PS, &error);
    if (error) printf("Error compiling PS: %s\n", (char*)(error->GetBufferPointer()));
  }

  // 2. Root Signature
  CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
  ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

  CD3DX12_ROOT_PARAMETER1 rootParameters[2];
  rootParameters[0].InitAsConstantBufferView(0, 0, // per-scene CB，包含TextColor
    D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);
  rootParameters[1].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);

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
    signature->GetBufferSize(), IID_PPV_ARGS(&root_signature_text_render)));
  root_signature_text_render->SetName(L"Text Render Root Signature");

  // 3. PSO
  D3D12_INPUT_ELEMENT_DESC input_element_desc[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
  };
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = root_signature_text_render;
  pso_desc.VS = CD3DX12_SHADER_BYTECODE(VS);
  pso_desc.PS = CD3DX12_SHADER_BYTECODE(PS);
  pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
  pso_desc.SampleMask = UINT_MAX,
  pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
  pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT),
  pso_desc.InputLayout.pInputElementDescs = input_element_desc;
  pso_desc.InputLayout.NumElements = 2;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 2;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
  pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pso_desc.SampleDesc.Count = 1;
  CE(g_device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state_text_render)));
}

void DX12TextScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, pipeline_state_text_render));
  command_list->SetGraphicsRootSignature(root_signature_text_render);

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));
  float bg_color[] = { 0.8f, 0.8f, 1.0f, 1.0f };
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PRESENT)));
  CE(command_list->Close());
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(g_swapchain->Present(1, 0));
  WaitForPreviousFrame();
}

void DX12TextScene::Update(float secs) {
}

void DX12TextScene::SetText(const std::wstring& txt) {

}

void DX12TextScene::InitFreetype() {
  FT_Library ft;
  if (FT_Init_FreeType(&ft)) {
    printf("Error: cannot init FreeType library\n");
  }
  const char* ttfs[] = {
    "/"
    "/usr/share/fonts/truetype/arphic/uming.ttc",
    "C:\\Windows\\Fonts\\simsun.ttc",
  };
  for (int i = 0; i < 2; i++) {
    if (FT_New_Face(ft,
      ttfs[i],
      0, &face)) {
      printf("Font file #%d=%s: cannot load\n", i, ttfs[i]);
    }
    else {
      printf("Font file #%d=%s: load complete\n", i, ttfs[i]);
      break;
    }
  }
  FT_Set_Pixel_Sizes(face, 0, 20);
}

void DX12TextScene::CreateChar() {
  wchar_t ch = L'A';
  if (FT_Load_Char(face, ch, FT_LOAD_RENDER)) {
    printf("Oh! Could not load character for rendering.\n");
  }

  if (face->glyph->bitmap.buffer) {
    const int W = face->glyph->bitmap.width;
    const int H = face->glyph->bitmap.rows;

    ID3D12Resource* rsrc;
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8_UNORM, W, H, 1, 0, 1, 0,
        D3D12_RESOURCE_FLAG_NONE)),
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
      nullptr,
      IID_PPV_ARGS(&rsrc)));
    CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(srv_heap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_R8_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    g_device12->CreateShaderResourceView(rsrc, &srv_desc, srv_handle);

    Character_D3D12 ch12;
    ch12.texture = rsrc;
    ch12.size = glm::ivec2(W, H);
    ch12.bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top);
    ch12.advance = face->glyph->advance.x;

    characters_d3d12[ch] = ch12;
    printf("Created ch.\n");
  }
}