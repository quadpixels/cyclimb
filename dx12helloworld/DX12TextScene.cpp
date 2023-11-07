#include <chrono>
#include <iomanip>

#include "scene.hpp"

#include "d3dx12.h"
#include "util.hpp"

#include <wrl/client.h>
#include <d3dcompiler.h>

extern ID3D12Device* g_device12;
using Microsoft::WRL::ComPtr;

extern int WIN_W, WIN_H;
extern ID3D12Resource* g_rendertargets[];
extern int g_frame_index;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern unsigned g_rtv_descriptor_size;
extern ID3D12CommandQueue* g_command_queue;
extern IDXGISwapChain3* g_swapchain;
extern void GlmMat4ToDirectXMatrix(DirectX::XMMATRIX* out, const glm::mat4& m);

// From textrender.cpp
struct TextCbPerScene {
  DirectX::XMVECTOR screensize; // Assume alignment at float4 boundary
  DirectX::XMMATRIX transform;
  DirectX::XMMATRIX projection;
  DirectX::XMVECTOR textcolor;
};

void WaitForPreviousFrame();

DX12TextScene::DX12TextScene() {
  InitCommandList();
  InitResources();
  InitFreetype();
  AddText(L"Hello world", glm::vec2(WIN_W / 2.0f, WIN_H / 2.0f));
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
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
    };
    CE(g_device12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srv_heap)));
    CE(g_device12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&cbv_heap)));
    srv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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

  D3D12_BLEND_DESC blend_desc{};
  blend_desc.AlphaToCoverageEnable = false;
  blend_desc.IndependentBlendEnable = false;
  blend_desc.RenderTarget[0].BlendEnable = true;
  blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = root_signature_text_render;
  pso_desc.VS = CD3DX12_SHADER_BYTECODE(VS);
  pso_desc.PS = CD3DX12_SHADER_BYTECODE(PS);
  pso_desc.BlendState = blend_desc,
  pso_desc.SampleMask = UINT_MAX,
  pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
  pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT),
  pso_desc.InputLayout.pInputElementDescs = input_element_desc;
  pso_desc.InputLayout.NumElements = 2;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 2;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
  pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
  pso_desc.SampleDesc.Count = 1;
  CE(g_device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state_text_render)));

  // 4. Per-Scene CB for text rendering
  // Create one CB
  ID3D12Resource* cb;
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(256)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&cb)));
  constant_buffers.push_back(cb);

  TextCbPerScene tcps{};
  glm::mat4 transform(1);
  tcps.screensize.m128_f32[0] = WIN_W;
  tcps.screensize.m128_f32[1] = WIN_H;
  GlmMat4ToDirectXMatrix(&tcps.transform, transform);
  glm::mat4 proj = glm::perspective(60.0f * 3.14159f / 180.0f, WIN_W * 1.0f / WIN_H, 0.1f, 499.0f);
  GlmMat4ToDirectXMatrix(&tcps.projection, proj);
  tcps.textcolor.m128_f32[0] = 1.0f;
  tcps.textcolor.m128_f32[1] = 1.0f;
  tcps.textcolor.m128_f32[2] = 0.1f;
  {
    CD3DX12_RANGE readRange(0, 0);
    UINT8* pData;
    CE(cb->Map(0, &readRange, (void**)&pData));
    memcpy(pData, &tcps, sizeof(tcps));
    cb->Unmap(0, nullptr);
  }

  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
  cbv_desc.BufferLocation = cb->GetGPUVirtualAddress();
  cbv_desc.SizeInBytes = 256;

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle1(cbv_heap->GetCPUDescriptorHandleForHeapStart());
  g_device12->CreateConstantBufferView(&cbv_desc, handle1);

  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(float) * 24 * 1024)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&vertex_buffers)));
}

void DX12TextScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, pipeline_state_text_render));
  command_list->SetGraphicsRootSignature(root_signature_text_render);

  ID3D12DescriptorHeap* ppHeaps[] = { srv_heap };
  command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));
  float bg_color[] = { 0.8f, 0.8f, 1.0f, 1.0f };
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  command_list->OMSetRenderTargets(1, &handle_rtv, FALSE, nullptr);
  float blend_factor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
  command_list->OMSetBlendFactor(blend_factor);
  
  D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f * WIN_W, 1.0f * WIN_H, -100.0f, 100.0f);
  D3D12_RECT scissor = CD3DX12_RECT(0, 0, long(WIN_W), long(WIN_H));
  command_list->RSSetViewports(1, &viewport);
  command_list->RSSetScissorRects(1, &scissor);

  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  for (size_t i = 0; i < characters_to_display.size(); i++) {
    const CharacterToDisplay& ctd = characters_to_display[i];
    command_list->IASetVertexBuffers(0, 1, &ctd.vbv);
    command_list->SetGraphicsRootConstantBufferView(0, constant_buffers[0]->GetGPUVirtualAddress());
    CD3DX12_GPU_DESCRIPTOR_HANDLE srv_handle(
      srv_heap->GetGPUDescriptorHandleForHeapStart(),
      ctd.character->offset_in_srv_heap, srv_descriptor_size);
    command_list->SetGraphicsRootDescriptorTable(1, srv_handle);
    command_list->DrawInstanced(6, 1, 0, 0);
  }

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
  auto t = std::chrono::system_clock::now();
  std::time_t t1 = std::chrono::system_clock::to_time_t(t);
  std::wstringstream wss;
  std::tm tm;
  ::localtime_s(&tm, &t1);
  wss << std::put_time(&tm, L"%F %T");
  AddText(wss.str(), glm::vec2(WIN_W / 2.0f, WIN_H / 2.0f));
}

