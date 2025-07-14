// 2025-07-11
// DXR Questions

#include <stdio.h>
#include <Windows.h>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>

ID3D12Device5* g_device12{};
IDXGIFactory4* g_factory{};
ID3D12CommandQueue* g_command_queue;
IDXGISwapChain3* g_swapchain;
ID3D12DescriptorHeap* g_rtv_heap;  // RTV heap for the swapchain's RTs
int g_rtv_descriptor_size;
const int FRAME_COUNT = 2;
int WIN_W = 512, WIN_H = 512;
ID3D12Resource* g_rendertargets[FRAME_COUNT];
HWND g_hwnd{};

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_CREATE:
  {
    LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
    SetWindowLongPtr(g_hwnd, GWLP_USERDATA, (LONG_PTR)pCreateStruct->lpCreateParams);
    break;
  }
  case WM_KEYDOWN:
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
void CreateMyWindow() {
  AllocConsole();
  freopen_s((FILE**)stdin, "CONIN$", "r", stderr);
  freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
  freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

  WNDCLASS windowClass = { 0 };
  windowClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
  windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
  windowClass.hInstance = NULL;
  windowClass.lpfnWndProc = WndProc;
  windowClass.lpszClassName = L"Window in Console"; //needs to be the same name
  //when creating the window as well
  windowClass.style = CS_HREDRAW | CS_VREDRAW;

  LPCWSTR window_name = L"DxrQuestions";
  LPCWSTR class_name = L"DxrQuestions_class";
  HINSTANCE hinstance = GetModuleHandle(nullptr);

  if (!RegisterClass(&windowClass)) {
    printf("Cannot register window class\n");
  }

  g_hwnd = CreateWindowW(
    windowClass.lpszClassName,
    window_name,
    WS_OVERLAPPEDWINDOW,
    16,
    16,
    WIN_W, WIN_H,
    nullptr, nullptr,
    hinstance, nullptr);

  ShowWindow(g_hwnd, SW_RESTORE);
}

int main() {
  CreateMyWindow();
  MSG msg = { 0 };
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  return 0;
}