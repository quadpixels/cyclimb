#include <assert.h>
#include <stdio.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <wrl/client.h>

#include "scene.hpp"
#include "utils.hpp"

unsigned WIN_W = 800, WIN_H = 480;
const int FRAME_COUNT = 2;
HWND g_hwnd;

ID3D12Device* g_device;
IDXGIFactory4* g_factory;
ID3D12CommandQueue* g_command_queue;
ID3D12Fence* g_fence;
int g_fence_value = 0;
HANDLE g_fence_event;
IDXGISwapChain3* g_swapchain;
ID3D12DescriptorHeap* g_rtv_heap;
ID3D12Resource* g_rendertargets[FRAME_COUNT];
unsigned g_rtv_descriptor_size;
int g_frame_index;

static Scene* g_scenes[2];
static int g_scene_idx = 0;

// Shared across all scenes
void InitDeviceAndCommandQ() {
  unsigned dxgi_factory_flags = 0;
  bool use_warp_device = false;

  ID3D12Debug* debug_controller;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
    debug_controller->EnableDebugLayer();
    dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
    printf("Enabling debug layer\n");
  }

  CE(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&g_factory)));
  if (use_warp_device) {
    IDXGIAdapter* warp_adapter;
    CE(g_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)));
    CE(D3D12CreateDevice(warp_adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)));
    printf("Created a WARP device=%p\n", g_device);;
  }
  else {
    IDXGIAdapter1* hw_adapter;
    for (int idx = 0; g_factory->EnumAdapters1(idx, &hw_adapter) != DXGI_ERROR_NOT_FOUND; idx++) {
      DXGI_ADAPTER_DESC1 desc;
      hw_adapter->GetDesc1(&desc);
      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
      else {
        CE(D3D12CreateDevice(hw_adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)));
        printf("Created a hardware device = %p\n", g_device);
        break;
      }
    }
  }

  assert(g_device != nullptr);

  {
    D3D12_COMMAND_QUEUE_DESC desc = {
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    };
    CE(g_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_command_queue)));
  }

  CE(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
  g_fence_value = 1;
  g_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void InitSwapChain() {
  DXGI_SWAP_CHAIN_DESC1 desc = {
    .Width = WIN_W,
    .Height = WIN_H,
    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
    .SampleDesc = {
      .Count = 1,
    },
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    .BufferCount = 2,
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
  };
  IDXGISwapChain1* swapchain1;
  CE(g_factory->CreateSwapChainForHwnd(g_command_queue,
    g_hwnd, &desc, nullptr, nullptr, &swapchain1));
  g_swapchain = (IDXGISwapChain3*)swapchain1;
  CE(g_factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER));
  g_frame_index = g_swapchain->GetCurrentBackBufferIndex();

  printf("Created swapchain.\n");

  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = 2,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };
    CE(g_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_rtv_heap)));
    printf("Created RTV heap.\n");
  }

  g_rtv_descriptor_size = g_device->GetDescriptorHandleIncrementSize(
    D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(g_rtv_heap->GetCPUDescriptorHandleForHeapStart());
  for (int i = 0; i < FRAME_COUNT; i++) {
    CE(g_swapchain->GetBuffer(i, IID_PPV_ARGS(&g_rendertargets[i])));
    g_device->CreateRenderTargetView(g_rendertargets[i], nullptr, rtv_handle);
    rtv_handle.Offset(1, g_rtv_descriptor_size);

    wchar_t buf[100];
    _snwprintf_s(buf, sizeof(buf), L"Render Target Frame %d", i);
    g_rendertargets[i]->SetName(buf);
  }
  printf("Created RTV and pointed RTVs to backbuffers.\n");
}

void WaitForPreviousFrame() {
  int value = g_fence_value++;
  CE(g_command_queue->Signal(g_fence, value));
  if (g_fence->GetCompletedValue() < value) {
    CE(g_fence->SetEventOnCompletion(value, g_fence_event));
    CE(WaitForSingleObject(g_fence_event, INFINITE));
  }
  g_frame_index = g_swapchain->GetCurrentBackBufferIndex();
}

void OnKeyDown(WPARAM wParam, LPARAM lParam) {
  if (lParam & 0x40000000) return;
  switch (wParam) {
  case VK_ESCAPE:
    PostQuitMessage(0);
    break;
  case '0': {
    printf("Current scene set to 0\n");
    g_scene_idx = 0; break;
  }
  case '1': {
    printf("Current scene set to 1\n");
    g_scene_idx = 1; break;
  }
  default: break;
  }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_CREATE:
  {
    LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
    SetWindowLongPtr(g_hwnd, GWLP_USERDATA, (LONG_PTR)pCreateStruct->lpCreateParams);
    break;
  }
  case WM_KEYDOWN:
    OnKeyDown(wParam, lParam);
    return 0;
  case WM_PAINT:
    g_scenes[g_scene_idx]->Render();
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
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

  LPCSTR window_name = "ChaoyueClimb (D3D12)";
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
}

int main() {
  AllocConsole();
  freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
  freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
  
  CreateCyclimbWindow();
  ShowWindow(g_hwnd, SW_RESTORE);

  InitDeviceAndCommandQ();
  InitSwapChain();
  g_scenes[0] = new DX12ClearScreenScene();
  g_scenes[1] = new DX12HelloTriangleScene();

  // Main message loop
  MSG msg = { 0 };
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return 0;
}