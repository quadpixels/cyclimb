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
}

void DX11ChunksScene::Render() {
  float bgcolor[4] = { 1.0f, 1.0f, 0.7f, 1.0f };
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
  DirectX::XMMATRIX M = DirectX::XMMatrixTranslation(0, 0, 0);
  chunk->Render_D3D11(M);

  // Normal pass
  g_context11->RSSetViewports(1, &g_viewport);
  g_context11->OMSetRenderTargets(1, &g_backbuffer_rtv11, dsv_main);
  V = camera->GetViewMatrix_D3D11();
  P = DirectX::XMMatrixPerspectiveFovLH(60.0f * 3.14159f / 180.0f, WIN_W * 1.0f / WIN_H, 1.0f, 499.0f);
  UpdateGlobalPerObjectCB(nullptr, &V, &P);
  g_context11->PSSetShaderResources(0, 1, &srv_shadowmap);
  g_context11->PSSetSamplers(0, 1, &sampler);
  chunk->Render_D3D11(M);

  {
    unsigned stride = sizeof(float) * 6;
    unsigned offset = 0;
    g_context11->IASetVertexBuffers(0, 1, &backdrop_vb, &stride, &offset);
    g_context11->Draw(6, 0);
  }

  g_swapchain11->Present(1, 0);
}

void DX11ChunksScene::Update(float secs) {

}