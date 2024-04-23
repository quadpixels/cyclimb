#include "scene_dx11.hpp"
#include <assert.h>
#include <stdio.h>

#include <d3d11.h>
#include <d3dcompiler.h>

extern ID3D11Device* g_device11;
extern ID3D11DeviceContext* g_context11;
extern ID3D11RenderTargetView* g_backbuffer_rtv11;
extern IDXGISwapChain* g_swapchain11;
extern D3D11_VIEWPORT g_viewport, g_viewport_shadowmap;
extern D3D11_RECT g_scissor_rect;
extern ID3D11Buffer* g_perobject_cb_default_palette;
extern ID3D11Buffer* g_perscene_cb_default_palette;
extern ID3D11Texture2D* g_backbuffer;
void UpdateGlobalPerObjectCB(const DirectX::XMMATRIX* M, const DirectX::XMMATRIX* V, const DirectX::XMMATRIX* P);
void UpdateGlobalPerSceneCB(const DirectX::XMVECTOR* dir_light, const DirectX::XMMATRIX* lightPV, const DirectX::XMVECTOR* camPos);

extern int WIN_W, WIN_H, SHADOW_RES;

static void CE(HRESULT x, ID3DBlob* error) {
  if (FAILED(x)) {
    printf("ERROR: %X\n", x);

    if (error) {
      char* ch = (char*)error->GetBufferPointer();
      printf("Error Message: %s\n", ch);
    }

    abort();
  }
}

void DX11ClearScreenScene::Render() {
  float bgcolor[4] = { 0.1f, 0.1f, 0.4f, 1.0f };
  g_context11->ClearRenderTargetView(g_backbuffer_rtv11, bgcolor);
  g_swapchain11->Present(1, 0);
}

void DX11ClearScreenScene::Update(float secs) {}

