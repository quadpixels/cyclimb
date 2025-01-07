#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

#include <Windows.h>
#include <windowsx.h>  // GET_{X,Y}_LPARAM
#undef max
#undef min

#include <stdio.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <assert.h>
#include <exception>

#include "testshapes.hpp"
#include "scene.hpp"
#include "textrender.hpp"
#include <DirectXMath.h>

#include "WICTextureLoader.h"

extern GraphicsAPI g_api;
extern Character_D3D11 GetCharacter_D3D11(wchar_t ch);
extern void Render_D3D12();

extern int WIN_W, WIN_H, SHADOW_RES;
extern Triangle *g_triangle[];
extern ColorCube *g_colorcube[];
extern ChunkGrid *g_chunkgrid[];
extern Chunk* g_chunk0;
extern void MyInit_D3D11();
extern GameScene* GetCurrentGameScene();
extern std::bitset<18> g_cam_flags;
extern TestShapesScene*   g_testscene;
extern ClimbScene*        g_climbscene;
extern LightTestScene*    g_lighttestscene;
extern char g_cam_dx, g_cam_dy, g_cam_dz,  // CONTROL axes, not OpenGL/D3D axes
g_arrow_dx, g_arrow_dy, g_arrow_dz;
extern void update();
extern DirectionalLight* g_dir_light;
extern DirectionalLight* g_dir_light1;
extern Camera* GetCurrentSceneCamera();
extern int g_scene_idx;
extern void GlmMat4ToDirectXMatrix(DirectX::XMMATRIX* out, const glm::mat4& m);
extern std::vector<Sprite*> g_projectiles;
extern bool g_shadows;
extern float g_cam_rot_x, g_cam_rot_y;
extern bool g_main_menu_visible;
extern MainMenu* g_mainmenu;
extern Chunk* ALLNULLS[26];
extern TextMessage* g_textmessage;
extern Particles*  g_particles;
extern int g_mouse_x, g_mouse_y;
extern glm::vec3 WindowCoordToPickRayDir(Camera* cam, int x, int y);
int g_titlebar_size;

bool init_done = false;
ID3D11Device *g_device11;
ID3D11DeviceContext *g_context11;
IDXGISwapChain *g_swapchain11;
ID3D11RenderTargetView *g_backbuffer_rtv11, *g_shadowmap_rtv11, *g_gbuffer_rtv11;
ID3D11RenderTargetView* g_maincanvas_rtv11;
ID3D11RenderTargetView* g_lightmask_rtv11;  // Light scatter
ID3D11DepthStencilView *g_dsv11, *g_shadowmap_dsv11;
ID3D11Texture2D *g_backbuffer, *g_depthbuffer11, *g_shadowmap11, *g_gbuffer11;
ID3D11Texture2D* g_lightmask;  // Light scatter
ID3D11Texture2D* g_maincanvas;

ID3D11ShaderResourceView *g_shadowmap_srv11, *g_gbuffer_srv11;
ID3D11ShaderResourceView* g_lightmask_srv11;
ID3D11ShaderResourceView* g_maincanvas_srv11;
D3D11_VIEWPORT g_viewport11, g_viewport_shadowmap11;
D3D11_RECT g_scissorrect11, g_scissorrect_shadowmap11;
ID3D11SamplerState *g_sampler11;
ID3D11Buffer* g_perobject_cb_default_palette;
ID3D11Buffer* g_perscene_cb_default_palette;
ID3D11Buffer* g_perscene_cb_light11;
ID3D11Buffer* g_simpletexture_cb;
ID3D11Buffer* g_lightscatter_cb;
ID3D11InputLayout* g_inputlayout_voxel11;
ID3D11BlendState* g_blendstate11;
ID3D11Buffer* g_fsquad_for_light11;
ID3D11Buffer* g_fsquad_for_lightscatter11;
ID3D11InputLayout* g_inputlayout_for_light11;
ID3D11InputLayout* g_inputlayout_lightscatter;
ID3D11DepthStencilState* g_dsstate_for_text11;
ID3D11RasterizerState* g_rsstate_normal11, * g_rsstate_wireframe11;

