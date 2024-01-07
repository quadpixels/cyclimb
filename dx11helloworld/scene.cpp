#include "scene.hpp"

#include <d3d11.h>

extern ID3D11DeviceContext* g_context11;
extern ID3D11RenderTargetView* g_backbuffer_rtv11;
extern IDXGISwapChain* g_swapchain11;

void DX11ClearScreenScene::Render() {
  float bgcolor[4] = { 0.1f, 0.1f, 0.4f, 1.0f };
  g_context11->ClearRenderTargetView(g_backbuffer_rtv11, bgcolor);
  g_swapchain11->Present(1, 0);
}

void DX11ClearScreenScene::Update(float secs) {}