#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")


#include <combaseapi.h>
#include <Windows.h>
#include <stdio.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_3.h>

#include "scene_dx11.hpp"
#include "util.hpp"

#include <glfw/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW\/glfw3native.h>

#undef max
#undef min

HWND g_hwnd;
GLFWwindow* g_window{};
int WIN_W = 800, WIN_H = 480, SHADOW_RES = 512;
int g_scene_idx = 0;

// Override the following functions for DX11
GraphicsAPI g_api = GraphicsAPI::ClimbD3D11;
bool IsGL() { return false; }
bool IsD3D11() { return true; }
bool IsD3D12() { return false; }

IDXGIFactory2* g_dxgi_factory;
ID3D11Device* g_device11;
ID3D11DeviceContext* g_context11;
ID3D12Device* g_device12;

// Shaders ..
ID3DBlob* g_vs_default_palette_blob, * g_ps_default_palette_blob;
ID3DBlob* g_ps_default_palette_shadowed_blob;
ID3DBlob* g_vs_textrender_blob, * g_ps_textrender_blob;
ID3DBlob* g_vs_light_blob, * g_ps_light_blob;
ID3DBlob* g_vs_simpletexture_blob, * g_ps_simpletexture_blob;
ID3D11VertexShader* g_vs_default_palette;
ID3D11VertexShader* g_vs_textrender;
ID3D11VertexShader* g_vs_light;
ID3D11VertexShader* g_vs_simpletexture;
ID3D11PixelShader* g_ps_default_palette;
ID3D11PixelShader* g_ps_default_palette_shadowed;
ID3D11PixelShader* g_ps_textrender;
ID3D11PixelShader* g_ps_light;
ID3D11PixelShader* g_ps_simpletexture;
ID3D11BlendState* g_blendstate11;
ID3D11Buffer* g_simpletexture_cb;
IDXGISwapChain1* g_swapchain11;
ID3D11Texture2D* g_backbuffer;
ID3D11RenderTargetView* g_backbuffer_rtv11;

D3D11_VIEWPORT g_viewport, g_viewport_shadowmap;
D3D11_RECT g_scissor_rect;

static Scene* g_scenes[5];
static int g_scene_index = 0;

// DX11路径里，这两个CB已经硬编码成全局变量了 :(
ID3D11Buffer* g_perobject_cb_default_palette;
ID3D11Buffer* g_perscene_cb_default_palette;
struct DefaultPalettePerObjectCB {
  DirectX::XMMATRIX M, V, P;
};
struct DefaultPalettePerObjectCB g_perobject_cb;