struct DefaultPalettePerObjectCB {
  DirectX::XMMATRIX M, V, P;
};
struct DefaultPalettePerObjectCB g_perobject_cb;
void UpdateGlobalPerObjectCB(const DirectX::XMMATRIX* M, const DirectX::XMMATRIX* V, const DirectX::XMMATRIX* P) {
  D3D11_MAPPED_SUBRESOURCE mapped;
  assert(SUCCEEDED(g_context11->Map(g_perobject_cb_default_palette, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  if (M) g_perobject_cb.M = *M;
  if (V) g_perobject_cb.V = *V;
  if (P) g_perobject_cb.P = *P;
  memcpy(mapped.pData, &g_perobject_cb, sizeof(g_perobject_cb));
  g_context11->Unmap(g_perobject_cb_default_palette, 0);
}

// Definition moved to util.hpp
struct DefaultPalettePerSceneCB g_perscene_cb;
void UpdatePerSceneCB(const DirectX::XMVECTOR* dir_light, const DirectX::XMMATRIX* lightPV, const DirectX::XMVECTOR* camPos) {
  D3D11_MAPPED_SUBRESOURCE mapped;
  assert(SUCCEEDED(g_context11->Map(g_perscene_cb_default_palette, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  if (dir_light) g_perscene_cb.dir_light = *dir_light;
  if (lightPV) g_perscene_cb.lightPV = *lightPV;
  if (camPos)  g_perscene_cb.cam_pos = *camPos;

  memcpy(mapped.pData, &g_perscene_cb, sizeof(g_perscene_cb));
  g_context11->Unmap(g_perscene_cb_default_palette, 0);
}

struct SimpleTexturePerSceneCB {
  DirectX::XMVECTOR xyoffset_alpha;
};
struct SimpleTexturePerSceneCB g_simpletexture_cb_cpu;
void UpdateSimpleTexturePerSceneCB(const float x, const float y, const float alpha) {
  D3D11_MAPPED_SUBRESOURCE mapped;
  assert(SUCCEEDED(g_context11->Map(g_simpletexture_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  g_simpletexture_cb_cpu.xyoffset_alpha.m128_f32[0] = x;
  g_simpletexture_cb_cpu.xyoffset_alpha.m128_f32[1] = y;
  g_simpletexture_cb_cpu.xyoffset_alpha.m128_f32[3] = alpha;
  memcpy(mapped.pData, &g_simpletexture_cb_cpu, sizeof(g_simpletexture_cb_cpu));
  g_context11->Unmap(g_simpletexture_cb, 0);
}

struct VolumetricLightCB g_vol_light_cb;

// Shaders ..
ID3DBlob *g_vs_default_palette_blob, *g_ps_default_palette_blob;
ID3DBlob *g_ps_default_palette_shadowed_blob;
ID3DBlob *g_vs_textrender_blob, *g_ps_textrender_blob;
ID3DBlob *g_vs_light_blob, *g_ps_light_blob;
ID3DBlob* g_vs_simpletexture_blob, * g_ps_simpletexture_blob;
ID3D11VertexShader* g_vs_default_palette;
ID3D11VertexShader* g_vs_textrender;
ID3D11VertexShader* g_vs_light;
ID3D11VertexShader* g_vs_simpletexture;
ID3D11VertexShader* g_vs_lightscatter_drawlight;
ID3D11VertexShader* g_vs_lightscatter_combine;
ID3D11PixelShader* g_ps_default_palette;
ID3D11PixelShader* g_ps_default_palette_shadowed;
ID3D11PixelShader* g_ps_textrender;
ID3D11PixelShader* g_ps_light;
ID3D11PixelShader* g_ps_simpletexture;
ID3D11PixelShader* g_ps_lightscatter_drawlight;
ID3D11PixelShader* g_ps_lightscatter_combine;

ID3DBlob *g_vs_simple_depth_blob;
ID3D11VertexShader *g_vs_simple_depth;

DirectX::XMMATRIX g_projection_d3d11;
DirectX::XMMATRIX g_projection_helpinfo_d3d11;

HWND g_hwnd;
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

void CE(HRESULT x, ID3DBlob* error) {
  if (FAILED(x)) {
    printf("ERROR: %X\n", x);
    
    if (error) {
      char* ch = (char*)error->GetBufferPointer();
      printf("Error Message: %s\n", ch);
    }

    abort();
  }
}

void InitDevice11() {
  // 1. Create Device and Swap Chain  
  D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_10_0 };

  DXGI_SWAP_CHAIN_DESC scd = { };
  scd.BufferDesc.Width = WIN_W;
  scd.BufferDesc.Height = WIN_H;
  scd.BufferDesc.RefreshRate.Numerator = 0;
  scd.BufferDesc.RefreshRate.Denominator = 0;
  scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  scd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
  scd.BufferDesc.Scaling = DXGI_MODE_SCALING_CENTERED;
  scd.SampleDesc.Count = 1;
  scd.SampleDesc.Quality = 0;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.BufferCount = 2;
  scd.OutputWindow = g_hwnd;
  scd.Windowed = TRUE;
  scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  scd.Flags = 0;

  HRESULT hr = D3D11CreateDeviceAndSwapChain(
    nullptr,
    D3D_DRIVER_TYPE_HARDWARE,
    nullptr,
    0,
    feature_levels,
    _countof(feature_levels),
    D3D11_SDK_VERSION,
    &scd,
    &g_swapchain11,
    (ID3D11Device**)&g_device11,
    nullptr,
    nullptr);
  if (hr != S_OK) {
    printf("hr=%X\n", hr);
  }
  assert(hr == S_OK);
  g_device11->GetImmediateContext(&g_context11);

  printf("g_device11  = %p\n", g_device11);
  printf("g_context11 = %p\n", g_context11);

  hr = g_swapchain11->GetBuffer(0, IID_PPV_ARGS(&g_backbuffer));
  assert(SUCCEEDED(hr));

  hr = g_device11->CreateRenderTargetView(g_backbuffer, nullptr, &g_backbuffer_rtv11);
  assert(SUCCEEDED(hr));

  g_viewport11.Height = WIN_H;
  g_viewport11.Width = WIN_W;
  g_viewport11.TopLeftX = 0;
  g_viewport11.TopLeftY = 0;
  g_viewport11.MinDepth = 0;
  g_viewport11.MaxDepth = 1; // Need to set otherwise depth will be all -1's

  g_viewport_shadowmap11.Height = SHADOW_RES;
  g_viewport_shadowmap11.Width = SHADOW_RES;
  g_viewport_shadowmap11.TopLeftX = 0;
  g_viewport_shadowmap11.TopLeftY = 0;
  g_viewport_shadowmap11.MinDepth = 0;
  g_viewport_shadowmap11.MaxDepth = 1;

  g_scissorrect11.left = 0;
  g_scissorrect11.top = 0;
  g_scissorrect11.bottom = WIN_H;
  g_scissorrect11.right = WIN_W;

  g_scissorrect_shadowmap11.left = 0;
  g_scissorrect_shadowmap11.top = 0;
  g_scissorrect_shadowmap11.bottom = SHADOW_RES;
  g_scissorrect_shadowmap11.right = SHADOW_RES;

  //g_projection_d3d11 = glm::perspective(60.0f*3.14159f / 180.0f, WIN_W*1.0f / WIN_H, 0.1f, 499.0f);
  g_projection_d3d11 = DirectX::XMMatrixPerspectiveFovLH(60.0f*3.14159f / 180.0f, WIN_W*1.0f / WIN_H, 1.0f, 499.0f);
  g_projection_helpinfo_d3d11 = DirectX::XMMatrixPerspectiveFovLH(60.0f * 3.14159f / 180.0f, 1.0f, 0.001f, 100.0f);

  // Per-Object CB for base pass
  // Per-Scene CB for base pass
  // Per-Scene CB for lighting pass
  {
    D3D11_BUFFER_DESC buf_desc = { };
    buf_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buf_desc.StructureByteStride = sizeof(DefaultPalettePerObjectCB);
    buf_desc.ByteWidth = sizeof(DefaultPalettePerObjectCB);
    buf_desc.Usage = D3D11_USAGE_DYNAMIC;
    buf_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    assert(SUCCEEDED(g_device11->CreateBuffer(&buf_desc, nullptr, &g_perobject_cb_default_palette)));

    buf_desc.StructureByteStride = sizeof(DefaultPalettePerSceneCB);
    buf_desc.ByteWidth = sizeof(DefaultPalettePerSceneCB);
    assert(SUCCEEDED(g_device11->CreateBuffer(&buf_desc, nullptr, &g_perscene_cb_default_palette)));

    buf_desc.StructureByteStride = sizeof(VolumetricLightCB);
    buf_desc.ByteWidth = sizeof(VolumetricLightCB);
    assert(SUCCEEDED(g_device11->CreateBuffer(&buf_desc, nullptr, &g_perscene_cb_light11)));

    buf_desc.StructureByteStride = sizeof(SimpleTexturePerSceneCB);
    buf_desc.ByteWidth = sizeof(SimpleTexturePerSceneCB);
    assert(SUCCEEDED(g_device11->CreateBuffer(&buf_desc, nullptr, &g_simpletexture_cb)));

    buf_desc.StructureByteStride = sizeof(LightScatterDrawLightCB);
    buf_desc.ByteWidth = sizeof(LightScatterDrawLightCB);
    assert(SUCCEEDED(g_device11->CreateBuffer(&buf_desc, nullptr, &g_lightscatter_cb)));
  }

  // Depth-Stencil buffer & View
  {
    D3D11_TEXTURE2D_DESC d2d = { };
    d2d.MipLevels = 1;
    d2d.Format = DXGI_FORMAT_R32_TYPELESS;
    d2d.Width = WIN_W;
    d2d.Height = WIN_H;
    d2d.ArraySize = 1;
    d2d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    d2d.SampleDesc.Count = 1;
    d2d.SampleDesc.Quality = 0;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = { };
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, nullptr, &g_depthbuffer11)));
    assert(SUCCEEDED(g_device11->CreateDepthStencilView(g_depthbuffer11, &dsv_desc, &g_dsv11)));

    d2d.Width = SHADOW_RES;
    d2d.Height = SHADOW_RES;
    d2d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, nullptr, &g_shadowmap11)));
    assert(SUCCEEDED(g_device11->CreateDepthStencilView(g_shadowmap11, &dsv_desc, &g_shadowmap_dsv11)));
  }

  // G Buffer and its SRV and RTV
  {
    D3D11_TEXTURE2D_DESC d2d = { };
    d2d.MipLevels = 1;
    d2d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    d2d.Width = WIN_W;
    d2d.Height = WIN_H;
    d2d.ArraySize = 1;
    d2d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d2d.SampleDesc.Count = 1;
    d2d.SampleDesc.Quality = 0;

    assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, nullptr, &g_gbuffer11)));

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
    srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    assert(SUCCEEDED(g_device11->CreateShaderResourceView(g_gbuffer11, &srv_desc, &g_gbuffer_srv11)));

    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = { };
    rtv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    assert(SUCCEEDED(g_device11->CreateRenderTargetView(g_gbuffer11, &rtv_desc, &g_gbuffer_rtv11)));
  }

  // Light scatter & main canvas
  {
    D3D11_TEXTURE2D_DESC d2d{};
    d2d.MipLevels = 1;
    d2d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d2d.Width = WIN_W;
    d2d.Height = WIN_H;
    d2d.ArraySize = 1;
    d2d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d2d.SampleDesc.Count = 1;
    d2d.SampleDesc.Quality = 0;
    d2d.Usage = D3D11_USAGE_DEFAULT;
    assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, nullptr, &g_lightmask)));
    assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, nullptr, &g_maincanvas)));

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    assert(SUCCEEDED(g_device11->CreateShaderResourceView(g_lightmask, &srv_desc, &g_lightmask_srv11)));
    assert(SUCCEEDED(g_device11->CreateShaderResourceView(g_maincanvas, &srv_desc, &g_maincanvas_srv11)));

    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    assert(SUCCEEDED(g_device11->CreateRenderTargetView(g_lightmask, &rtv_desc, &g_lightmask_rtv11)));
    assert(SUCCEEDED(g_device11->CreateRenderTargetView(g_maincanvas, &rtv_desc, &g_maincanvas_rtv11)));
  }

  // Sampler State
  {
    D3D11_SAMPLER_DESC sd = { };
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.MaxAnisotropy = 1;
    sd.MinLOD = -FLT_MAX;
    sd.MaxLOD = FLT_MAX;
    assert(SUCCEEDED(g_device11->CreateSamplerState(&sd, &g_sampler11)));
  }

  {
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    assert(SUCCEEDED(g_device11->CreateShaderResourceView(g_shadowmap11, &srv_desc, &g_shadowmap_srv11)));
  }

  // Blend State for both text and light volumes
  {
    D3D11_BLEND_DESC bd = { };
    bd.AlphaToCoverageEnable = false;
    bd.IndependentBlendEnable = false;
    bd.RenderTarget[0].BlendEnable = true;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    assert(SUCCEEDED(g_device11->CreateBlendState(&bd, &g_blendstate11)));
  }

  {
    D3D11_DEPTH_STENCIL_DESC dsd = { };
    dsd.DepthEnable = false;
    assert(SUCCEEDED(g_device11->CreateDepthStencilState(&dsd, &g_dsstate_for_text11)));
  }

  {
    D3D11_RASTERIZER_DESC rsd = { };
    rsd.AntialiasedLineEnable = false;
    rsd.CullMode = D3D11_CULL_BACK;
    rsd.DepthBias = 0;
    rsd.DepthBiasClamp = 0;
    rsd.DepthClipEnable = true;
    rsd.FillMode = D3D11_FILL_SOLID;
    rsd.FrontCounterClockwise = false;
    rsd.MultisampleEnable = false;
    rsd.ScissorEnable = false;
    rsd.SlopeScaledDepthBias = 0;
    assert(SUCCEEDED(g_device11->CreateRasterizerState(&rsd, &g_rsstate_normal11)));

    rsd.FillMode = D3D11_FILL_WIREFRAME;
    assert(SUCCEEDED(g_device11->CreateRasterizerState(&rsd, &g_rsstate_wireframe11)));
  }
}

