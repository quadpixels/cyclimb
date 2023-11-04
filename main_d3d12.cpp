#pragma comment(lib, "d3d12.lib")

#include <Windows.h>
#undef max
#undef min

#include <d3d12.h>

#include "testshapes.hpp"
#include "scene.hpp"
#include "textrender.hpp"
#include <DirectXMath.h>

extern GraphicsAPI g_api;

extern int WIN_W, WIN_H, SHADOW_RES;

extern bool init_done;
extern HWND g_hwnd;
extern void CreateCyclimbWindow();
extern ClimbScene* g_climbscene;
extern bool g_main_menu_visible;
extern MainMenu* g_mainmenu;
extern Particles* g_particles;
extern ChunkGrid* g_chunkgrid[];

void Render_D3D12() {
}

void MyInit_D3D12() {
  g_chunkgrid[3] = new ChunkGrid(1, 1, 1);
  g_chunkgrid[3]->SetVoxel(0, 0, 0, 12);

  Particles::InitStatic(g_chunkgrid[3]);
  ClimbScene::InitStatic();
  g_climbscene = new ClimbScene();
  g_climbscene->Init();

  g_mainmenu = new MainMenu();
}

int main_d3d12(int argc, char** argv) {
  CreateCyclimbWindow();
  MyInit_D3D12();

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