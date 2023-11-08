#pragma comment(lib, "d3d12.lib")

#include <Windows.h>
#undef max
#undef min

#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <wrl/client.h>

#include "chunk.hpp"
#include "testshapes.hpp"
#include "scene.hpp"
#include "sprite.hpp"
#include "textrender.hpp"
#include "util.hpp"
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
extern DirectionalLight* g_dir_light;
extern Camera* GetCurrentSceneCamera();
extern GameScene* GetCurrentGameScene();
extern DirectX::XMMATRIX g_projection_d3d11;
extern Particles* g_particles;
extern float g_cam_rot_x, g_cam_rot_y;
extern TextMessage* g_textmessage;

static ChunkPass* chunk_pass_depth, * chunk_pass_normal;
const static int NUM_SPRITES = 1024;
ID3D12Device* g_device12;  // Referred to in chunk.cpp
static IDXGIFactory4* g_factory;
static int g_frame_index;
static ID3D12Fence* g_fence;
static int g_fence_value = 0;
static HANDLE g_fence_event;
ID3D12CommandQueue* g_command_queue; // Shared with textrender.cpp
ID3D12CommandAllocator* g_command_allocator; // Shared with textrender.cpp
ID3D12GraphicsCommandList* g_command_list; // Shared with textrender.cpp
static IDXGISwapChain3* g_swapchain;
static ID3D12DescriptorHeap* g_rtv_heap;
static ID3D12Resource* g_rendertargets[FRAME_COUNT];
static unsigned g_rtv_descriptor_size;
static ID3D12Resource* g_depth_buffer;
static ID3D12DescriptorHeap* g_dsv_heap;
static unsigned g_dsv_descriptor_size;
static ID3D12Resource* g_gbuffer12;
static ID3D12Resource* g_shadow_map;
static TextPass* text_pass;
const static int NUM_CHARS = 1024;

// For ChunkPass
static ID3D12Resource* d_per_scene_cb;
DefaultPalettePerSceneCB h_per_scene_cb;
static ID3D12DescriptorHeap* g_cbv_heap;
static int g_cbv_descriptor_size;

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
    desc.NumDescriptors = 4;  // framebuffer[0], framebuffer[1], GBuffer, Shadow Map
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

static void InitResources() {
  // 对应InitDevice11()中的g_perscene_cb
  assert(sizeof(DefaultPalettePerSceneCB) <= 256);
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(256)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&d_per_scene_cb)));

  D3D12_DESCRIPTOR_HEAP_DESC cbv_heap_desc{};
  cbv_heap_desc.NumDescriptors = 2;  // Per-scene CB, Shadow Map SRV
  cbv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  cbv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CE(g_device12->CreateDescriptorHeap(&cbv_heap_desc, IID_PPV_ARGS(&g_cbv_heap)));
  g_cbv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // Per-scene CBV
  D3D12_CONSTANT_BUFFER_VIEW_DESC per_scene_cbv_desc{};
  per_scene_cbv_desc.BufferLocation = d_per_scene_cb->GetGPUVirtualAddress();
  per_scene_cbv_desc.SizeInBytes = 256;
  CD3DX12_CPU_DESCRIPTOR_HANDLE handle1(g_cbv_heap->GetCPUDescriptorHandleForHeapStart());
  g_device12->CreateConstantBufferView(&per_scene_cbv_desc, handle1);

  // Depth buffer
  D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
  depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
  depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
  depthOptimizedClearValue.DepthStencil.Stencil = 0;

  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Tex2D(
      DXGI_FORMAT_R32_TYPELESS, WIN_W, WIN_H, 1, 0, 1, 0,
      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)),
    D3D12_RESOURCE_STATE_DEPTH_WRITE,
    &depthOptimizedClearValue,
    IID_PPV_ARGS(&g_depth_buffer)));

  D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc{};
  dsv_heap_desc.NumDescriptors = 2;
  dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  CE(g_device12->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&g_dsv_heap)));
  g_dsv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

  // DSV
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = { };
  dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
  dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
  CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(g_dsv_heap->GetCPUDescriptorHandleForHeapStart());
  g_device12->CreateDepthStencilView(g_depth_buffer, &dsv_desc, dsv_handle);
  dsv_handle.Offset(g_dsv_descriptor_size);

  // Shadow map
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Tex2D(
      DXGI_FORMAT_R32_TYPELESS, SHADOW_RES, SHADOW_RES, 1, 0, 1, 0,
      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)),
    D3D12_RESOURCE_STATE_GENERIC_READ,  // Will transition to DEPTH_WRITE during rendering
    &depthOptimizedClearValue,
    IID_PPV_ARGS(&g_shadow_map)));
  
  // Shadow map's DSV
  g_device12->CreateDepthStencilView(g_shadow_map, &dsv_desc, dsv_handle);

  // Shadow map's SRV
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Texture2D.MostDetailedMip = 0;
  srv_desc.Texture2D.MipLevels = 1;
  g_device12->CreateShaderResourceView(g_shadow_map, &srv_desc,
    CD3DX12_CPU_DESCRIPTOR_HANDLE(g_cbv_heap->GetCPUDescriptorHandleForHeapStart(), 1, g_cbv_descriptor_size));

  // GBuffer's resource
  D3D12_CLEAR_VALUE zero{};
  zero.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Tex2D(
      DXGI_FORMAT_R32G32B32A32_FLOAT, WIN_W, WIN_H, 1, 0, 1, 0,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)),
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    &zero,
    IID_PPV_ARGS(&g_gbuffer12)));

  // GBuffer's RTV
  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(g_rtv_heap->GetCPUDescriptorHandleForHeapStart());
    rtv_handle.Offset(FRAME_COUNT, g_rtv_descriptor_size);
    g_device12->CreateRenderTargetView(g_gbuffer12, nullptr, rtv_handle);
  }
}