DX11HelloTriangleScene::DX11HelloTriangleScene() {
  // Vertex buffer
  Vertex verts[] = {
    { { 0.0f, 0.25f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
    { { 0.25f, -0.25f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
    { { -0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
  };
  D3D11_BUFFER_DESC desc{};
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.ByteWidth = sizeof(verts);
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  desc.CPUAccessFlags = 0;
  desc.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = verts;
  data.SysMemPitch = 0;
  data.SysMemSlicePitch = 0;

  HRESULT hr = g_device11->CreateBuffer(&desc, &data, &vertex_buffer);
  assert(SUCCEEDED(hr));

  // Shaders
  ID3DBlob* error = nullptr;
  UINT compileFlags = 0;
  ID3DBlob* vs_shader_blob;
  hr = D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", compileFlags, 0, &vs_shader_blob, &error);
  assert(SUCCEEDED(g_device11->CreateVertexShader(vs_shader_blob->GetBufferPointer(),
    vs_shader_blob->GetBufferSize(), nullptr, &vs)));
  CE(hr, error);
  ID3DBlob* ps_shader_blob;
  hr = D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr, "PSMain", "ps_4_0", compileFlags, 0, &ps_shader_blob, &error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(ps_shader_blob->GetBufferPointer(),
    ps_shader_blob->GetBufferSize(), nullptr, &ps)));
  CE(hr, error);

  // Input Layout
  D3D11_INPUT_ELEMENT_DESC inputdesc[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
  };
  assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc, 2, vs_shader_blob->GetBufferPointer(),
    vs_shader_blob->GetBufferSize(), &input_layout)));

  // CB
  {
    desc.ByteWidth = sizeof(PerTriangleCB);
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    DirectX::XMFLOAT2 pos;
    pos.x = 0.0f;
    pos.y = 0.0f;
    data.pSysMem = &pos;
    hr = g_device11->CreateBuffer(&desc, &data, &per_triangle_cb);
    assert(SUCCEEDED(hr));
  }

  elapsed_secs = 0;
}

void DX11HelloTriangleScene::Render() {
  float bgcolor[4] = { 0.1f, 0.1f, 0.6f, 1.0f };
  g_context11->ClearRenderTargetView(g_backbuffer_rtv11, bgcolor);
  g_context11->IASetInputLayout(input_layout);
  g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  unsigned stride = sizeof(float) * 7, offset = 0;
  g_context11->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
  g_context11->VSSetShader(vs, nullptr, 0);
  g_context11->PSSetShader(ps, nullptr, 0);
  g_context11->VSSetConstantBuffers(0, 1, &per_triangle_cb);
  g_context11->RSSetViewports(1, &g_viewport);
  g_context11->RSSetScissorRects(1, &g_scissor_rect);
  g_context11->OMSetRenderTargets(1, &g_backbuffer_rtv11, nullptr);
  g_context11->Draw(3, 0);
  g_swapchain11->Present(1, 0);
}

void DX11HelloTriangleScene::Update(float secs) {
  elapsed_secs += secs;
  D3D11_MAPPED_SUBRESOURCE mapped;
  DirectX::XMFLOAT2 pos;
  pos.x = cosf(elapsed_secs * 3.14159) * 0.5f;
  pos.y = sinf(elapsed_secs * 3.14159) * 0.5f;
  g_context11->Map(per_triangle_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  memcpy(mapped.pData, &pos, sizeof(pos));
  g_context11->Unmap(per_triangle_cb, 0);
}

DX11ChunksScene::DX11ChunksScene() {
  elapsed_secs = 0;

  chunk = new Chunk();
  chunk->LoadDefault();
  chunk->BuildBuffers(nullptr);

  // Shaders
  ID3DBlob* error = nullptr;
  UINT compileFlags = 0;
  ID3DBlob* vs_shader_blob;
  HRESULT hr;
  hr = D3DCompileFromFile(L"../shaders_hlsl/default_palette.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", compileFlags, 0, &vs_shader_blob, &error);
  hr = g_device11->CreateVertexShader(vs_shader_blob->GetBufferPointer(),
    vs_shader_blob->GetBufferSize(), nullptr, &vs);
  CE(hr, error);
  if (error) {
    printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));
  }
  ID3DBlob* ps_shader_blob;
  hr = D3DCompileFromFile(L"../shaders_hlsl/default_palette.hlsl", nullptr, nullptr, "PSMainWithShadow", "ps_4_0", compileFlags, 0, &ps_shader_blob, &error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(ps_shader_blob->GetBufferPointer(),
    ps_shader_blob->GetBufferSize(), nullptr, &ps)));
  if (error) {
    printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));
  }

  // IA
  D3D11_INPUT_ELEMENT_DESC inputdesc1[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 1, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 2, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
  };
  assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc1, 4, vs_shader_blob->GetBufferPointer(),
    vs_shader_blob->GetBufferSize(), &input_layout)));

  // Depth-stencil buffer and view
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
    ID3D11Texture2D* dsv_tex;

    assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, nullptr, &dsv_tex)));
    assert(SUCCEEDED(g_device11->CreateDepthStencilView(dsv_tex, &dsv_desc, &dsv_main)));

    d2d.Width = SHADOW_RES;
    d2d.Height = SHADOW_RES;
    d2d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* shadow_map_tex;
    assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, nullptr, &shadow_map_tex)));
    assert(SUCCEEDED(g_device11->CreateDepthStencilView(shadow_map_tex, &dsv_desc, &dsv_shadowmap)));

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    assert(SUCCEEDED(g_device11->CreateShaderResourceView(shadow_map_tex, &srv_desc, &srv_shadowmap)));
  }

  // Backdrop
  {
    const float L = 80.0f, H = -35.0f;
    float backdrop_verts[] = {  // X, Y, Z, nidx, data, ao
      -L, H, L, 4, 44, 0,
       L, H, L, 4, 44, 0,
      -L, H, -L, 4, 44, 0,
       L, H, L, 4, 44, 0,
      L, H, -L, 4, 44, 0,
      -L, H, -L, 4, 44, 0,
    };
    D3D11_BUFFER_DESC desc{};
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.ByteWidth = sizeof(backdrop_verts);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = backdrop_verts;
    data.SysMemPitch = 0;
    data.SysMemSlicePitch = 0;

    HRESULT hr = g_device11->CreateBuffer(&desc, &data, &backdrop_vb);
    assert(SUCCEEDED(hr));
  }

  //
  camera = new Camera();
  camera->pos = glm::vec3(0, 0, 80);
  camera->lookdir = glm::vec3(0, 0, -1);
  camera->up = glm::vec3(0, 1, 0);

  // Directional Light
  dir_light = new DirectionalLight(glm::vec3(-1, -1, -1), glm::vec3(50, 50, 50));

  // Sampler
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
    assert(SUCCEEDED(g_device11->CreateSamplerState(&sd, &sampler)));
  }

  // GBuffer, its SRV and RTV
  {
    D3D11_TEXTURE2D_DESC d2d = {};
    d2d.MipLevels = 1;
    d2d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    d2d.Width = WIN_W;
    d2d.Height = WIN_H;
    d2d.ArraySize = 1;
    d2d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d2d.SampleDesc.Count = 1;
    d2d.SampleDesc.Quality = 0;
    assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, nullptr, &gbuffer)));

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
    srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    assert(SUCCEEDED(g_device11->CreateShaderResourceView(gbuffer, &srv_desc, &srv_gbuffer)));

    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = { };
    rtv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    assert(SUCCEEDED(g_device11->CreateRenderTargetView(gbuffer, &rtv_desc, &rtv_gbuffer)));
  }
}