void InitAssets11() {
  printf(">> InitAssets11\n");

  UINT compileFlags = 0;// D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

  ID3DBlob* error = nullptr;
  HRESULT hr = D3DCompileFromFile(L"shaders_hlsl/default_palette.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", compileFlags, 0, &g_vs_default_palette_blob, &error);
  CE(hr, error);
  assert(SUCCEEDED(g_device11->CreateVertexShader(g_vs_default_palette_blob->GetBufferPointer(),
    g_vs_default_palette_blob->GetBufferSize(), nullptr, &g_vs_default_palette)));

  CE(D3DCompileFromFile(L"shaders_hlsl/default_palette.hlsl", nullptr, nullptr, "PSMainWithoutShadow", "ps_4_0", compileFlags, 0, &g_ps_default_palette_blob, &error), error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(g_ps_default_palette_blob->GetBufferPointer(),
    g_ps_default_palette_blob->GetBufferSize(), nullptr, &g_ps_default_palette)));

  CE(D3DCompileFromFile(L"shaders_hlsl/default_palette.hlsl", nullptr, nullptr, "PSMainWithShadow", "ps_4_0", compileFlags, 0, &g_ps_default_palette_shadowed_blob, &error), error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(g_ps_default_palette_shadowed_blob->GetBufferPointer(),
    g_ps_default_palette_shadowed_blob->GetBufferSize(), nullptr, &g_ps_default_palette_shadowed)));

  CE(D3DCompileFromFile(L"shaders_hlsl/textrender.hlsl", nullptr, nullptr,
    "VSMain", "vs_4_0", compileFlags, 0, &g_vs_textrender_blob, &error), error);
  assert(SUCCEEDED(g_device11->CreateVertexShader(g_vs_textrender_blob->GetBufferPointer(),
    g_vs_textrender_blob->GetBufferSize(), nullptr, &g_vs_textrender)));

  CE(D3DCompileFromFile(L"shaders_hlsl/textrender.hlsl", nullptr, nullptr,
    "PSMain", "ps_4_0", compileFlags, 0, &g_ps_textrender_blob, &error), error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(g_ps_textrender_blob->GetBufferPointer(),
    g_ps_textrender_blob->GetBufferSize(), nullptr, &g_ps_textrender)));

  hr = D3DCompileFromFile(L"shaders_hlsl/simple_depth.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", compileFlags, 0, &g_vs_simple_depth_blob, &error);
  CE(hr, error);
  assert(SUCCEEDED(g_device11->CreateVertexShader(g_vs_simple_depth_blob->GetBufferPointer(),
    g_vs_simple_depth_blob->GetBufferSize(), nullptr, &g_vs_simple_depth)));

  hr = D3DCompileFromFile(L"shaders_hlsl/volumetric_light.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", compileFlags, 0, &g_vs_light_blob, &error);
  CE(hr, error);
  assert(SUCCEEDED(g_device11->CreateVertexShader(g_vs_light_blob->GetBufferPointer(), g_vs_light_blob->GetBufferSize(), nullptr, &g_vs_light)));

  hr = D3DCompileFromFile(L"shaders_hlsl/volumetric_light.hlsl", nullptr, nullptr, "PSMain", "ps_4_0", compileFlags, 0, &g_ps_light_blob, &error);
  CE(hr, error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(g_ps_light_blob->GetBufferPointer(), g_ps_light_blob->GetBufferSize(), nullptr, &g_ps_light)));

  hr = D3DCompileFromFile(L"shaders_hlsl/simple_texture.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", compileFlags, 0, &g_vs_simpletexture_blob, &error);
  CE(hr, error);
  assert(SUCCEEDED(g_device11->CreateVertexShader(g_vs_simpletexture_blob->GetBufferPointer(), g_vs_simpletexture_blob->GetBufferSize(), nullptr, &g_vs_simpletexture)));

  hr = D3DCompileFromFile(L"shaders_hlsl/simple_texture.hlsl", nullptr, nullptr, "PSMain", "ps_4_0", compileFlags, 0, &g_ps_simpletexture_blob, &error);
  CE(hr, error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(g_ps_simpletexture_blob->GetBufferPointer(), g_ps_simpletexture_blob->GetBufferSize(), nullptr, &g_ps_simpletexture)));

  ID3DBlob* vs_blob, *ps_blob;
  hr = D3DCompileFromFile(L"shaders_hlsl/shaders_drawlight_withdepth.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", compileFlags, 0, &vs_blob, nullptr);
  CE(hr, error);
  assert(SUCCEEDED(g_device11->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &g_vs_lightscatter_drawlight)));

  hr = D3DCompileFromFile(L"shaders_hlsl/shaders_drawlight_withdepth.hlsl", nullptr, nullptr, "PSMain", "ps_4_0", compileFlags, 0, &ps_blob, nullptr);
  CE(hr, error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &g_ps_lightscatter_drawlight)));

  hr = D3DCompileFromFile(L"shaders_hlsl/shaders_combine_withdepth.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", compileFlags, 0, &vs_blob, nullptr);
  CE(hr, error);
  assert(SUCCEEDED(g_device11->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &g_vs_lightscatter_combine)));

  hr = D3DCompileFromFile(L"shaders_hlsl/shaders_combine_withdepth.hlsl", nullptr, nullptr, "PSMain", "ps_4_0", compileFlags, 0, &ps_blob, nullptr);
  CE(hr, error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &g_ps_lightscatter_combine)));

  // Input Layout requires VS to be ready
  D3D11_INPUT_ELEMENT_DESC inputdesc1[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 1, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 2, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
  };
  assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc1, 4, g_vs_default_palette_blob->GetBufferPointer(),
    g_vs_default_palette_blob->GetBufferSize(), &g_inputlayout_voxel11)));

  // Fullscreen quad
  {
    float data[][4] = {  // N D C       TexCoord
      {-1, 1, 0, 0 }, //  -1,1   1,1    0,0     1,0
      { 1, 1, 1, 0 }, //
      {-1,-1, 0, 1 }, //
      { 1, 1, 1, 0 }, //
      { 1,-1, 1, 1 }, //
      {-1,-1, 0, 1 }, //  -1,-1  1,-1   0,1     1,1
    };
    D3D11_BUFFER_DESC buf_desc = { };
    buf_desc.StructureByteStride = 4 * sizeof(float);
    buf_desc.ByteWidth = sizeof(float) * 24;
    buf_desc.Usage = D3D11_USAGE_IMMUTABLE;
    buf_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA subsc = { };
    subsc.pSysMem = data;
    subsc.SysMemPitch = sizeof(float) * 24;
    assert(SUCCEEDED(g_device11->CreateBuffer(&buf_desc, &subsc, &g_fsquad_for_light11)));
  }

  // Fullscreen quad, but for lightscatter
  {
    float data[][5] = {
      {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f }, //  
      {  1.0f, -1.0f, 0.0f,  1.0f, 1.0f }, //  +-----------+ UV = (1, 0), NDC=(1, 1)
      { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f }, //  |           |
                                           //  |           |
      { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f }, //  |           |
      { -1.0f,  1.0f, 0.0f,  0.0f, 0.0f }, //  |           |
      {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f }, //  +-----------+ UV = (1, 1), NDC=(1,-1)
    };

    D3D11_BUFFER_DESC desc = { };
    desc.ByteWidth = sizeof(data);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.StructureByteStride = sizeof(float) * 5;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA srd = { };
    srd.pSysMem = data;

    HRESULT hr = g_device11->CreateBuffer(&desc, &srd, &g_fsquad_for_lightscatter11);
    assert(SUCCEEDED(hr));

  }

  // Input Layout requires VS to be ready
  {
    D3D11_INPUT_ELEMENT_DESC inputdesc2[] = {
      { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
      { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc2, 2, g_vs_light_blob->GetBufferPointer(), g_vs_light_blob->GetBufferSize(), &g_inputlayout_for_light11)));
  }

  {
    D3D11_INPUT_ELEMENT_DESC inputdesc2[] = {
      { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
      { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc2, 2,
      vs_blob->GetBufferPointer(),
      vs_blob->GetBufferSize(),
      &g_inputlayout_lightscatter)));
  }

  CoInitialize(nullptr);
}