void DX12TextScene::AddText(const std::wstring& txt, glm::vec2 pos) {
  if (txt == text_to_display && pos == text_pos) return;
  ClearCharactersToDisplay();
  float scale = 1.0f;
  float x = pos.x;
  float y = pos.y;
  for (int i = 0; i < txt.size(); i++) {
    wchar_t ch = txt[i];
    Character_D3D12* ch12 = CreateOrGetChar(ch);
    if (ch12 == nullptr) {  // Space
      x += 8 * scale;
      continue;
    }

    float xpos = x + ch12->bearing.x * scale;
    float ypos = y - ch12->bearing.y * scale;
    float w = ch12->size.x * scale;
    float h = ch12->size.y * scale;
    x = xpos + w;

    float vertices[6][4] = {
        { xpos,     ypos + h,   0.0, 1.0 }, //  +-------> +X
        { xpos,     ypos,       0.0, 0.0 }, //  |
        { xpos + w, ypos,       1.0, 0.0 }, //  |
        { xpos,     ypos + h,   0.0, 1.0 }, //  V
        { xpos + w, ypos,       1.0, 0.0 }, //  
        { xpos + w, ypos + h,   1.0, 1.0 }, //  +Y
    };

    const int size = sizeof(vertices);
    const int offset = characters_to_display.size() * size;
    UINT8* pData;
    CD3DX12_RANGE readRange(0, 0);
    CE(vertex_buffers->Map(0, &readRange, (void**)&pData));
    memcpy(pData + offset, vertices, size);
    CD3DX12_RANGE writeRange(offset, offset + size);
    vertex_buffers->Unmap(0, &writeRange);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = vertex_buffers->GetGPUVirtualAddress() + offset;
    vbv.StrideInBytes = sizeof(float) * 4;
    vbv.SizeInBytes = sizeof(vertices);

    CharacterToDisplay ctd{};
    ctd.character = ch12;
    ctd.vbv = vbv;
    characters_to_display.push_back(ctd);
  }
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

DX12TextScene::Character_D3D12* DX12TextScene::CreateOrGetChar(wchar_t ch) {
  if (characters_d3d12.count(ch) > 0) {
    return &(characters_d3d12.at(ch));
  }

  if (FT_Load_Char(face, ch, FT_LOAD_RENDER)) {
    printf("Oh! Could not load character for rendering.\n");
  }

  if (face->glyph->bitmap.buffer) {
    const int W = face->glyph->bitmap.width;
    const int H = face->glyph->bitmap.rows;

    // https://www.braynzarsoft.net/viewtutorial/q16390-directx-12-textures-from-file
    ID3D12Resource* rsrc, * intermediate;
    D3D12_RESOURCE_DESC tex_desc = CD3DX12_RESOURCE_DESC::Tex2D(
      DXGI_FORMAT_R8_UNORM, W, H, 1, 0, 1, 0,
      D3D12_RESOURCE_FLAG_NONE);
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &tex_desc,
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS(&rsrc)));
    uint64_t tex_upload_buffer_size;
    g_device12->GetCopyableFootprints(&tex_desc, 0, 1, 0, nullptr, nullptr, nullptr, &tex_upload_buffer_size);
    printf("Tex upload buffer size: %llu\n", tex_upload_buffer_size);

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)), // upload heap
      D3D12_HEAP_FLAG_NONE, // no flags
      &keep(CD3DX12_RESOURCE_DESC::Buffer(tex_upload_buffer_size)), // resource description for a buffer (storing the image data in this heap just to copy to the default heap)
      D3D12_RESOURCE_STATE_GENERIC_READ, // We will copy the contents from this heap to the default heap above
      nullptr,
      IID_PPV_ARGS(&intermediate)));

    D3D12_SUBRESOURCE_DATA tex_data = {};
    tex_data.pData = face->glyph->bitmap.buffer;
    tex_data.RowPitch = W;
    tex_data.SlicePitch = W * H;
    CE(command_list->Reset(command_allocator, pipeline_state_text_render));
    ::UpdateSubresources(command_list, rsrc, intermediate, 0, 0, 1, &tex_data);
    command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
      rsrc, D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)));
    CE(command_list->Close());
    g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);

    CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(srv_heap->GetCPUDescriptorHandleForHeapStart());
    int offset = int(characters_d3d12.size());
    srv_handle.Offset(offset, srv_descriptor_size);
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
    ch12.offset_in_srv_heap = offset;

    characters_d3d12[ch] = ch12;
    return &(characters_d3d12.at(ch));
  }
  else return nullptr;
}

void DX12TextScene::ClearCharactersToDisplay() {
  characters_to_display.clear();
}