void DX11ChunksScene::do_Render() {
  float bgcolor[4] = { 1.0f, 1.0f, 0.7f, 1.0f };
  float zero4[] = { 0, 0, 0, 0 };
  g_context11->ClearRenderTargetView(rtv_gbuffer, zero4);
  g_context11->ClearRenderTargetView(g_backbuffer_rtv11, bgcolor);
  g_context11->ClearDepthStencilView(dsv_main, D3D11_CLEAR_DEPTH, 1.0f, 0);
  g_context11->ClearDepthStencilView(dsv_shadowmap, D3D11_CLEAR_DEPTH, 1.0f, 0);
  g_context11->IASetInputLayout(input_layout);
  g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_context11->RSSetScissorRects(1, &g_scissor_rect);
  //g_context11->OMSetDepthStencilState(ds_state, 0);
  g_context11->VSSetShader(vs, nullptr, 0);
  g_context11->PSSetShader(ps, nullptr, 0);

  // Set CBs
  ID3D11Buffer* cbs[] = { g_perobject_cb_default_palette, g_perscene_cb_default_palette };
  g_context11->VSSetConstantBuffers(0, 2, cbs);
  g_context11->PSSetConstantBuffers(1, 1, &g_perscene_cb_default_palette);

  // Depth pass
  g_context11->RSSetViewports(1, &g_viewport_shadowmap);
  g_context11->OMSetRenderTargets(0, nullptr, dsv_shadowmap);
  DirectX::XMMATRIX V, P, PV;
  V = dir_light->GetV_D3D11();
  P = dir_light->GetP_D3D11_DXMath();
  PV = dir_light->GetPV_D3D11();
  DirectX::XMVECTOR cam_pos = camera->GetPos_D3D11();
  DirectX::XMVECTOR dir_light_dir = dir_light->GetDir_D3D11();
  UpdateGlobalPerSceneCB(&dir_light_dir, &PV, &cam_pos);
  UpdateGlobalPerObjectCB(nullptr, &V, &P);
  DirectX::XMMATRIX M = DirectX::XMMatrixTranslation(chunk_pos.x, chunk_pos.y, chunk_pos.z);
  chunk->Render_D3D11(M);

  // Normal pass
  g_context11->RSSetViewports(1, &g_viewport);
  ID3D11RenderTargetView* rtvs[] = { g_backbuffer_rtv11, rtv_gbuffer };
  g_context11->OMSetRenderTargets(2, rtvs, dsv_main);
  V = camera->GetViewMatrix_D3D11();
  P = DirectX::XMMatrixPerspectiveFovLH(60.0f * 3.14159f / 180.0f, WIN_W * 1.0f / WIN_H, 1.0f, 499.0f);
  UpdateGlobalPerObjectCB(nullptr, &V, &P);
  g_context11->PSSetShaderResources(0, 1, &srv_shadowmap);
  g_context11->PSSetSamplers(0, 1, &sampler);
  chunk->Render_D3D11(M);

  M = DirectX::XMMatrixTranslation(0, 0, 0);
  UpdateGlobalPerObjectCB(&M, nullptr, nullptr);
  {
    unsigned stride = sizeof(float) * 6;
    unsigned offset = 0;
    g_context11->IASetVertexBuffers(0, 1, &backdrop_vb, &stride, &offset);
    g_context11->Draw(6, 0);
  }
}

void DX11ChunksScene::Render() {
  do_Render();
  g_swapchain11->Present(1, 0);
}

