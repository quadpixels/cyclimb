#include "scene.hpp"
#include <assert.h>
#include <stdio.h>

#include <d3d11.h>
#include <d3dcompiler.h>

extern ID3D11Device* g_device11;
extern ID3D11DeviceContext* g_context11;
extern ID3D11RenderTargetView* g_backbuffer_rtv11;
extern IDXGISwapChain* g_swapchain11;
extern D3D11_VIEWPORT g_viewport;
extern D3D11_RECT g_scissor_rect;

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
}

void DX11HelloTriangleScene::Render() {
  float bgcolor[4] = { 0.1f, 0.1f, 0.4f, 1.0f };
  g_context11->ClearRenderTargetView(g_backbuffer_rtv11, bgcolor);
  g_context11->IASetInputLayout(input_layout);
  g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  unsigned stride = sizeof(float) * 7, offset = 0;
  g_context11->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
  g_context11->VSSetShader(vs, nullptr, 0);
  g_context11->PSSetShader(ps, nullptr, 0);
  g_context11->RSSetViewports(1, &g_viewport);
  g_context11->RSSetScissorRects(1, &g_scissor_rect);
  g_context11->OMSetRenderTargets(1, &g_backbuffer_rtv11, nullptr);
  g_context11->Draw(3, 0);
  g_swapchain11->Present(1, 0);
}

void DX11HelloTriangleScene::Update(float secs) { }