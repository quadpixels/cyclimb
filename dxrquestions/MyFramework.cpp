#include "MyFramework.h"

#include <assert.h>
#include <stdio.h>

#include <source_location>
#include <stdexcept>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>

#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_CREATE:
  {
    LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pCreateStruct->lpCreateParams);
    break;
  }
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) {
      PostQuitMessage(0);
      return 0;
    }
    return 0;
  case WM_KEYUP:
    return 0;
  case WM_PAINT: {
    return 0;
  }
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

void MyFramework::InitWindow() {
  WNDCLASS windowClass = { 0 };
  windowClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
  windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
  windowClass.hInstance = NULL;
  windowClass.lpfnWndProc = WndProc;
  windowClass.lpszClassName = L"Window in Console"; //needs to be the same name
  //when creating the window as well
  windowClass.style = CS_HREDRAW | CS_VREDRAW;

  LPCWSTR window_name = L"My Framework!";
  LPCWSTR class_name = L"MyFramework_class";
  HINSTANCE hinstance = GetModuleHandle(nullptr);

  if (!RegisterClass(&windowClass)) {
    printf("Cannot register window class\n");
  }

  hwnd = CreateWindowW(
    windowClass.lpszClassName,
    window_name,
    WS_OVERLAPPEDWINDOW,
    16,
    16,
    WIN_W, WIN_H,
    nullptr, nullptr,
    hinstance, nullptr);

  ShowWindow(hwnd, SW_RESTORE);
}

void MyFramework::InitDeviceAndCommandQ() {
  uint32_t dxgi_factory_flags = 0;
  bool use_warp_device = false;
#ifndef NDEBUG
  const bool use_debug_layer = true;
#else
  const bool use_debug_layer = false;
#endif

  if (use_debug_layer) {
    ID3D12Debug* debug_controller;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
      debug_controller->EnableDebugLayer();
      dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
      printf("Enabling debug layer\n");

      ID3D12Debug1* debug_controller1;
      debug_controller->QueryInterface(IID_PPV_ARGS(&debug_controller1));
      if (debug_controller1) {
        printf("Enabling GPU-based validation\n");
        debug_controller1->SetEnableGPUBasedValidation(true);
      }
    }
  }

  CE(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&dxgi_factory)));

  if (use_warp_device) {
    IDXGIAdapter* warp_adapter;
    CE(dxgi_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)));
    CE(D3D12CreateDevice(warp_adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device12)));
    printf("Created a WARP device=%p\n", device12);
  }
  else {
    IDXGIAdapter1* hw_adapter;
    for (int idx = 0; dxgi_factory->EnumAdapters1(idx, &hw_adapter) != DXGI_ERROR_NOT_FOUND; idx++) {
      DXGI_ADAPTER_DESC1 desc;
      hw_adapter->GetDesc1(&desc);
      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
      else {
        CE(D3D12CreateDevice(hw_adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device12)));
        printf("Created a hardware device = %p, %ls\n", device12, desc.Description);
        break;
      }
    }
  }

  // Check raytracing support
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
  assert(SUCCEEDED(device12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
    &options5, sizeof(options5))));
  if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0) {
    printf("This device supports DXR 1.0.\n");
  }
  if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1) {
    printf("This device supports DXR 1.1.\n");
  }

  assert(device12 != nullptr);

  D3D12_COMMAND_QUEUE_DESC desc = {
    .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
    .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
  };
  CE(device12->CreateCommandQueue(&desc, IID_PPV_ARGS(&command_queue)));

  CE(device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  fence_value = 1;
  fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}