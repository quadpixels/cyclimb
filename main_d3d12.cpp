#pragma comment(lib, "d3d12.lib")

#include <Windows.h>
#undef max
#undef min

#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <wrl/client.h>

#include "testshapes.hpp"
#include "scene.hpp"
#include "textrender.hpp"
#include <DirectXMath.h>

extern GraphicsAPI g_api;

extern int WIN_W, WIN_H, SHADOW_RES;
static const int FRAME_COUNT = 2;

extern bool init_done;
extern HWND g_hwnd;
extern void CreateCyclimbWindow();
extern ClimbScene* g_climbscene;
extern bool g_main_menu_visible;
extern MainMenu* g_mainmenu;
extern Particles* g_particles;
extern ChunkGrid* g_chunkgrid[];

static ChunkPass* chunk_pass_depth, * chunk_pass_normal;
const static int NUM_SPRITES = 1024;
ID3D12Device* g_device12;  // Referred to in chunk.cpp
static IDXGIFactory4* g_factory;
static int g_frame_index;
static ID3D12Fence* g_fence;
static int g_fence_value = 0;
static HANDLE g_fence_event;
static ID3D12CommandQueue* g_command_queue;
static ID3D12CommandAllocator* g_command_allocator;
static ID3D12GraphicsCommandList* g_command_list;
static IDXGISwapChain3* g_swapchain;
static ID3D12DescriptorHeap* g_rtv_heap;
static ID3D12Resource* g_rendertargets[FRAME_COUNT];
static unsigned g_rtv_descriptor_size;

// DirectX 12 boilerplate starts from here
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
    CE(D3D12CreateDevice(warp_adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device12)));
    printf("Created a WARP device=%p\n", g_device12);;
  }
  else {
    IDXGIAdapter1* hw_adapter;
    for (int idx = 0; g_factory->EnumAdapters1(idx, &hw_adapter) != DXGI_ERROR_NOT_FOUND; idx++) {
      DXGI_ADAPTER_DESC1 desc;
      hw_adapter->GetDesc1(&desc);
      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
      else {
        CE(D3D12CreateDevice(hw_adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device12)));
        printf("Created a hardware device = %p\n", g_device12);
        break;
      }
    }
  }

  assert(g_device12 != nullptr);

  {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    CE(g_device12->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_command_queue)));
  }

  CE(g_device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
  g_fence_value = 1;
  g_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&g_command_allocator)));

  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
    g_command_allocator, nullptr, IID_PPV_ARGS(&g_command_list)));
  CE(g_command_list->Close());
}

void InitSwapChain() {
  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = (UINT)WIN_W;
  desc.Height = (UINT)WIN_H;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  IDXGISwapChain1* swapchain1;
  CE(g_factory->CreateSwapChainForHwnd(g_command_queue,
    g_hwnd, &desc, nullptr, nullptr, &swapchain1));
  g_swapchain = (IDXGISwapChain3*)swapchain1;
  CE(g_factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER));
  g_frame_index = g_swapchain->GetCurrentBackBufferIndex();

  printf("Created swapchain.\n");

  {
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = 2;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    CE(g_device12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_rtv_heap)));
    printf("Created RTV heap.\n");
  }

  g_rtv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(
    D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(g_rtv_heap->GetCPUDescriptorHandleForHeapStart());
  for (int i = 0; i < FRAME_COUNT; i++) {
    CE(g_swapchain->GetBuffer(i, IID_PPV_ARGS(&g_rendertargets[i])));
    g_device12->CreateRenderTargetView(g_rendertargets[i], nullptr, rtv_handle);
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

void Render_D3D12() {
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);

  CE(g_command_allocator->Reset());
  CE(g_command_list->Reset(g_command_allocator, chunk_pass_normal->pipeline_state_depth_only));
  
  ID3D12Resource* render_target = g_rendertargets[g_frame_index];
  g_command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    render_target,
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));

  float bg_color[] = { 1.0f, 1.0f, 0.8f, 1.0f };
  g_command_list->ClearRenderTargetView(rtv_handle, bg_color, 0, nullptr);

  g_command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    render_target,
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PRESENT
    )));

  CE(g_command_list->Close());
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_command_list);
  CE(g_swapchain->Present(1, 0));
  WaitForPreviousFrame();
}

void MyInit_D3D12() {
  InitDeviceAndCommandQ();
  InitSwapChain();

  g_chunkgrid[3] = new ChunkGrid(1, 1, 1);
  g_chunkgrid[3]->SetVoxel(0, 0, 0, 12);

  Particles::InitStatic(g_chunkgrid[3]);
  ClimbScene::InitStatic();
  g_climbscene = new ClimbScene();
  g_climbscene->Init();

  g_mainmenu = new MainMenu();

  // DX12-specific
  chunk_pass_depth = new ChunkPass();
  chunk_pass_depth->AllocateConstantBuffers(NUM_SPRITES);
  chunk_pass_depth->InitD3D12DefaultPalette();
  chunk_pass_normal = new ChunkPass();
  chunk_pass_normal->AllocateConstantBuffers(NUM_SPRITES);
  chunk_pass_normal->InitD3D12DefaultPalette();

  init_done = true;
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