#include "textrender1.hpp"
#include "MyFramework.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <d3d12.h>
#include "d3dx12.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

extern ID3DBlob* g_vs_textrender_blob;
extern void GlmMat4ToDirectXMatrix(DirectX::XMMATRIX* out, const glm::mat4& m);
extern void do_RenderText_D3D12(const std::wstring& text, float x, float y, float scale, glm::vec3 color, glm::mat4 transform);

float g_font_size = 12;
static TextPass* text_pass;
FT_Face g_face;

// https://stackoverflow.com/questions/65315241/how-can-i-fix-requires-l-value
template <class T>
constexpr auto& keep(T&& x) noexcept {
  return x;
}

void GlmMat4ToDirectXMatrix(DirectX::XMMATRIX* out, const glm::mat4& m) {
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      out->r[c].m128_f32[r] = m[c][r];
    }
  }
  //out->r[3].m128_f32[2] *= -1;
}


void do_RenderText_D3D12(const std::wstring& text, float x, float y, float scale, glm::vec3 color, glm::mat4 transform) {
  text_pass->AddText(text, x, y, scale, color, transform);
}

void do_InitCommon() {
  // Face
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
      0, &g_face)) {
      printf("Error: cannot load font file #%d=%s\n", i, ttfs[i]);
    }
    else break;
  }
  FT_Set_Pixel_Sizes(g_face, 0, g_font_size);
}

// For DX12
// TextPass's resources that depend on N
void TextPass::AllocateConstantBuffers(int n) {
  num_max_chars = n;
  device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(cb_stride * n)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&per_scene_cbs));

  D3D12_DESCRIPTOR_HEAP_DESC desc{};
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = num_max_chars;
  desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

  device12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srv_heap));
  srv_descriptor_size = device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // 4. Vertex buffers
  device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(float) * 24 * num_max_chars)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&vertex_buffers));
}

void TextPass::InitD3D12(const char* shader_source) {
  // 1. Shader
  ID3DBlob* VS{}, * PS{};
  unsigned compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

  if (!shader_source)
  {
    ID3DBlob* error = nullptr;
    const wchar_t* filenames[] = {
      L"shaders_hlsl/textrender.hlsl",
      L"../shaders_hlsl/textrender.hlsl",
    };
    for (size_t i = 0; i < 2; i++) {
      HRESULT hr = D3DCompileFromFile(filenames[i], nullptr, nullptr,
        "VSMain", "vs_5_0", compile_flags, 0, &VS, &error);

      if (error) printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));

      hr = D3DCompileFromFile(filenames[i], nullptr, nullptr,
        "PSMain", "ps_5_0", compile_flags, 0, &PS, &error);
      if (error) printf("Error compiling PS: %s\n", (char*)(error->GetBufferPointer()));
      if (error == nullptr && hr == 0) break;
    }
  }
  else {
    ID3DBlob* error = nullptr;
    HRESULT hr = D3DCompile(shader_source, strlen(shader_source), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", compile_flags, 0, &VS, &error);
    if (error) printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));
    hr = D3DCompile(shader_source, strlen(shader_source), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", compile_flags, 0, &PS, &error);
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

  device12->CreateRootSignature(0, signature->GetBufferPointer(),
    signature->GetBufferSize(), IID_PPV_ARGS(&root_signature));
  root_signature->SetName(L"Text Render Root Signature");

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
  pso_desc.pRootSignature = root_signature;
  pso_desc.VS = CD3DX12_SHADER_BYTECODE(VS);
  pso_desc.PS = CD3DX12_SHADER_BYTECODE(PS);
  pso_desc.BlendState = blend_desc;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pso_desc.DepthStencilState.DepthEnable = false;
  pso_desc.DepthStencilState.StencilEnable = false;
  pso_desc.InputLayout.pInputElementDescs = input_element_desc;
  pso_desc.InputLayout.NumElements = 2;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 2;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
  pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
  pso_desc.SampleDesc.Count = 1;
  device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state));

  // todo: populate per-scene cb

}

void TextPass::InitFreetype() {
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
  g_face = face;
}

