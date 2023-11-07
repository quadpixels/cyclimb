#include "textrender.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#ifdef WIN32
#include "d3dx12.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif
GLuint vao, vbo;
extern int WIN_W, WIN_H;
extern int g_font_size;
#ifdef WIN32
extern ID3D11Device *g_device11;
extern ID3D11DeviceContext *g_context11;
extern ID3D11VertexShader* g_vs_textrender;
extern ID3D11PixelShader* g_ps_textrender;
extern ID3DBlob *g_vs_textrender_blob;
extern void GlmMat4ToDirectXMatrix(DirectX::XMMATRIX* out, const glm::mat4& m);
ID3D11InputLayout *input_layout11;

#endif
extern GLuint g_programs[];


std::map<wchar_t, Character> g_characters;
#ifdef WIN32
std::map<wchar_t, Character_D3D11> g_characters_d3d11;
#endif
FT_Face g_face;

#ifdef WIN32
struct TextCbPerScene {
  DirectX::XMVECTOR screensize; // Assume alignment at float4 boundary
  DirectX::XMMATRIX transform;
  DirectX::XMMATRIX projection;
  DirectX::XMVECTOR textcolor;
};
static ID3D11Buffer* vertex_buffer11;
static ID3D11Buffer* textcb_perscene11;
extern ID3D11BlendState* g_blendstate11;
#endif

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

void InitTextRender() {
	// VAO
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glBufferData(GL_ARRAY_BUFFER, sizeof(float)*6*4, nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	//                index size  type  normalized  stride        pointer
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  do_InitCommon();
}

#ifdef WIN32
void InitTextRender_D3D11() {
  // Declare a dynamic Vertex Buffer
  // 4 floats per vertex, 3 vertex per triangle, 2 triangles per character, so 6*4
  D3D11_BUFFER_DESC desc = { };
  desc.ByteWidth = sizeof(float) * 6 * 4;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  desc.MiscFlags = 0;
  desc.StructureByteStride = sizeof(float) * 4;
  desc.Usage = D3D11_USAGE_DYNAMIC;

  assert(SUCCEEDED(g_device11->CreateBuffer(&desc, nullptr, &vertex_buffer11)));

  desc.ByteWidth = sizeof(TextCbPerScene);
  desc.StructureByteStride = sizeof(TextCbPerScene);
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  assert(SUCCEEDED(g_device11->CreateBuffer(&desc, nullptr, &textcb_perscene11)));

  // Create Input Layout
  D3D11_INPUT_ELEMENT_DESC inputdesc[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
  };
  assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc, 2, g_vs_textrender_blob->GetBufferPointer(),
    g_vs_textrender_blob->GetBufferSize(), &input_layout11)));

  do_InitCommon();
}
#endif

Character GetCharacter(wchar_t ch) {
	if (g_characters.find(ch) == g_characters.end()) {
		if (FT_Load_Char(g_face, ch, FT_LOAD_RENDER)) {
			printf("Oh! Could not load character for rendering.\n");
		}
		GLuint texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RED,
			g_face->glyph->bitmap.width,
			g_face->glyph->bitmap.rows,
			0,
			GL_RED,
			GL_UNSIGNED_BYTE,
			g_face->glyph->bitmap.buffer
		);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    Character character = {
			texture,
			glm::ivec2(g_face->glyph->bitmap.width, g_face->glyph->bitmap.rows),
			glm::ivec2(g_face->glyph->bitmap_left, g_face->glyph->bitmap_top),
			(GLuint)(g_face->glyph->advance.x)
		};
		g_characters[ch] = character;

		glBindTexture(GL_TEXTURE_2D, 0);
	}
	return g_characters[ch];
}

#ifdef WIN32
Character_D3D11 GetCharacter_D3D11(wchar_t ch) {
  if (g_characters_d3d11.find(ch) == g_characters_d3d11.end()) {
    if (FT_Load_Char(g_face, ch, FT_LOAD_RENDER)) {
      printf("Oh! Could not load character for rendering.\n");
    }

    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView *srv = nullptr;
    int W = 0, H = 0;

    if (g_face->glyph->bitmap.buffer) {
      W = g_face->glyph->bitmap.width;
      H = g_face->glyph->bitmap.rows;

      D3D11_TEXTURE2D_DESC d2d = { };
      d2d.MipLevels = 1;
      d2d.Format = DXGI_FORMAT_R8_UNORM;
      d2d.Width = W;
      d2d.Height = H;
      d2d.ArraySize = 1;
      d2d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      d2d.SampleDesc.Count = 1;
      d2d.SampleDesc.Quality = 0;

      D3D11_SUBRESOURCE_DATA sd = { };
      sd.pSysMem = g_face->glyph->bitmap.buffer;
      sd.SysMemPitch = W; // Line distance

      assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, &sd, &tex)));

      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
      srv_desc.Format = DXGI_FORMAT_R8_UNORM;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      assert(SUCCEEDED(g_device11->CreateShaderResourceView(tex, &srv_desc, &srv)));
    }

    // texture, srv, size, bearing, advance
    Character_D3D11 character_d3d11 = {
      tex, srv,
      glm::ivec2(W, H),
      glm::ivec2(g_face->glyph->bitmap_left, g_face->glyph->bitmap_top),
      (GLuint)(g_face->glyph->advance.x)
    };
    g_characters_d3d11[ch] = character_d3d11;
  }

  return g_characters_d3d11[ch];
}
#endif