void UpdatePerSceneCB_D3D12(const DirectX::XMVECTOR* dir_light, const DirectX::XMMATRIX* lightPV, const DirectX::XMVECTOR* camPos) {
  if (dir_light) h_per_scene_cb.dir_light = *dir_light;
  if (lightPV) h_per_scene_cb.lightPV = *lightPV;
  if (camPos) h_per_scene_cb.cam_pos = *camPos;

  UINT8* pData;
  CD3DX12_RANGE readRange(0, 0);
  CE(d_per_scene_cb->Map(0, &readRange, (void**)&pData));
  memcpy(pData, &h_per_scene_cb, sizeof(h_per_scene_cb));
  d_per_scene_cb->Unmap(0, nullptr);
}

void do_RenderText_D3D12(const std::wstring& text, float x, float y, float scale, glm::vec3 color, glm::mat4 transform) {
  text_pass->AddText(text, x, y, scale, color, transform);
}

void Render_D3D12() {
  text_pass->StartPass();
  GetCurrentGameScene()->PreRender();
  GetCurrentGameScene()->PrepareSpriteListForRender();
  UpdatePerSceneCB_D3D12(&(g_dir_light->GetDir_D3D11()), &(g_dir_light->GetPV_D3D11()), &(GetCurrentSceneCamera()->GetPos_D3D11()));

  Camera* cam = GetCurrentSceneCamera();
  GameScene* scene = GetCurrentGameScene();
  std::vector<Sprite*>* sprites = nullptr;
  if (scene) {
    sprites = GetCurrentGameScene()->GetSpriteListForRender();
    // for (Sprite* s : *sprites) {
    //   if (s && s->draw_mode == draw_mode) s->Render_D3D11();
    // }
    // for (Sprite* s : g_projectiles) {
    //   if (s && s->draw_mode == draw_mode)
    //     s->Render_D3D11();
    // }
  }

  // Prepare handles
  CD3DX12_CPU_DESCRIPTOR_HANDLE backbuffer_rtv_handle(g_rtv_heap->GetCPUDescriptorHandleForHeapStart(), g_frame_index, g_rtv_descriptor_size);
  CD3DX12_CPU_DESCRIPTOR_HANDLE gbuffer_rtv_handle(g_rtv_heap->GetCPUDescriptorHandleForHeapStart(), FRAME_COUNT, g_rtv_descriptor_size);
  CD3DX12_CPU_DESCRIPTOR_HANDLE main_dsv_handle(g_dsv_heap->GetCPUDescriptorHandleForHeapStart());
  CD3DX12_CPU_DESCRIPTOR_HANDLE shadow_map_dsv_handle(g_dsv_heap->GetCPUDescriptorHandleForHeapStart(), 1, g_dsv_descriptor_size);
  CD3DX12_GPU_DESCRIPTOR_HANDLE shadow_map_srv_handle(g_cbv_heap->GetGPUDescriptorHandleForHeapStart(), 1, g_cbv_descriptor_size);

  ID3D12DescriptorHeap* ppHeaps[] = { g_cbv_heap };

  CE(g_command_allocator->Reset());
  CE(g_command_list->Reset(g_command_allocator, chunk_pass_normal->pipeline_state_depth_only));
  g_command_list->SetGraphicsRootSignature(chunk_pass_normal->root_signature_default_palette);
  
  g_command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_shadow_map,
    D3D12_RESOURCE_STATE_GENERIC_READ,
    D3D12_RESOURCE_STATE_DEPTH_WRITE)));

  // Depth pass
  g_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_VIEWPORT viewport_depth = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f * SHADOW_RES, 1.0f * SHADOW_RES, 0.0f, 1.0f);
  D3D12_RECT scissor_depth = CD3DX12_RECT(0, 0, long(SHADOW_RES), long(SHADOW_RES));
  g_command_list->RSSetViewports(1, &viewport_depth);
  g_command_list->RSSetScissorRects(1, &scissor_depth);
  g_command_list->ClearDepthStencilView(shadow_map_dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  g_command_list->OMSetRenderTargets(0, nullptr, FALSE, &shadow_map_dsv_handle);

  g_command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
  g_command_list->SetGraphicsRootConstantBufferView(1, d_per_scene_cb->GetGPUVirtualAddress());
  
  if (sprites != nullptr) {
    chunk_pass_depth->StartPass();
    for (Sprite* s : *sprites) {
      if (s && s->draw_mode == Sprite::DrawMode::NORMAL)
        s->RecordRenderCommand_D3D12(chunk_pass_depth, g_dir_light->GetV_D3D11(), g_dir_light->GetP_D3D11_DXMath());
    }
    chunk_pass_depth->EndPass();
    const int N = int(chunk_pass_depth->chunk_instances.size());
    for (int i = 0; i < N; i++) {
      Chunk* c = chunk_pass_depth->chunk_instances[i];
      D3D12_GPU_VIRTUAL_ADDRESS cbv0_addr = chunk_pass_depth->d_per_object_cbs->GetGPUVirtualAddress() + 256 * i;
      g_command_list->SetGraphicsRootConstantBufferView(0, cbv0_addr);  // Per-object CB
      g_command_list->IASetVertexBuffers(0, 1, &(c->d3d12_vertex_buffer_view));
      g_command_list->DrawInstanced(c->tri_count * 3, 1, 0, 0);
    }
  }

  CE(g_command_list->Close());
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_command_list);

  CE(g_command_list->Reset(g_command_allocator, chunk_pass_normal->pipeline_state_default_palette));
  g_command_list->SetGraphicsRootSignature(chunk_pass_normal->root_signature_default_palette);

  ID3D12Resource* render_target = g_rendertargets[g_frame_index];
  g_command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    render_target,
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));

  float bg_color[] = { 1.0f, 1.0f, 0.8f, 1.0f };
  g_command_list->ClearRenderTargetView(backbuffer_rtv_handle, bg_color, 0, nullptr);
  g_command_list->ClearDepthStencilView(main_dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

  g_command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_shadow_map,
    D3D12_RESOURCE_STATE_DEPTH_WRITE,
    D3D12_RESOURCE_STATE_GENERIC_READ)));

  // Main color pass
  D3D12_VIEWPORT main_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f * WIN_W, 1.0f * WIN_H, 0.0f, 1.0f);
  D3D12_RECT main_scissor = CD3DX12_RECT(0, 0, long(WIN_W), long(WIN_H));
  g_command_list->RSSetViewports(1, &main_viewport);
  g_command_list->RSSetScissorRects(1, &main_scissor);
  D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = { backbuffer_rtv_handle, gbuffer_rtv_handle };
  g_command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
  g_command_list->SetGraphicsRootDescriptorTable(2, shadow_map_srv_handle);
  g_command_list->OMSetRenderTargets(2, rtvs, FALSE, &main_dsv_handle);
  g_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_command_list->SetGraphicsRootConstantBufferView(1, d_per_scene_cb->GetGPUVirtualAddress());
  

  if (sprites != nullptr) {
    chunk_pass_normal->StartPass();
    for (Sprite* s : *sprites) {
      if (s && s->draw_mode == Sprite::DrawMode::NORMAL)
        s->RecordRenderCommand_D3D12(chunk_pass_normal, cam->GetViewMatrix_D3D11(), g_projection_d3d11);
    }
    chunk_pass_normal->EndPass();
    const int N = int(chunk_pass_depth->chunk_instances.size());
    for (int i = 0; i < N; i++) {
      Chunk* c = chunk_pass_normal->chunk_instances[i];
      D3D12_GPU_VIRTUAL_ADDRESS cbv0_addr = chunk_pass_normal->d_per_object_cbs->GetGPUVirtualAddress() + 256 * i;
      g_command_list->SetGraphicsRootConstantBufferView(0, cbv0_addr);  // Per-object CB
      g_command_list->IASetVertexBuffers(0, 1, &(c->d3d12_vertex_buffer_view));
      g_command_list->DrawInstanced(c->tri_count * 3, 1, 0, 0);
    }
  }

  CE(g_command_list->Close());
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_command_list);

  {
    glm::mat4 uitransform(1);
    uitransform *= glm::rotate(uitransform, g_cam_rot_x, glm::vec3(0.0f, 1.0f, 0.0f));
    uitransform *= glm::rotate(uitransform, g_cam_rot_y, glm::vec3(1.0f, 0.0f, 0.0f));
    if (g_main_menu_visible) {
      g_mainmenu->Render_D3D12(uitransform);
    }
    GameScene* scene = GetCurrentGameScene();
    if (scene) scene->RenderHUD_D3D12();
  }

  // TextPass's rendering procedure
  CE(g_command_list->Reset(g_command_allocator, text_pass->pipeline_state));
  g_command_list->SetGraphicsRootSignature(text_pass->root_signature);
  ID3D12DescriptorHeap* ppHeaps_textpass[] = { text_pass->srv_heap };
  g_command_list->SetDescriptorHeaps(_countof(ppHeaps_textpass), ppHeaps_textpass);
  g_command_list->RSSetViewports(1, &main_viewport);
  g_command_list->RSSetScissorRects(1, &main_scissor);
  g_command_list->OMSetRenderTargets(2, rtvs, FALSE, &main_dsv_handle);
  float blend_factor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
  g_command_list->OMSetBlendFactor(blend_factor);

  g_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  for (size_t i = 0; i < text_pass->characters_to_display.size(); i++) {
    const TextPass::CharacterToDisplay& ctd = text_pass->characters_to_display[i];
    g_command_list->IASetVertexBuffers(0, 1, &ctd.vbv);
    g_command_list->SetGraphicsRootConstantBufferView(0, text_pass->per_scene_cbs->GetGPUVirtualAddress() + sizeof(TextCbPerScene) * ctd.per_scene_cb_index);
    CD3DX12_GPU_DESCRIPTOR_HANDLE srv_handle(
      text_pass->srv_heap->GetGPUDescriptorHandleForHeapStart(),
      ctd.character->offset_in_srv_heap, text_pass->srv_descriptor_size);
    g_command_list->SetGraphicsRootDescriptorTable(1, srv_handle);
    g_command_list->DrawInstanced(6, 1, 0, 0);
  }

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

// Init game-related variables
void MyInit_D3D12() {
  InitDeviceAndCommandQ();
  InitSwapChain();
  InitResources();

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
  text_pass = new TextPass(g_device12, g_command_queue, g_command_list, g_command_allocator);
  text_pass->AllocateConstantBuffers(NUM_CHARS);
  text_pass->InitD3D12();
  text_pass->InitFreetype();

  g_projection_d3d11 = DirectX::XMMatrixPerspectiveFovLH(60.0f * 3.14159f / 180.0f, WIN_W * 1.0f / WIN_H, 0.01f, 499.0f);
  Particles::InitStatic(g_chunkgrid[3]);
  g_particles = new Particles();
  g_dir_light = new DirectionalLight(glm::vec3(1, -3, -1), glm::vec3(1, 3, -1));

  g_textmessage = new TextMessage();

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