void DX11ChunksScene::Update(float secs) {
  elapsed_secs += secs;
  chunk_pos.x = 20 * cos(elapsed_secs * 3.14159) - 16;
  chunk_pos.y = 20 * sin(elapsed_secs * 3.14159) - 16;
  chunk_pos.z = 40 * cos(elapsed_secs * 3.14159 * 0.5) + 40;
}

void CreateFullScreenQuadVertexBuffer(ID3D11Buffer** ppbuffer)
// Full scan quad VB
{
  VertexUV verts[] = {
    {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f }, //  
    {  1.0f, -1.0f, 0.0f,  1.0f, 1.0f }, //  +-----------+ UV = (1, 0), NDC=(1, 1)
    { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f }, //  |           |
                                         //  |           |
    { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f }, //  |           |
    { -1.0f,  1.0f, 0.0f,  0.0f, 0.0f }, //  |           |
    {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f }, //  +-----------+ UV = (1, 1), NDC=(1,-1)

    {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f }, //  
    {  1.0f, -1.0f, 0.0f,  1.0f, 1.0f }, //  +-----------+ UV = (1, 0), NDC=(1, 1)
    { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f }, //  |           |
                                         //  |           |
    { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f }, //  |           |
    { -1.0f,  1.0f, 0.0f,  0.0f, 0.0f }, //  |           |
    {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f }, //  +-----------+ UV = (1, 1), NDC=(1,-1)
  };

  D3D11_BUFFER_DESC desc = { };
  desc.ByteWidth = sizeof(verts);
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.StructureByteStride = sizeof(VertexUV);
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

  D3D11_SUBRESOURCE_DATA srd = { };
  srd.pSysMem = verts;

  HRESULT hr = g_device11->CreateBuffer(&desc, &srd, ppbuffer);
  assert(SUCCEEDED(hr));
}

DX11LightScatterScene::DX11LightScatterScene() {
  elapsed_secs = 0;
  // Shaders
  ID3DBlob* error = nullptr;
  UINT compileFlags = 0;
  ID3DBlob* vs_shader_blob;
  HRESULT hr;
  hr = D3DCompileFromFile(L"shaders/shaders_drawlight.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", compileFlags, 0, &vs_shader_blob, &error);
  assert(SUCCEEDED(g_device11->CreateVertexShader(vs_shader_blob->GetBufferPointer(),
    vs_shader_blob->GetBufferSize(), nullptr, &vs_drawlight)));
  CE(hr, error);
  ID3DBlob* ps_shader_blob;
  hr = D3DCompileFromFile(L"shaders/shaders_drawlight.hlsl", nullptr, nullptr, "PSMain", "ps_4_0", compileFlags, 0, &ps_shader_blob, &error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(ps_shader_blob->GetBufferPointer(),
    ps_shader_blob->GetBufferSize(), nullptr, &ps_drawlight)));
  CE(hr, error);

  ID3DBlob* vs_shader_blob_combine, * ps_shader_blob_combine;
  hr = D3DCompileFromFile(L"shaders/shaders_combine.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", compileFlags, 0, &vs_shader_blob_combine, &error);
  if (error) printf("Error compiling VS: %s\n", (char*)error->GetBufferPointer());
  assert(SUCCEEDED(g_device11->CreateVertexShader(vs_shader_blob_combine->GetBufferPointer(),
    vs_shader_blob_combine->GetBufferSize(), nullptr, &vs_combine)));
  CE(hr, error);
  hr = D3DCompileFromFile(L"shaders/shaders_combine.hlsl", nullptr, nullptr, "PSMain", "ps_4_0", compileFlags, 0, &ps_shader_blob_combine, &error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(ps_shader_blob_combine->GetBufferPointer(),
    ps_shader_blob_combine->GetBufferSize(), nullptr, &ps_combine)));
  CE(hr, error);

  // Light map and main canvas
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
    assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, nullptr, &lightmap)));
    assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, nullptr, &main_canvas)));

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    assert(SUCCEEDED(g_device11->CreateShaderResourceView(lightmap, &srv_desc, &srv_lightmask)));
    assert(SUCCEEDED(g_device11->CreateShaderResourceView(main_canvas, &srv_desc, &srv_main_canvas)));

    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    assert(SUCCEEDED(g_device11->CreateRenderTargetView(lightmap, &rtv_desc, &rtv_lightmask)));
    assert(SUCCEEDED(g_device11->CreateRenderTargetView(main_canvas, &rtv_desc, &rtv_main_canvas)));
  }


  // Draw light
  {
    D3D11_BUFFER_DESC buf_desc{};
    buf_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buf_desc.StructureByteStride = sizeof(ConstantBufferDataDrawLight);
    buf_desc.ByteWidth = sizeof(ConstantBufferDataDrawLight);
    buf_desc.Usage = D3D11_USAGE_DYNAMIC;
    buf_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    assert(SUCCEEDED(g_device11->CreateBuffer(&buf_desc, nullptr, &cb_drawlight)));
  }

  CreateFullScreenQuadVertexBuffer(&vb_fsquad);

  // Input layout
  {
    D3D11_INPUT_ELEMENT_DESC inputdesc2[] = {
      { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
      { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc2, 2,
      vs_shader_blob->GetBufferPointer(),
      vs_shader_blob->GetBufferSize(),
      &input_layout)));
  }
}