// 
void UpdateGlobalPerObjectCB(const DirectX::XMMATRIX* M, const DirectX::XMMATRIX* V, const DirectX::XMMATRIX* P) {
  D3D11_MAPPED_SUBRESOURCE mapped;
  assert(SUCCEEDED(g_context11->Map(g_perobject_cb_default_palette, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  if (M) g_perobject_cb.M = *M;
  if (V) g_perobject_cb.V = *V;
  if (P) g_perobject_cb.P = *P;
  memcpy(mapped.pData, &g_perobject_cb, sizeof(g_perobject_cb));
  g_context11->Unmap(g_perobject_cb_default_palette, 0);
}

struct DefaultPalettePerSceneCB g_perscene_cb;
void UpdateGlobalPerSceneCB(const DirectX::XMVECTOR* dir_light, const DirectX::XMMATRIX* lightPV, const DirectX::XMVECTOR* camPos) {
  D3D11_MAPPED_SUBRESOURCE mapped;
  assert(SUCCEEDED(g_context11->Map(g_perscene_cb_default_palette, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  if (dir_light) g_perscene_cb.dir_light = *dir_light;
  if (lightPV) g_perscene_cb.lightPV = *lightPV;
  if (camPos)  g_perscene_cb.cam_pos = *camPos;

  memcpy(mapped.pData, &g_perscene_cb, sizeof(g_perscene_cb));
  g_context11->Unmap(g_perscene_cb_default_palette, 0);
}

#include "chunk.hpp"

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
static long long g_last_ms;

static void Render_D3D11() {
  long long ms = MillisecondsNow();
  Scene* s = g_scenes[g_scene_idx];
  s->Update((ms - g_last_ms) / 1000.0f);
  s->Render();
  g_last_ms = ms;
}

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  switch (key) {
  case GLFW_KEY_0: {
    printf("Current scene set to 0\n");
    g_scene_idx = 0; break;
  }
  case GLFW_KEY_1: {
    printf("Current scene set to 1\n");
    g_scene_idx = 1; break;
  }
  case GLFW_KEY_2: {
    printf("Current scene set to 2\n");
    g_scene_idx = 2; break;
  }
  case GLFW_KEY_3: {
    printf("Current scene set to 3\n");
    g_scene_idx = 3; break;
  }
  case GLFW_KEY_4: {
    printf("Current scene set to 4\n");
    g_scene_idx = 4; break;
  }
  default: break;
  }
}

void CreateCyclimbWindow() {
  AllocConsole();
  freopen_s((FILE**)stdin, "CONIN$", "r", stderr);
  freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
  freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

  if (!glfwInit()) {
    printf("GLFW initialization failed\n");
  }
  printf("GLFW inited.\n");
  GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* video_mode = glfwGetVideoMode(primary_monitor);
  printf("Video mode of primary monitor is %dx%d\n",
    video_mode->width, video_mode->height);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  g_window = glfwCreateWindow(
    WIN_W, WIN_H,
    "DX11HelloWorld",
    nullptr,
    nullptr);
  assert(g_window);

  glfwSetKeyCallback(g_window, KeyCallback);
}

void InitDX11() {
  HRESULT hr{};
  CE(CreateDXGIFactory1(IID_PPV_ARGS(&g_dxgi_factory)));
  D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };

  CE(D3D11CreateDevice(
    nullptr,
    D3D_DRIVER_TYPE::D3D_DRIVER_TYPE_HARDWARE,
    nullptr,
    0,
    feature_levels,
    _countof(feature_levels),
    D3D11_SDK_VERSION,
    &g_device11,
    nullptr,
    &g_context11));

  DXGI_SWAP_CHAIN_DESC1 scd = { };
  scd.Width = WIN_W;
  scd.Height = WIN_H;
  scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  scd.SampleDesc.Count = 1;
  scd.SampleDesc.Quality = 0;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.BufferCount = 2;
  scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  scd.Scaling = DXGI_SCALING::DXGI_SCALING_STRETCH;
  scd.Flags = 0;

  DXGI_SWAP_CHAIN_FULLSCREEN_DESC scfsd{};
  scfsd.Windowed = true;

  CE(g_dxgi_factory->CreateSwapChainForHwnd(
    g_device11,
    glfwGetWin32Window(g_window),
    &scd,
    &scfsd,
    nullptr,
    &g_swapchain11));

  hr = g_swapchain11->GetBuffer(0, IID_PPV_ARGS(&g_backbuffer));
  assert(SUCCEEDED(hr));
  hr = g_device11->CreateRenderTargetView(g_backbuffer, nullptr, &g_backbuffer_rtv11);
  assert(SUCCEEDED(hr));

  // CBs for Chunk
  {
    D3D11_BUFFER_DESC buf_desc = { };
    buf_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buf_desc.StructureByteStride = sizeof(DefaultPalettePerObjectCB);
    buf_desc.ByteWidth = sizeof(DefaultPalettePerObjectCB);
    buf_desc.Usage = D3D11_USAGE_DYNAMIC;
    buf_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    assert(SUCCEEDED(g_device11->CreateBuffer(&buf_desc, nullptr, &g_perobject_cb_default_palette)));

    buf_desc.StructureByteStride = sizeof(DefaultPalettePerSceneCB);
    buf_desc.ByteWidth = sizeof(DefaultPalettePerSceneCB);
    assert(SUCCEEDED(g_device11->CreateBuffer(&buf_desc, nullptr, &g_perscene_cb_default_palette)));
  }

  g_viewport.Height = WIN_H;
  g_viewport.Width = WIN_W;
  g_viewport.TopLeftX = 0;
  g_viewport.TopLeftY = 0;
  g_viewport.MinDepth = 0;
  g_viewport.MaxDepth = 1; // Need to set otherwise depth will be all -1's

  g_viewport_shadowmap = g_viewport;
  g_viewport_shadowmap.Height = SHADOW_RES;
  g_viewport_shadowmap.Width = SHADOW_RES;

  g_scissor_rect.left = 0;
  g_scissor_rect.top = 0;
  g_scissor_rect.bottom = WIN_H;
  g_scissor_rect.right = WIN_W;
};

// Mimik MVP transformation on the CPU side
void MyTest() {
  glm::vec4 world_pos(16.5, 15.5, -5.5, 1);

  // Column major of this view matrix
  // | 1.00, 0.00, 0.00,  0.00 |
  // | 0.00, 1.00, 0.00,  0.00 |
  // | 0.00, 0.00, 1.00, 80.00 |
  // | 0.00, 0.00, 0.00,  1.00 |
  glm::mat4 v(1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 80, 1);

  // Column major of this projection matrix
  // | 1.03923, 0.00,    0.00,     0.00    |
  // | 0.00,    1.73205, 0.00,     0.00    |
  // | 0.00,    0.00,    1.00201, -1.00201 |
  // | 0.00,    0.00,    1.00,     0.00    |
  glm::mat4 p(1.03923, 0, 0, 0,
    0, 1.73205, 0, 0,
    0, 0, 1.00201, 1,
    0, 0, -1.00201, 0);

  glm::mat4 p1 = glm::perspective(60.0f * 3.14159f / 180.0f, WIN_W * 1.0f / WIN_H, 0.1f, 499.0f);

  glm::vec4 clip_pos1 = p * v * world_pos;
  glm::vec4 clip_pos2 = p1 * v * world_pos;  // 相比起 DX 的结果，z 与 w 各需要乘以 -1
}

int main(int argc, char** argv) {
  MyTest();

  CreateCyclimbWindow();

  InitDX11();

  for (int i = 0; i < argc; i++) {
    if (!strcmp(argv[i], "-scene") && argc > i + 1) {
      printf("Scene set to %s\n", argv[i + 1]);
      g_scene_idx = std::atoi(argv[i + 1]);
    }
  }

  g_scenes[0] = new DX11ClearScreenScene();
  g_scenes[1] = new DX11HelloTriangleScene();
  DX11ChunksScene* chunks_scene = new DX11ChunksScene();
  g_scenes[2] = chunks_scene;
  DX11LightScatterScene* lightscatter_scene = new DX11LightScatterScene();
  g_scenes[3] = lightscatter_scene;
  g_scenes[4] = new DX11LightScatterWithChunkScene(chunks_scene, lightscatter_scene);

  while (!glfwWindowShouldClose(g_window)) {
    glfwPollEvents();
    Render_D3D11();
  }

  return 0;
}