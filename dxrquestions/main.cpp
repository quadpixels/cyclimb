// 2025-07-11
// DXR Questions

#include <stdexcept>
#include <source_location>
#include <stdio.h>
#include <Windows.h>

#include <d3dcompiler.h>
#include <dxcapi.h>
#include "../dx12helloworld/dxcapi.use.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <nvapi.h>

#include "MyFramework.h"
#include "MyScene.h"
#include "textrender1.hpp"

#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}

static dxc::DxcDllSupport g_dxc_support;
namespace dxc {
  char const* kDxCompilerLib = "dxcompiler";
}

MyFramework* g_myframework{};
MyScene* g_scenes[4];
uint32_t g_scene_idx = 1;
HWND g_hwnd{};
long long g_last_ms{ 0 };
static bool g_init_done = false;

void InitDX12Stuff() {
  g_dxc_support.Initialize();
  IDxcCompiler* dxc_compiler;
  IDxcOperationResult* dxc_opr_result;
  CE(g_dxc_support.CreateInstance(CLSID_DxcCompiler, &dxc_compiler));

  g_myframework = new MyFramework();
  g_myframework->InitWindow();
  g_myframework->InitDeviceAndCommandQ();
  g_myframework->InitSwapchain();
}

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

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_CREATE:
  {
    LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
    SetWindowLongPtr(g_hwnd, GWLP_USERDATA, (LONG_PTR)pCreateStruct->lpCreateParams);
    break;
  }
  case WM_KEYDOWN: {
    if (wParam == VK_ESCAPE) {
      exit(0);
    }
    else if (wParam >= '1' && wParam < '1' + _countof(g_scenes)) {
      g_scene_idx = wParam - '1';
    }
    else {
      MyScene* scene = g_scenes[g_scene_idx];
      scene->OnKeyDown(wParam);
    }
    return 0;
  }
  case WM_KEYUP: {
    MyScene* scene = g_scenes[g_scene_idx];
    scene->OnKeyUp(wParam);
    return 0;
  }
  case WM_PAINT: {
    if (!g_init_done) return 0;
    long long ms = MillisecondsNow();
    MyScene* scene = g_scenes[g_scene_idx];
    if (scene) {
      if (g_last_ms > 0) {
        scene->Update((ms - g_last_ms) / 1000.0f);
        scene->Render();
      }
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

  LPCWSTR window_name = L"DXR Questions";
  LPCSTR class_name = "DXR_Questions_class";
  HINSTANCE hinstance = GetModuleHandle(nullptr);

  if (!RegisterClass(&windowClass)) {
    printf("Cannot register window class\n");
  }

  g_hwnd = CreateWindow(
    windowClass.lpszClassName,
    window_name,
    WS_OVERLAPPEDWINDOW,
    16,
    16,
    MyFramework::WIN_W, MyFramework::WIN_H,
    nullptr, nullptr,
    hinstance, nullptr);
}

int main() {
  InitDX12Stuff();
  g_myframework->InitNVAPI();
#ifndef NDEBUG
  g_myframework->InitRayTracingValidation();
#endif
  g_scenes[0] = new MyParisIvyLeafScene(g_myframework);
  g_scenes[1] = new MyLssScene(g_myframework);
  g_scenes[2] = new MyInstanceFlagScene(g_myframework);
  g_scenes[3] = new MyCullingScene(g_myframework);
  g_init_done = true;

  MSG msg = { 0 };
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  return 0;
}