Character_D3D12* TextPass::CreateOrGetChar(wchar_t ch) {
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
    device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &tex_desc,
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS(&rsrc));
    uint64_t tex_upload_buffer_size;
    device12->GetCopyableFootprints(&tex_desc, 0, 1, 0, nullptr, nullptr, nullptr, &tex_upload_buffer_size);
    printf("Tex upload buffer size: %llu\n", tex_upload_buffer_size);

    device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)), // upload heap
      D3D12_HEAP_FLAG_NONE, // no flags
      &keep(CD3DX12_RESOURCE_DESC::Buffer(tex_upload_buffer_size)), // resource description for a buffer (storing the image data in this heap just to copy to the default heap)
      D3D12_RESOURCE_STATE_GENERIC_READ, // We will copy the contents from this heap to the default heap above
      nullptr,
      IID_PPV_ARGS(&intermediate));

    D3D12_SUBRESOURCE_DATA tex_data = {};
    tex_data.pData = face->glyph->bitmap.buffer;
    tex_data.RowPitch = W;
    tex_data.SlicePitch = W * H;
    command_list->Reset(command_allocator, pipeline_state);
    ::UpdateSubresources(command_list, rsrc, intermediate, 0, 0, 1, &tex_data);
    command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
      rsrc, D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)));
    command_list->Close();
    command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);

    CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(srv_heap->GetCPUDescriptorHandleForHeapStart());
    int offset = int(characters_d3d12.size());
    srv_handle.Offset(offset, srv_descriptor_size);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_R8_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    device12->CreateShaderResourceView(rsrc, &srv_desc, srv_handle);

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

void TextPass::AddText(std::wstring text, float x, float y, float scale, glm::vec3 color, glm::mat4 transform) {
  bool need_new_perscene_cb = false;
  if (num_per_scene_cbs == 0 || color != last_color || transform != last_transform) {
    need_new_perscene_cb = true;
    last_color = color;
    last_transform = transform;
  }

  if (need_new_perscene_cb) {
    TextCbPerScene cb_perscene = { };
    cb_perscene.screensize.m128_f32[0] = MyFramework::WIN_W;
    cb_perscene.screensize.m128_f32[1] = MyFramework::WIN_H;
    GlmMat4ToDirectXMatrix(&cb_perscene.transform, transform);
    glm::mat4 proj = glm::perspective(60.0f * 3.14159f / 180.0f, MyFramework::WIN_W * 1.0f / MyFramework::WIN_H, 0.1f, 499.0f);
    GlmMat4ToDirectXMatrix(&cb_perscene.projection, proj);
    cb_perscene.textcolor.m128_f32[0] = color.x;
    cb_perscene.textcolor.m128_f32[1] = color.y;
    cb_perscene.textcolor.m128_f32[2] = color.z;

    CD3DX12_RANGE readRange(0, 0);
    size_t offset = cb_stride * num_per_scene_cbs;
    UINT8* pData;
    per_scene_cbs->Map(0, &readRange, (void**)&pData);
    memcpy(pData + offset, &cb_perscene, sizeof(TextCbPerScene));
    CD3DX12_RANGE writeRange(offset, offset + sizeof(TextCbPerScene));
    per_scene_cbs->Unmap(0, &writeRange);

    num_per_scene_cbs++;
  }

  const int per_scene_cb_index = num_per_scene_cbs - 1;

  for (size_t i = 0; i < text.size(); i++) {
    wchar_t ch = text.at(i);
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
    vertex_buffers->Map(0, &readRange, (void**)&pData);
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
    ctd.per_scene_cb_index = per_scene_cb_index;
    characters_to_display.push_back(ctd);
  }
}

void TextPass::RenderText(ID3D12GraphicsCommandList* command_list) {
  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  for (size_t i = 0; i < characters_to_display.size(); i++) {
    const TextPass::CharacterToDisplay& ctd = characters_to_display[i];
    command_list->IASetVertexBuffers(0, 1, &ctd.vbv);
    command_list->SetGraphicsRootConstantBufferView(0, per_scene_cbs->GetGPUVirtualAddress() + cb_stride * ctd.per_scene_cb_index);
    CD3DX12_GPU_DESCRIPTOR_HANDLE srv_handle(
      srv_heap->GetGPUDescriptorHandleForHeapStart(),
      ctd.character->offset_in_srv_heap, srv_descriptor_size);
    command_list->SetGraphicsRootDescriptorTable(1, srv_handle);
    command_list->DrawInstanced(6, 1, 0, 0);
  }
}

TextPass::TextPass(ID3D12Device* d, ID3D12CommandQueue* cq, ID3D12GraphicsCommandList* cl, ID3D12CommandAllocator* ca) :
  device12(d), command_queue(cq) {
  device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator));
  device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
    nullptr, IID_PPV_ARGS(&command_list));
  command_list->Close();
}

TextPass::~TextPass() {
  command_list->Release();
  command_allocator->Release();
}