void CreateCyclimbWindow() {
  AllocConsole();
  freopen_s((FILE**)stdin, "CONIN$", "r", stderr);
  freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
  freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

  WNDCLASS windowClass = { 0 };
  windowClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
  windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
  windowClass.hInstance = NULL;
  windowClass.lpfnWndProc = WndProc;
  windowClass.lpszClassName = "Window in Console"; //needs to be the same name
  //when creating the window as well
  windowClass.style = CS_HREDRAW | CS_VREDRAW;

  LPCSTR window_name = (g_api == GraphicsAPI::ClimbD3D11) ? "ChaoyueClimb (D3D11)" : "ChaoyueClimb (D3D12)";
  LPCSTR class_name = "ChaoyueClimb_class";
  HINSTANCE hinstance = GetModuleHandle(nullptr);

  if (!RegisterClass(&windowClass)) {
    printf("Cannot register window class\n");
  }

  g_hwnd = CreateWindowA(
    windowClass.lpszClassName,
    window_name,
    WS_OVERLAPPEDWINDOW,
    16,
    16,
    WIN_W, WIN_H,
    nullptr, nullptr,
    hinstance, nullptr);

  TITLEBARINFOEX* ptinfo = (TITLEBARINFOEX*)malloc(sizeof(TITLEBARINFOEX));
  ptinfo->cbSize = sizeof(TITLEBARINFOEX);
  SendMessage(g_hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)ptinfo);
  g_titlebar_size = ptinfo->rcTitleBar.bottom - ptinfo->rcTitleBar.top;
  delete ptinfo;
}

