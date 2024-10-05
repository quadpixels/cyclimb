#include "scene.hpp"

#include <d3dcompiler.h>
#include <d3dx12.h>
#include <dxgi1_4.h>

extern int WIN_W, WIN_H;
extern ID3D12Device5* g_device12;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern int g_rtv_descriptor_size;
extern int g_frame_index;
extern ID3D12Resource* g_rendertargets[];
extern ID3D12CommandQueue* g_command_queue;
extern IDXGISwapChain3* g_swapchain;

void WaitForPreviousFrame();

// Declared in DxrObjScene.cpp
IDxcBlob* CompileShaderLibrary(LPCWSTR fileName);

ObjScene1::ObjScene1() {
  InitDX12Stuff();
  CreateRTPipeline();
}

void ObjScene1::InitDX12Stuff() {
  printf("[ObjScene1::InitDX12Stuff]\n");
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(
    0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
    nullptr, IID_PPV_ARGS(&command_list)));
  command_list->Close();
}

void ObjScene1::CreateRTPipeline() {
}

void ObjScene1::Render() {
  // Just clear RTV
  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);

  float bg_color[] = { 0.8f, 1.0f, 0.8f, 1.0f };
  CE(command_list->Reset(command_allocator, nullptr));
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PRESENT)));
  CE(command_list->Close());
  g_command_queue->ExecuteCommandLists(1,
    (ID3D12CommandList* const*)&command_list);
  CE(g_swapchain->Present(1, 0));
  WaitForPreviousFrame();
}

void ObjScene1::Update(float secs) {
}