void DX11LightScatterScene::Render() {
  float bgcolor[4] = { 0.3f, 0.3f, 0.2f, 1.0f };
  float zero4[] = { 0, 0, 0, 0 };
  g_context11->ClearRenderTargetView(rtv_main_canvas, bgcolor);
  g_context11->ClearRenderTargetView(rtv_lightmask, zero4);

  // Draw light mask
  g_context11->RSSetViewports(1, &g_viewport);
  g_context11->OMSetRenderTargets(1, &rtv_lightmask, nullptr);
  g_context11->PSSetConstantBuffers(0, 1, &cb_drawlight);
  g_context11->PSSetShader(ps_drawlight, nullptr, 0);
  g_context11->VSSetShader(vs_drawlight, nullptr, 0);
  UINT zero = 0, stride = sizeof(VertexUV);
  g_context11->IASetInputLayout(input_layout);
  g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_context11->IASetVertexBuffers(0, 1, &vb_fsquad, &stride, &zero);
  g_context11->Draw(6, 0);

  // Combine
  g_context11->ClearRenderTargetView(g_backbuffer_rtv11, zero4);
  g_context11->OMSetRenderTargets(1, &g_backbuffer_rtv11, nullptr);
  g_context11->VSSetShader(vs_combine, nullptr, 0);
  g_context11->PSSetShader(ps_combine, nullptr, 0);
  g_context11->IASetVertexBuffers(0, 1, &vb_fsquad, &stride, &zero);
  ID3D11ShaderResourceView* srvs[] = { srv_main_canvas, srv_lightmask };
  g_context11->PSSetShaderResources(0, 2, srvs);
  g_context11->PSSetConstantBuffers(0, 1, &cb_drawlight);
  g_context11->Draw(6, 0);

  g_swapchain11->Present(1, 0);
}