int main_d3d11(int argc, char** argv) {
  CreateCyclimbWindow();
  InitDevice11();
  InitAssets11(); // Will call CoInitialize(nullptr) here, needed for loading images
  MyInit_D3D11();

  init_done = true;

  BOOL x = ShowWindow(g_hwnd, SW_RESTORE);
  printf("ShowWindow returns %d, g_hwnd=%X\n", x, int(g_hwnd));

  // Message Loop
  MSG msg = { 0 };
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return 0;
}

static void expanded_draw_calls() {
  // Pipeline State
  {
    float bgcolor[4] = { 0.1f, 0.1f, 0.4f, 1.0f };
    g_context11->ClearDepthStencilView(g_dsv11, D3D11_CLEAR_DEPTH, 1.0f, 0);
    g_context11->ClearDepthStencilView(g_shadowmap_dsv11, D3D11_CLEAR_DEPTH, 1.0f, 0);
    g_context11->ClearRenderTargetView(g_backbuffer_rtv11, bgcolor);
    g_context11->RSSetViewports(1, &g_viewport_shadowmap11);
    g_context11->RSSetScissorRects(1, &g_scissorrect_shadowmap11);
    g_context11->OMSetRenderTargets(1, &g_backbuffer_rtv11, g_dsv11);

    g_context11->PSSetSamplers(0, 1, &g_sampler11);
    g_context11->VSSetShader(g_vs_default_palette, nullptr, 0);
    g_context11->PSSetShader(g_ps_default_palette, nullptr, 0);
    ID3D11ShaderResourceView *null_srv = nullptr;
    g_context11->PSSetShaderResources(0, 1, &null_srv);

    ID3D11Buffer* cbs[] = { g_perobject_cb_default_palette, g_perscene_cb_default_palette };
    UpdatePerSceneCB(&(g_dir_light->GetDir_D3D11()), &(g_dir_light->GetPV_D3D11()), &(GetCurrentSceneCamera()->GetPos_D3D11()));
    g_context11->VSSetConstantBuffers(0, 2, cbs);
  }

  // V and P matrices
  {
    bool is_testing_dir_light = false;
    DirectX::XMMATRIX V, P;
    P = g_dir_light->GetP_D3D11_DXMath();
    V = g_dir_light->GetV_D3D11();
    UpdateGlobalPerObjectCB(nullptr, &V, &P);
  }

  ID3D11RenderTargetView* rtv_empty = nullptr;
  g_context11->OMSetRenderTargets(1, &rtv_empty, g_shadowmap_dsv11);

  // Depth pass
  {
    g_triangle[0]->Render_D3D11();
    g_triangle[1]->Render_D3D11();
    g_colorcube[0]->Render_D3D11();
    DirectX::XMMATRIX M = DirectX::XMMatrixTranslation(-30, 0, 0);
    g_chunk0->Render_D3D11(M);
    g_chunkgrid[2]->Render_D3D11(glm::vec3(10, 10, 10), glm::vec3(1), glm::mat3(1), glm::vec3(0.5, 0.5, 0.5));
    g_testscene->test_sprite->Render_D3D11();
    g_testscene->test_background->Render_D3D11();
  }

  g_context11->OMSetRenderTargets(1, &g_backbuffer_rtv11, g_dsv11);
  g_context11->RSSetViewports(1, &g_viewport11);
  g_context11->RSSetScissorRects(1, &g_scissorrect11);

  // Drawing with shadow map
  {
    DirectX::XMMATRIX V, P;
    V = GetCurrentSceneCamera()->GetViewMatrix_D3D11();
    P = g_projection_d3d11;
    UpdateGlobalPerObjectCB(nullptr, &V, &P);
    DirectX::XMVECTOR dir_light;
    UpdatePerSceneCB(&g_dir_light->GetDir_D3D11(), &(g_dir_light->GetPV_D3D11()), &(GetCurrentSceneCamera()->GetPos_D3D11()));
    g_context11->PSSetConstantBuffers(1, 1, &g_perscene_cb_default_palette);

    g_context11->PSSetShaderResources(0, 1, &g_shadowmap_srv11);
    g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_context11->VSSetShader(g_vs_default_palette, nullptr, 0);
    g_context11->PSSetShader(g_ps_default_palette_shadowed, nullptr, 0);
  }

  {
    //g_triangle[0]->Render_D3D11();
    g_testscene->test_background->Render_D3D11();
    //g_triangle[1]->Render_D3D11();
    g_colorcube[0]->Render_D3D11();
    DirectX::XMMATRIX M = DirectX::XMMatrixTranslation(-30, 0, 0);
    g_chunk0->Render_D3D11(M);
    g_chunkgrid[2]->Render_D3D11(glm::vec3(10, 10, 10), glm::vec3(1), glm::mat3(1), glm::vec3(0.5, 0.5, 0.5));
    g_testscene->test_sprite->Render_D3D11();
  }

  {
    RenderText(ClimbD3D11, L"ABC123哈哈嘿", 64.0f, 32.0f, 1.0f,
      glm::vec3(1.0f, 1.0f, 0.2f), glm::mat4(1));
  }

  g_swapchain11->Present(1, 0);
}

