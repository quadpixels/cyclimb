#include <stdio.h>

#include <Windows.h>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>

#include "scene.hpp"
#include "util.hpp"

ID3D12Device5* g_device12;
IDXGIFactory4* g_factory;
ID3D12CommandQueue* g_command_queue;
IDXGISwapChain3* g_swapchain;
ID3D12DescriptorHeap* g_rtv_heap;  // RTV heap for the swapchain's RTs
int g_rtv_descriptor_size;
const int FRAME_COUNT = 2;
ID3D12Resource* g_rendertargets[FRAME_COUNT];

int WIN_W = 512, WIN_H = 512;
HWND g_hwnd;
static long long g_last_ms;
static Scene* g_scenes[2];
static int g_scene_idx = 0;
ID3D12Fence* g_fence;
int g_fence_value = 0;
HANDLE g_fence_event;
int g_frame_index;
void WaitForPreviousFrame();
bool g_use_debug_layer = false;

// Override the following functions for DX12 and to make the compiler happy
GraphicsAPI g_api = GraphicsAPI::ClimbD3D12;
bool IsGL() { return false; }
bool IsD3D11() { return false; }
bool IsD3D12() { return true; }
void UpdateGlobalPerObjectCB(const DirectX::XMMATRIX* M, const DirectX::XMMATRIX* V, const DirectX::XMMATRIX* P) {}
ID3D11Device* g_device11;
ID3D11DeviceContext* g_context11;
ID3D11VertexShader* g_vs_simpletexture;
ID3D11PixelShader* g_ps_simpletexture;
ID3DBlob* g_vs_textrender_blob, * g_ps_textrender_blob;
ID3D11BlendState* g_blendstate11;
ID3D11Buffer* g_simpletexture_cb;
int g_font_size;
ID3D11VertexShader* g_vs_textrender;
ID3D11PixelShader* g_ps_textrender;
GLuint g_programs[10];

long long MillisecondsNow() {
  static LARGE_INTEGER s_frequency;
  static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
  if (s_use_qpc) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (1000LL * now.QuadPart) / s_frequency.QuadPart;
  }
  else {
    return GetTickCount();
  }
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
    g_scene_idx = 1;
    break;
  }
  default: break;
  }
}

void InitDeviceAndCommandQ() {
  unsigned dxgi_factory_flags = 0;
  bool use_warp_device = false;

  if (g_use_debug_layer) {
    ID3D12Debug* debug_controller;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
      debug_controller->EnableDebugLayer();
      dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
      printf("Enabling debug layer\n");
    }
  }

  CE(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&g_factory)));
  if (use_warp_device) {
    IDXGIAdapter* warp_adapter;
    CE(g_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)));
    CE(D3D12CreateDevice(warp_adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&g_device12)));
    printf("Created a WARP device=%p\n", g_device12);;
  }
  else {
    IDXGIAdapter1* hw_adapter;
    for (int idx = 0; g_factory->EnumAdapters1(idx, &hw_adapter) != DXGI_ERROR_NOT_FOUND; idx++) {
      DXGI_ADAPTER_DESC1 desc;
      hw_adapter->GetDesc1(&desc);
      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
      else {
        CE(D3D12CreateDevice(hw_adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&g_device12)));
        printf("Created a hardware device = %p\n", g_device12);
        break;
      }
    }
  }

  // Check raytracing support
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
  assert(SUCCEEDED(g_device12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
    &options5, sizeof(options5))));
  if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0) {
    printf("This device supports RayTracing.\n");
  }

  assert(g_device12 != nullptr);

  {
    D3D12_COMMAND_QUEUE_DESC desc = {
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    };
    CE(g_device12->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_command_queue)));
  }

  CE(g_device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
  g_fence_value = 1;
  g_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void InitSwapchain() {
  {
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FRAME_COUNT;
    swapChainDesc.Width = WIN_W;
    swapChainDesc.Height = WIN_H;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    CE(g_factory->CreateSwapChainForHwnd(
      g_command_queue, g_hwnd, &swapChainDesc, nullptr, nullptr,
      (IDXGISwapChain1**)(&g_swapchain)));
    printf("Created swapchain.\n");
  }

  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = FRAME_COUNT,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };
    CE(g_device12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_rtv_heap)));
    printf("Created RTV heap.\n");

    g_rtv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(g_rtv_heap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < FRAME_COUNT; i++) {
      CE(g_swapchain->GetBuffer(i, IID_PPV_ARGS(&g_rendertargets[i])));
      wchar_t buf[100];
      _snwprintf_s(buf, sizeof(buf), L"Render target #%d", i);
      g_rendertargets[i]->SetName(buf);

      g_device12->CreateRenderTargetView(g_rendertargets[i], nullptr, rtv_handle);
      rtv_handle.Offset(g_rtv_descriptor_size);
    }
    printf("Created backbuffers' RTVs\n");
  }

  CE(g_factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER));
  WaitForPreviousFrame();
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
  case WM_PAINT: {
    long long ms = MillisecondsNow();
    Scene* scene = g_scenes[g_scene_idx];
    if (scene) {
      scene->Update((ms - g_last_ms) / 1000.0f);
      scene->Render();
    }
    g_last_ms = ms;
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

  LPCSTR window_name = "ChaoyueClimb (DXR)";
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

int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "debug")) {
      g_use_debug_layer = true;
    }
    else if (!strcmp(argv[i], "-w") && argc > i + 1) {
      WIN_W = std::atoi(argv[i + 1]);
      printf("WIN_W set to %d\n", WIN_W);
    }
    else if (!strcmp(argv[i], "-h") && argc > i + 1) {
      WIN_H = std::atoi(argv[i + 1]);
      printf("WIN_H set to %d\n", WIN_H);
    }
  }

  CreateCyclimbWindow();
  InitDeviceAndCommandQ();
  InitSwapchain();
  ShowWindow(g_hwnd, SW_RESTORE);

  g_scenes[0] = new ObjScene();
  g_scenes[1] = new TriangleScene();

  // Main message loop
  g_last_ms = MillisecondsNow();
  MSG msg = { 0 };
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return 0;
}