void DX11LightScatterScene::Update(float secs) {
  elapsed_secs += secs;

  h_cb_drawlight.light_x = WIN_W * 0.5f + 120 * cos(elapsed_secs * 3.14159);
  h_cb_drawlight.light_y = WIN_H * 0.5f + 120 * sin(elapsed_secs * 3.14159);
  h_cb_drawlight.light_r = 100.0f;
  h_cb_drawlight.WIN_H = WIN_H * 1.0f;
  h_cb_drawlight.WIN_W = WIN_W * 1.0f;
  h_cb_drawlight.light_color.m128_f32[0] = 1.0f;
  h_cb_drawlight.light_color.m128_f32[1] = 1.0f;
  h_cb_drawlight.light_color.m128_f32[2] = 1.0f;
  h_cb_drawlight.light_color.m128_f32[3] = 1.0f;
  h_cb_drawlight.global_alpha = 1.0f;

  D3D11_MAPPED_SUBRESOURCE mapped;
  CE(g_context11->Map(cb_drawlight, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
  memcpy(mapped.pData, &h_cb_drawlight, sizeof(ConstantBufferDataDrawLight));
  g_context11->Unmap(cb_drawlight, 0);
}

DX11LightScatterWithChunkScene::DX11LightScatterWithChunkScene(DX11ChunksScene* cs, DX11LightScatterScene* ls) :
  chunk_scene(cs), lightscatter_scene(ls) {
  CreateFullScreenQuadVertexBuffer(&vb_fsquad);

  ID3DBlob* error;
  ID3DBlob* vs_shader_blob_drawlight;
  HRESULT hr;
  UINT compileFlags = 0;
  hr = D3DCompileFromFile(L"shaders/shaders_drawlight_withdepth.hlsl",
    nullptr, nullptr,
    "VSMain", "vs_4_0", compileFlags, 0, &vs_shader_blob_drawlight, &error);
  if (error) {
    printf("Error creating vs: %s\n", error->GetBufferPointer());
  }
  assert(SUCCEEDED(g_device11->CreateVertexShader(vs_shader_blob_drawlight->GetBufferPointer(),
    vs_shader_blob_drawlight->GetBufferSize(), nullptr, &vs_drawlight)));
  CE(hr, error);
  ID3DBlob* ps_shader_blob_drawlight;
  hr = D3DCompileFromFile(L"shaders/shaders_drawlight_withdepth.hlsl",
    nullptr, nullptr,
    "PSMain", "ps_4_0", compileFlags, 0, &ps_shader_blob_drawlight, &error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(ps_shader_blob_drawlight->GetBufferPointer(),
    ps_shader_blob_drawlight->GetBufferSize(), nullptr, &ps_drawlight)));
  CE(hr, error);

  ID3DBlob* vs_shader_blob_combine;
  ID3DBlob* ps_shader_blob_combine;
  hr = D3DCompileFromFile(L"shaders/shaders_combine_withdepth.hlsl",
    nullptr, nullptr,
    "VSMain", "vs_4_0", compileFlags, 0, &vs_shader_blob_combine, &error);
  if (error) {
    printf("Error building vs: %s\n", error->GetBufferPointer());
  }
  assert(SUCCEEDED(g_device11->CreateVertexShader(vs_shader_blob_combine->GetBufferPointer(),
    vs_shader_blob_combine->GetBufferSize(), nullptr, &vs_combine)));

  hr = D3DCompileFromFile(L"shaders/shaders_combine_withdepth.hlsl",
    nullptr, nullptr,
    "PSMain", "ps_4_0", compileFlags, 0, &ps_shader_blob_combine, &error);
  assert(SUCCEEDED(g_device11->CreatePixelShader(ps_shader_blob_combine->GetBufferPointer(),
    ps_shader_blob_combine->GetBufferSize(), nullptr, &ps_combine)));

  // Draw light
  {
    D3D11_BUFFER_DESC buf_desc{};
    buf_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buf_desc.StructureByteStride = sizeof(ConstantBufferDataDrawLightWithZ);
    buf_desc.ByteWidth = sizeof(ConstantBufferDataDrawLightWithZ);
    buf_desc.Usage = D3D11_USAGE_DYNAMIC;
    buf_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    assert(SUCCEEDED(g_device11->CreateBuffer(&buf_desc, nullptr, &cb_drawlight)));
  }

  // Input layout
  {
    D3D11_INPUT_ELEMENT_DESC inputdesc2[] = {
      { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
      { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc2, 2,
      vs_shader_blob_drawlight->GetBufferPointer(),
      vs_shader_blob_drawlight->GetBufferSize(),
      &input_layout)));
  }
  
  // Sampler
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
    assert(SUCCEEDED(g_device11->CreateSamplerState(&sd, &sampler)));
  }
}