void IssueDrawCalls_D3D11(Sprite::DrawMode draw_mode) {
  GameScene* scene = GetCurrentGameScene();
  if (scene) {
    std::vector<Sprite*>* sprites = GetCurrentGameScene()->GetSpriteListForRender();
    for (Sprite* s : *sprites) {
      if (s && s->draw_mode == draw_mode) s->Render_D3D11();
    }
    for (Sprite* s : g_projectiles) {
      if (s && s->draw_mode == draw_mode)
        s->Render_D3D11();
    }
  }
}

// Using glm::mat4 for compatibility with OpenGL code path
void RenderScene_D3D11(const DirectX::XMMATRIX& V, const DirectX::XMMATRIX& P, Sprite::DrawMode draw_mode) {
  ID3D11Buffer* cbs[] = { g_perobject_cb_default_palette, g_perscene_cb_default_palette };
  UpdatePerSceneCB(&(g_dir_light->GetDir_D3D11()), &(g_dir_light->GetPV_D3D11()), &(GetCurrentSceneCamera()->GetPos_D3D11()));
  g_context11->VSSetConstantBuffers(0, 2, cbs);
  g_context11->PSSetConstantBuffers(1, 1, &g_perscene_cb_default_palette);

  {
    // Prepare V and P 
    bool is_testing_dir_light = false;
    UpdateGlobalPerObjectCB(nullptr, &V, &P);

    // Perpare dir_light
    UpdatePerSceneCB(&g_dir_light->GetDir_D3D11(), &(g_dir_light->GetPV_D3D11()), &(GetCurrentSceneCamera()->GetPos_D3D11()));
  }

  IssueDrawCalls_D3D11(draw_mode);
}