void MeasureTextWidth(std::wstring text, float *w) {
  float ret = 0.0f;
  for (std::wstring::const_iterator itr = text.begin();
      itr != text.end(); itr++) {
    Character ch = GetCharacter(*itr);
    ret = ret + ch.advance / 64.0f;
  }
  *w = ret;
}

// https://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
void do_RenderText(GLuint program, std::wstring text, GLfloat x, GLfloat y, GLfloat scale, glm::vec3 color, glm::mat4 transform) {
	glUseProgram(program);
	glUniform3f(glGetUniformLocation(program, "textColor"), color.x, color.y, color.z);
	glm::vec2 screensize(WIN_W, WIN_H);
	glUniform2fv(glGetUniformLocation(program, "screensize"), 1, glm::value_ptr(screensize));
	glUniformMatrix4fv(glGetUniformLocation(program, "transform"), 1, GL_FALSE, glm::value_ptr(transform));
  glm::mat4 proj = glm::perspective(60.0f*3.14159f/180.0f, WIN_W*1.0f/WIN_H, 0.1f, 499.0f);
  glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(vao);

	int idx = 0;
	for (std::wstring::const_iterator itr = text.begin();
			itr != text.end(); itr ++) {
		Character ch = GetCharacter(*itr);
		GLfloat xpos = x + ch.bearing.x * scale;
		GLfloat ypos = y - ch.bearing.y * scale;

		GLfloat w = ch.size.x * scale;
		GLfloat h = ch.size.y * scale;
		// Update VBO for each character
		GLfloat vertices[6][4] = {
			{ xpos,     ypos + h,   0.0, 1.0 }, //  +-------> +X
			{ xpos + w, ypos,       1.0, 0.0 }, //  |
			{ xpos,     ypos,       0.0, 0.0 }, //  |
			{ xpos,     ypos + h,   0.0, 1.0 }, //  V
			{ xpos + w, ypos + h,   1.0, 1.0 }, //
			{ xpos + w, ypos,       1.0, 0.0 }, //  +Y
		};
		// Render glyph texture over quad
		glBindTexture(GL_TEXTURE_2D, ch.textureID);
		// Update content of VBO memory
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices); // Be sure to use glBufferSubData and not glBufferData

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		// Render quad
		glDrawArrays(GL_TRIANGLES, 0, 6);
		// Now advance cursors for next glyph (note that advance is number of 1/64 pixels)
		x += (ch.advance >> 6) * scale; // Bitshift by 6 to get value in pixels (2^6 = 64 (divide amount of 1/64th pixels by 64 to get amount of pixels))
		idx ++;
	}
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

#ifdef WIN32
void do_RenderText_D3D11(std::wstring text, float x, float y, float scale, glm::vec3 color, glm::mat4 transform) {
  g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_context11->IASetInputLayout(input_layout11);
  g_context11->VSSetShader(g_vs_textrender, nullptr, 0);
  g_context11->PSSetShader(g_ps_textrender, nullptr, 0);
  unsigned stride = sizeof(float) * 4;
  unsigned zero = 0;
  g_context11->IASetVertexBuffers(0, 1, &vertex_buffer11, &stride, &zero);
  float blend_factor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
  g_context11->OMSetBlendState(g_blendstate11, blend_factor, 0xFFFFFFFF);
  g_context11->VSSetConstantBuffers(0, 1, &textcb_perscene11);
  g_context11->PSSetConstantBuffers(0, 1, &textcb_perscene11);

  TextCbPerScene cb_perscene = { };
  cb_perscene.screensize.m128_f32[0] = WIN_W;
  cb_perscene.screensize.m128_f32[1] = WIN_H;
  GlmMat4ToDirectXMatrix(&cb_perscene.transform, transform);
  glm::mat4 proj = glm::perspective(60.0f*3.14159f / 180.0f, WIN_W*1.0f / WIN_H, 0.1f, 499.0f);
  GlmMat4ToDirectXMatrix(&cb_perscene.projection, proj);
  cb_perscene.textcolor.m128_f32[0] = color.x;
  cb_perscene.textcolor.m128_f32[1] = color.y;
  cb_perscene.textcolor.m128_f32[2] = color.z;

  D3D11_MAPPED_SUBRESOURCE mapped;
  assert(SUCCEEDED(g_context11->Map(textcb_perscene11, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  memcpy(mapped.pData, &cb_perscene, sizeof(cb_perscene));
  g_context11->Unmap(textcb_perscene11, 0);

  for (int i = 0; i<int(text.size()); i++) {
    Character_D3D11 ch = GetCharacter_D3D11(text[i]);

    if (ch.srv != nullptr) {

      float xpos = x + ch.bearing.x * scale;
      float ypos = y - ch.bearing.y * scale;
      float w = ch.size.x * scale;
      float h = ch.size.y * scale;

      float vertices[6][4] = {
        { xpos,     ypos + h,   0.0, 1.0 }, //  +-------> +X
        { xpos,     ypos,       0.0, 0.0 }, //  |
        { xpos + w, ypos,       1.0, 0.0 }, //  |
        { xpos,     ypos + h,   0.0, 1.0 }, //  V
        { xpos + w, ypos,       1.0, 0.0 }, //  
        { xpos + w, ypos + h,   1.0, 1.0 }, //  +Y
      };
      assert(SUCCEEDED(g_context11->Map(vertex_buffer11, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
      memcpy(mapped.pData, vertices, sizeof(vertices));
      g_context11->Unmap(vertex_buffer11, 0);
      g_context11->PSSetShaderResources(0, 1, &ch.srv);
      g_context11->Draw(6, 0);
    }

    x += (ch.advance >> 6) * scale;
  }
}

// For DX12
// TextPass's resources that depend on N
void TextPass::AllocateConstantBuffers(int n) {
  num_max_chars = n;
  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(TextCbPerScene) * n)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&per_scene_cbs)));

  D3D12_DESCRIPTOR_HEAP_DESC desc{};
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = num_max_chars;
  desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

  CE(device12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srv_heap)));
  srv_descriptor_size = device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // 4. Vertex buffers
  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(float) * 24 * num_max_chars)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&vertex_buffers)));
}