void DX11LightScatterWithChunkScene::Render() {
  chunk_scene->do_Render();
  ID3D11RenderTargetView* null_rtvs[] = { nullptr, nullptr, nullptr };
  g_context11->OMSetRenderTargets(2, null_rtvs, nullptr);

  // Draw light
  float zero4[] = { 0, 0, 0, 0 };
  unsigned stride = sizeof(float) * 5;
  unsigned offset = 0;
  g_context11->ClearRenderTargetView(lightscatter_scene->rtv_lightmask, zero4);
  g_context11->IASetInputLayout(input_layout);
  g_context11->IASetVertexBuffers(0, 1, &vb_fsquad, &stride, &offset);
  g_context11->VSSetShader(vs_drawlight, nullptr, 0);
  g_context11->PSSetShader(ps_drawlight, nullptr, 0);
  g_context11->PSSetConstantBuffers(0, 1, &cb_drawlight);
  g_context11->PSSetSamplers(0, 1, &sampler);
  g_context11->PSSetShaderResources(0, 1, &(chunk_scene->srv_gbuffer));
  g_context11->OMSetRenderTargets(1, &(lightscatter_scene->rtv_lightmask), nullptr);
  g_context11->Draw(6, 0);

  // Combine
  g_context11->CopyResource(lightscatter_scene->main_canvas, g_backbuffer);
  g_context11->OMSetRenderTargets(2, null_rtvs, nullptr);

  g_context11->VSSetShader(vs_combine, nullptr, 0);
  g_context11->PSSetShader(ps_combine, nullptr, 0);
  ID3D11ShaderResourceView* srvs[] = {
    lightscatter_scene->srv_main_canvas,
    lightscatter_scene->srv_lightmask,
    chunk_scene->srv_gbuffer
  };
  g_context11->PSSetConstantBuffers(0, 1, &cb_drawlight);
  g_context11->IASetVertexBuffers(0, 1, &vb_fsquad, &stride, &offset);
  g_context11->PSSetSamplers(0, 1, &sampler);
  g_context11->PSSetShaderResources(0, 3, srvs);
  g_context11->OMSetRenderTargets(1, &g_backbuffer_rtv11, nullptr);
  g_context11->Draw(6, 0);

  g_context11->OMSetRenderTargets(3, null_rtvs, nullptr);

  g_swapchain11->Present(1, 0);
}