void MyInit_D3D11() {
  Triangle::Init_D3D11();
  ColorCube::Init_D3D11();

  g_triangle[0] = new Triangle();
  g_triangle[0]->pos = glm::vec3(10, 0, -10);
  g_triangle[1] = new Triangle();
  g_triangle[1]->pos = glm::vec3(0.1, 0, 0);
  g_colorcube[0] = new ColorCube();
  g_colorcube[0]->pos = glm::vec3(11, 0, 0);

  g_testscene = new TestShapesScene();
  ChunkSprite* test_sprite = new ChunkSprite(new ChunkGrid(
    "climb/coords.vox"
  )), *global_xyz = new ChunkSprite(new ChunkGrid(
    "climb/xyz.vox"
  )), *test_background = new ChunkSprite(new ChunkGrid(
    "climb/bg1_2.vox"
  ));
  g_testscene->test_sprite = test_sprite;
  g_testscene->global_xyz = global_xyz;
  g_testscene->test_background = test_background;


  g_chunk0 = new Chunk();
  g_chunk0->LoadDefault();
  g_chunk0->BuildBuffers(ALLNULLS);
  g_chunk0->pos = glm::vec3(-10, 10, 10);

  g_chunkgrid[2] = new ChunkGrid(5, 5, 5);
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 5; j++) {
      for (int k = 0; k < 5; k++) {
        g_chunkgrid[2]->SetVoxel(i, j, k, (i + j + k) % 255);
      }
    }
  }

  test_sprite->pos = glm::vec3(0, -4, 0);
  test_sprite->anchor = glm::vec3(0.5f, 0.5f, 0.5f);
  test_sprite->scale = glm::vec3(3, 3, 3);

  global_xyz->pos = glm::vec3(-40, -42, -24);
  global_xyz->scale = glm::vec3(2, 2, 2);
  global_xyz->anchor = glm::vec3(0.5f, 0.5f, 0.5f);

  test_background->pos = glm::vec3(0, 0, -10);
  test_background->scale = glm::vec3(2, 2, 2);

  g_testscene = new TestShapesScene();
  g_testscene->test_sprite = test_sprite;
  g_testscene->global_xyz = global_xyz;
  g_testscene->test_background = test_background;

  g_dir_light = new DirectionalLight(glm::vec3(1, -3, -1), glm::vec3(1, 3, -1));
  //g_dir_light  = new DirectionalLight(glm::vec3(1, -1, 0), glm::vec3(-200, 200, 0), glm::vec3(1, 0, 0), 7 * 3.14159f / 180.0f);
  g_dir_light1 = new DirectionalLight(glm::vec3(1, -1, 0), glm::vec3(-200, 200, 0), glm::vec3(1, 0, 0), 7 * 3.14159f / 180.0f);

  // Font stuff
  InitTextRender_D3D11();

  g_textmessage = new TextMessage();

  // Climb Scene Stuff
  g_chunkgrid[3] = new ChunkGrid(1, 1, 1);
  g_chunkgrid[3]->SetVoxel(0, 0, 0, 12);

  Particles::InitStatic(g_chunkgrid[3]);
  g_particles = new Particles();

  ClimbScene::InitStatic();
  g_climbscene = new ClimbScene();
  g_climbscene->Init();

  if (g_scene_idx == 2) {
    LightTestScene::InitStatic();
    g_lighttestscene = new LightTestScene();
  }

  FullScreenQuad::Init_D3D11();

  FullScreenQuad* fsq = new FullScreenQuad(ClimbScene::helpinfo_srv);
  g_testscene->fsquad = fsq;

  ImageSprite2D::Init_D3D11();

  // Depends on ClimbScene's static resources
  g_mainmenu = new MainMenu();
}

void Render_D3D11() {
  bool USE_EXPANDED_DRAWCALLS = false;
  if (USE_EXPANDED_DRAWCALLS && (g_scene_idx == 0)) {
    expanded_draw_calls();
  }
  else {
    Camera* cam = GetCurrentSceneCamera();
    // 0: Prepare and a bunch of pipeline states
    GetCurrentGameScene()->PreRender();
    GetCurrentGameScene()->PrepareSpriteListForRender();

    // Clear a bunch of render targets
    float bgcolor[4] = { 0.2f, 0.2f, 0.4f, 1.0f };
    g_context11->ClearDepthStencilView(g_dsv11, D3D11_CLEAR_DEPTH, 1.0f, 0);
    g_context11->ClearDepthStencilView(g_shadowmap_dsv11, D3D11_CLEAR_DEPTH, 1.0f, 0);
    g_context11->ClearRenderTargetView(g_backbuffer_rtv11, bgcolor);
    float zeros[4] = { 0, 0, 0, 0 };
    g_context11->ClearRenderTargetView(g_gbuffer_rtv11, zeros);

    ID3D11SamplerState *sampler_empty = nullptr;
    g_context11->PSSetSamplers(0, 1, &sampler_empty);
    g_context11->IASetInputLayout(g_inputlayout_voxel11);

    // Shadow Pass
    if (g_shadows) {
      ID3D11ShaderResourceView* srv_empty = nullptr;
      g_context11->PSSetShaderResources(0, 1, &srv_empty);
      ID3D11RenderTargetView* rtv_empty = nullptr;
      g_context11->OMSetRenderTargets(1, &rtv_empty, g_shadowmap_dsv11);
      g_context11->RSSetViewports(1, &g_viewport_shadowmap11);
      g_context11->RSSetScissorRects(1, &g_scissorrect_shadowmap11);
      g_context11->VSSetShader(g_vs_default_palette, nullptr, 0);
      g_context11->PSSetShader(g_ps_default_palette, nullptr, 0);

      DirectX::XMMATRIX V, P;
      P = g_dir_light->GetP_D3D11_DXMath();
      V = g_dir_light->GetV_D3D11();
      RenderScene_D3D11(V, P, Sprite::DrawMode::NORMAL);
    }

    // Normal Pass
    ID3D11RenderTargetView* rtvs[] = { g_backbuffer_rtv11, g_gbuffer_rtv11 };
    g_context11->OMSetRenderTargets(2, rtvs, g_dsv11);
    g_context11->RSSetViewports(1, &g_viewport11);
    g_context11->RSSetScissorRects(1, &g_scissorrect11);
    g_context11->VSSetShader(g_vs_default_palette, nullptr, 0);
    g_context11->PSSetShader(g_ps_default_palette_shadowed, nullptr, 0);
    g_context11->PSSetShaderResources(0, 1, &g_shadowmap_srv11);
    g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context11->IASetInputLayout(g_inputlayout_voxel11);
    g_context11->PSSetSamplers(0, 1, &g_sampler11);
    g_context11->RSSetState(g_rsstate_normal11);

    RenderScene_D3D11(cam->GetViewMatrix_D3D11(), g_projection_d3d11, Sprite::DrawMode::NORMAL);

    // Wireframe Pass
    g_context11->RSSetState(g_rsstate_wireframe11);
    RenderScene_D3D11(cam->GetViewMatrix_D3D11(), g_projection_d3d11, Sprite::DrawMode::WIREFRAME);

    g_context11->RSSetState(nullptr);

    GameScene* scene = GetCurrentGameScene();

    if (true) {
      // Volumetric lights
      if (scene) {
        if (1) {
          scene->PrepareLights();
          g_context11->OMSetRenderTargets(1, &g_backbuffer_rtv11, g_dsv11);
          scene->RenderLights();
        }
      }
    }

    // U I
    glm::mat4 uitransform(1);
    uitransform *= glm::rotate(uitransform, g_cam_rot_x, glm::vec3(0.0f, 1.0f, 0.0f));
    uitransform *= glm::rotate(uitransform, g_cam_rot_y, glm::vec3(1.0f, 0.0f, 0.0f));

    ID3D11Buffer* cbs[] = { g_perobject_cb_default_palette, g_perscene_cb_default_palette };
    g_context11->VSSetConstantBuffers(0, 2, cbs);
    g_context11->PSSetConstantBuffers(1, 1, &g_perscene_cb_default_palette);
    g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context11->IASetInputLayout(g_inputlayout_voxel11);
    g_context11->PSSetSamplers(0, 1, &g_sampler11);
    g_context11->OMSetRenderTargets(2, rtvs, g_dsv11);

    g_context11->OMSetDepthStencilState(g_dsstate_for_text11, 0);
    if (scene) scene->RenderHUD_D3D11();

    {
      if (g_main_menu_visible) {
        g_mainmenu->Render_D3D11(uitransform);
      }
    }
    g_context11->OMSetDepthStencilState(nullptr, 0);

    // Image test
    

    g_swapchain11->Present(1, 0);
  }
}

