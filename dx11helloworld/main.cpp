#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

#include <Windows.h>
#include <stdio.h>
#include <d3d11.h>

#undef max
#undef min

HWND g_hwnd;
int WIN_W = 800, WIN_H = 480;
int g_scene_idx = 0;

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
  case '2': {
    printf("Current scene set to 2\n");
    g_scene_idx = 2; break;
  }
  case '3': {
    printf("Current scene set to 3\n");
    g_scene_idx = 3; break;
  }
  default: break;
  }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
  switch (message)
  {
  case WM_KEYDOWN:
    OnKeyDown(wparam, lparam);
    break;
  case WM_KEYUP:
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  case WM_PAINT:
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(g_hwnd, &pt);
    printf("Mouse position in client (%d,%d)\n", pt.x, pt.y);
    break;
  default:
    return DefWindowProc(hwnd, message, wparam, lparam);
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

  LPCSTR window_name = "ChaoyueClimb (D3D11)";
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
  CreateCyclimbWindow();
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