void DX11LightScatterWithChunkScene::Update(float secs) {
  chunk_scene->Update(secs);

  // Manually calculate the light source's on-screen position
  glm::vec3 world_pos = chunk_scene->chunk_pos + glm::vec3(16, 16, 16);
  world_pos.z *= -1;
  glm::mat4 V = chunk_scene->camera->GetViewMatrix();
  glm::mat4 P = glm::perspective(60.0f * 3.14159f / 180.0f, WIN_W * 1.0f / WIN_H, 0.1f, 499.0f);
  glm::vec4 clip_pos = P * V * glm::vec4(world_pos, 1.0f);
  float x = (clip_pos.x / clip_pos.w + 1.0f) * 0.5f * WIN_W;
  float y = WIN_H - (clip_pos.y / clip_pos.w + 1.0f) * 0.5f * WIN_H;

  glm::vec4 temp1 = glm::vec4(world_pos, 1);
  temp1.x -= 16;
  temp1 = P * V * temp1;
  glm::vec4 temp2 = glm::vec4(world_pos, 1);
  temp2.x += 16;
  temp2 = P * V * temp2;
  float x2 = (temp2.x / temp2.w + 1.0f) * 0.5f * WIN_W;
  float x1 = (temp1.x / temp1.w + 1.0f) * 0.5f * WIN_W;

  h_cb_drawlight.light_x = x;
  h_cb_drawlight.light_y = y;
  h_cb_drawlight.light_r = x2 - x1;
  h_cb_drawlight.WIN_H = WIN_H * 1.0f;
  h_cb_drawlight.WIN_W = WIN_W * 1.0f;
  h_cb_drawlight.light_color.m128_f32[0] = 0.0f;
  h_cb_drawlight.light_color.m128_f32[1] = 1.0f;
  h_cb_drawlight.light_color.m128_f32[2] = 1.0f;
  h_cb_drawlight.light_color.m128_f32[3] = 1.0f;
  h_cb_drawlight.global_alpha = 1.0f;
  h_cb_drawlight.light_z = -world_pos.z;  // GL to DX coord

  D3D11_MAPPED_SUBRESOURCE mapped;
  CE(g_context11->Map(cb_drawlight, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
  memcpy(mapped.pData, &h_cb_drawlight, sizeof(ConstantBufferDataDrawLightWithZ));
  g_context11->Unmap(cb_drawlight, 0);
}

DX11ComputeShaderScene::DX11ComputeShaderScene() {
  D3D11_BUFFER_DESC desc{};
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
  desc.ByteWidth = WIN_W * WIN_H * 4;
  desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  desc.StructureByteStride = 4;

  // Initialize values from the CPU
  unsigned char* init_val = new unsigned char[WIN_W * WIN_H * 4];
  for (int y = 0; y < WIN_H; y++) {
    for (int x = 0; x < WIN_W; x++) {
      int offset = (y * WIN_W + x) * 4;
      init_val[offset] = 256 * (x * 1.0f / WIN_W);
      init_val[offset + 1] = 256 * (y * 1.0f / WIN_H);
      init_val[offset + 2] = 0;
      init_val[offset + 3] = 255;
    }
  }

  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = init_val;

  CE(g_device11->CreateBuffer(&desc, &data, &out_buf));
  delete[] init_val;

  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  CE(g_device11->CreateBuffer(&desc, nullptr, &out_buf_cpu));

  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.FirstElement = 0;
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.Buffer.NumElements = desc.ByteWidth / desc.StructureByteStride;
  CE(g_device11->CreateUnorderedAccessView(out_buf, &uav_desc, &uav_out_buf));

  D3D11_TEXTURE2D_DESC d2ddesc{};
  g_backbuffer->GetDesc(&d2ddesc);
  d2ddesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  d2ddesc.Usage = D3D11_USAGE_DYNAMIC;
  d2ddesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  CE(g_device11->CreateTexture2D(&d2ddesc, nullptr, &backbuffer_staging));

  // Load compute shader
  ID3DBlob* error;
  ID3DBlob* cs_shader_blob;
  HRESULT hr;
  UINT compileFlags = 0;
  hr = D3DCompileFromFile(L"shaders/fill_rectangle.hlsl",
    nullptr, nullptr,
    "CSMain", "cs_4_0", compileFlags, 0, &cs_shader_blob, &error);
  if (error) {
    printf("Error creating cs: %s\n", error->GetBufferPointer());
  }
  assert(SUCCEEDED(g_device11->CreateComputeShader(cs_shader_blob->GetBufferPointer(),
    cs_shader_blob->GetBufferSize(), nullptr, &cs_fillrect)));
  CE(hr, error);

  // ... and its CB
  {
    D3D11_BUFFER_DESC buf_desc{};
    buf_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buf_desc.StructureByteStride = sizeof(ConstantBufferFillRect);
    buf_desc.ByteWidth = std::max(16ULL, sizeof(ConstantBufferFillRect));
    buf_desc.Usage = D3D11_USAGE_DYNAMIC;
    buf_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;


    ConstantBufferFillRect cb{};
    cb.WIN_H = WIN_H;
    cb.WIN_W = WIN_W;
    D3D11_SUBRESOURCE_DATA dsd{};
    dsd.pSysMem = (void*)(&cb);

    assert(SUCCEEDED(g_device11->CreateBuffer(&buf_desc, &dsd, &cb_fillrect)));
  }
}

void DX11ComputeShaderScene::Render() {
  float bgcolor[4] = { 0.7f, 0.7f, 0.4f, 1.0f };
  g_context11->ClearRenderTargetView(g_backbuffer_rtv11, bgcolor);
  
  ID3D11UnorderedAccessView* null_uav = nullptr;
  g_context11->CSSetConstantBuffers(0, 1, &cb_fillrect);
  g_context11->CSSetUnorderedAccessViews(0, 1, &uav_out_buf, nullptr);
  g_context11->CSSetShader(cs_fillrect, nullptr, 0);
  g_context11->Dispatch(WIN_W / 16, WIN_H / 16, 1);
  g_context11->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);

  g_context11->CopyResource(out_buf_cpu, out_buf);
  D3D11_MAPPED_SUBRESOURCE mapped0{};
  CE(g_context11->Map(out_buf_cpu, 0, D3D11_MAP_READ, 0, &mapped0));
  D3D11_MAPPED_SUBRESOURCE mapped1{};
  CE(g_context11->Map(backbuffer_staging, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped1));
  unsigned char* ptr_in = (unsigned char*)mapped0.pData;
  unsigned char* ptr_out = (unsigned char*)mapped1.pData;
  for (int y = 0; y < WIN_H; y++) {
    for (int x = 0; x < WIN_W; x++) {
      int idx_out = y * mapped1.RowPitch + x * 4;
      int idx_in = (y * WIN_W + x) * 4;
      for (int i = 0; i < 4; i++) {
        ptr_out[idx_out + i] = ptr_in[idx_in + i];
      }
    }
  }

  g_context11->Unmap(backbuffer_staging, 0);
  g_context11->Unmap(out_buf_cpu, 0);

  g_context11->CopyResource(g_backbuffer, backbuffer_staging);

  g_swapchain11->Present(1, 0);
}

void DX11ComputeShaderScene::Update(float secs) {}