void OnKeyDown(WPARAM wParam, LPARAM lParam) {
  //printf("KeyDown wParam=%X lParam=%X\n", int(wParam), int(lParam));
  if (lParam & 0x40000000) return;

  if (wParam == 27) {
    if (g_main_menu_visible) {
      g_mainmenu->OnEscPressed();
    }
    else {
      bool is_from_exit = true;

      if (g_mainmenu->curr_selection.empty() == false) {
        is_from_exit = false;
      }
      if (is_from_exit) {
        g_mainmenu->EnterMenu(MainMenu::MenuKind::MAIN, true);
      }
      g_main_menu_visible = true;
    }
  }

  if (g_main_menu_visible) {
    switch (wParam) {
    case VK_UP: g_mainmenu->OnUpDownPressed(-1); break;
    case VK_DOWN: g_mainmenu->OnUpDownPressed(1); break;
    case VK_LEFT: g_mainmenu->OnLeftRightPressed(-1); break;
    case VK_RIGHT: g_mainmenu->OnLeftRightPressed(1); break;
    case 13: g_mainmenu->OnEnter(); break;
    }
  }

  if (GetCurrentGameScene() == g_testscene ||
      GetCurrentGameScene() == g_lighttestscene) {
    switch (wParam) {
    case 'T': g_cam_flags.set(0); break;
    case 'G': g_cam_flags.set(1); break;
    case 'F': g_cam_flags.set(2); break;
    case 'H': g_cam_flags.set(3); break;
    case 'R': g_cam_flags.set(4); break;
    case 'Y': g_cam_flags.set(5); break;
    case 'I': g_cam_flags.set(12); break;
    case 'K': g_cam_flags.set(13); break;
    case 'J': g_cam_flags.set(14); break;
    case 'L': g_cam_flags.set(15); break;
    case 'U': g_cam_flags.set(16); break;
    case 'O': g_cam_flags.set(17); break;

    case 'W': g_cam_dy = 1; break;
    case 'S': g_cam_dy = -1; break;
    case 'A': g_cam_dx = -1; break;
    case 'D': g_cam_dx = 1; break;
    case 'Q': g_cam_dz = -1; break;
    case 'E': g_cam_dz = 1; break;
    }
  }
  else GetCurrentGameScene()->OnKeyPressed(char(tolower(wParam)));
}

void OnKeyUp(WPARAM wParam, LPARAM lParam) {
  if (GetCurrentGameScene() == g_testscene ||
      GetCurrentGameScene() == g_lighttestscene) {
    switch (wParam) {
    case 'T': g_cam_flags.reset(0); break;
    case 'G': g_cam_flags.reset(1); break;
    case 'F': g_cam_flags.reset(2); break;
    case 'H': g_cam_flags.reset(3); break;
    case 'R': g_cam_flags.reset(4); break;
    case 'Y': g_cam_flags.reset(5); break;
    case 'I': g_cam_flags.reset(12); break;
    case 'K': g_cam_flags.reset(13); break;
    case 'J': g_cam_flags.reset(14); break;
    case 'L': g_cam_flags.reset(15); break;
    case 'U': g_cam_flags.reset(16); break;
    case 'O': g_cam_flags.reset(17); break;

    case 'W': g_cam_dy = 0; break;
    case 'S': g_cam_dy = 0; break;
    case 'A': g_cam_dx = 0; break;
    case 'D': g_cam_dx = 0; break;
    case 'Q': g_cam_dz = 0; break;
    case 'E': g_cam_dz = 0; break;
    }
  }
  else GetCurrentGameScene()->OnKeyReleased(char(tolower(wParam)));
}

void OnMouseMove(int x, int y) {
  g_mouse_x = x;
  g_mouse_y = y + g_titlebar_size;
  GetCurrentGameScene()->OnMouseMove(g_mouse_x, g_mouse_y);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
  switch (message)
  {
  case WM_KEYDOWN:
    OnKeyDown(wparam, lparam);
    break;
  case WM_KEYUP:
    OnKeyUp(wparam, lparam);
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  case WM_PAINT:
    if (init_done) {
      switch (g_api) {
      case ClimbD3D11:
        Render_D3D11();
        break;
      case ClimbD3D12:
        Render_D3D12();
        break;
      }
    }
    update();
    break;
  case WM_MOUSEMOVE:
    OnMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
    break;
  case WM_LBUTTONDOWN:
    GetCurrentGameScene()->OnMouseDown();
    break;
  case WM_LBUTTONUP:
    GetCurrentGameScene()->OnMouseUp();
    break;
  default:
    return DefWindowProc(hwnd, message, wparam, lparam);
  }
  return 0;
}