void TextPass::InitD3D12() {
  // 1. Shader
  ID3DBlob* VS, * PS;
  {
    ID3DBlob* error = nullptr;
    unsigned compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    const wchar_t* filenames[] = {
      L"shaders/textrender.hlsl",
      L"../shaders/textrender.hlsl",
    };
    for (size_t i = 0; i < 2; i++) {
      D3DCompileFromFile(filenames[i], nullptr, nullptr,
        "VSMain", "vs_5_0", compile_flags, 0, &VS, &error);
      if (error) printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));

      D3DCompileFromFile(filenames[i], nullptr, nullptr,
        "PSMain", "ps_5_0", compile_flags, 0, &PS, &error);
      if (error) printf("Error compiling PS: %s\n", (char*)(error->GetBufferPointer()));
      else break;
    }
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

  CE(device12->CreateRootSignature(0, signature->GetBufferPointer(),
    signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
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
  pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  pso_desc.InputLayout.pInputElementDescs = input_element_desc;
  pso_desc.InputLayout.NumElements = 2;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 2;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
  pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
  pso_desc.SampleDesc.Count = 1;
  CE(device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state)));

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
    CE(device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &tex_desc,
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS(&rsrc)));
    uint64_t tex_upload_buffer_size;
    device12->GetCopyableFootprints(&tex_desc, 0, 1, 0, nullptr, nullptr, nullptr, &tex_upload_buffer_size);
    printf("Tex upload buffer size: %llu\n", tex_upload_buffer_size);

    CE(device12->CreateCommittedResource(
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
    CE(command_list->Reset(command_allocator, pipeline_state));
    ::UpdateSubresources(command_list, rsrc, intermediate, 0, 0, 1, &tex_data);
    command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
      rsrc, D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)));
    CE(command_list->Close());
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
    cb_perscene.screensize.m128_f32[0] = WIN_W;
    cb_perscene.screensize.m128_f32[1] = WIN_H;
    GlmMat4ToDirectXMatrix(&cb_perscene.transform, transform);
    glm::mat4 proj = glm::perspective(60.0f * 3.14159f / 180.0f, WIN_W * 1.0f / WIN_H, 0.1f, 499.0f);
    GlmMat4ToDirectXMatrix(&cb_perscene.projection, proj);
    cb_perscene.textcolor.m128_f32[0] = color.x;
    cb_perscene.textcolor.m128_f32[1] = color.y;
    cb_perscene.textcolor.m128_f32[2] = color.z;

    CD3DX12_RANGE readRange(0, 0);
    size_t offset = sizeof(TextCbPerScene) * num_per_scene_cbs;
    UINT8* pData;
    CE(per_scene_cbs->Map(0, &readRange, (void**)&pData));
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
    ctd.per_scene_cb_index = per_scene_cb_index;
    characters_to_display.push_back(ctd);
  }
}

#endif

void RenderText(GraphicsAPI api, std::wstring text, GLfloat x, GLfloat y, GLfloat scale, glm::vec3 color, glm::mat4 transform) {
  switch (api) {
#ifdef WIN32
    case ClimbD3D11: do_RenderText_D3D11(text, x, y, scale, color, transform); break;
#endif
    case ClimbOpenGL: do_RenderText(g_programs[6], text, x, y, scale, color, transform); break;
    default: break;
  }
}