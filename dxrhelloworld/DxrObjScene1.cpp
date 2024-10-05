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
  IDxcBlob* raygen_library = CompileShaderLibrary(L"shaders/minimal.hlsl");

  D3D12_EXPORT_DESC raygen_export{};
  raygen_export.Name = L"RayGen";
  raygen_export.ExportToRename = nullptr;
  raygen_export.Flags = D3D12_EXPORT_FLAG_NONE;
  D3D12_DXIL_LIBRARY_DESC raygen_lib_desc{};
  raygen_lib_desc.DXILLibrary.BytecodeLength = raygen_library->GetBufferSize();
  raygen_lib_desc.DXILLibrary.pShaderBytecode = raygen_library->GetBufferPointer();
  raygen_lib_desc.NumExports = 1;
  raygen_lib_desc.pExports = &raygen_export;

  std::vector<D3D12_STATE_SUBOBJECT> subobjects(4);

  // [0]: RayGen Library
  D3D12_STATE_SUBOBJECT subobj_raygen_lib{};
  subobj_raygen_lib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  subobj_raygen_lib.pDesc = &raygen_lib_desc;
  subobjects[0] = subobj_raygen_lib;

  // [1]: Shader Config
  D3D12_RAYTRACING_SHADER_CONFIG shader_config{};
  shader_config.MaxAttributeSizeInBytes = 32;
  shader_config.MaxPayloadSizeInBytes = 32;
  D3D12_STATE_SUBOBJECT subobj_shader_config{};
  subobj_shader_config.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  subobj_shader_config.pDesc = &shader_config;
  subobjects[1] = subobj_shader_config;

  const wchar_t* exported_symbols[] = { L"RayGen" };

  // [2]: Says [1] is associated to export "RayGen"
  D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shader_config_to_export_assoc{};
  shader_config_to_export_assoc.NumExports = 1;
  shader_config_to_export_assoc.pExports = exported_symbols;
  shader_config_to_export_assoc.pSubobjectToAssociate = &(subobjects[1]);
  D3D12_STATE_SUBOBJECT subobj_shader_config_to_export_assoc{};
  subobj_shader_config_to_export_assoc.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
  subobj_shader_config_to_export_assoc.pDesc = &shader_config_to_export_assoc;
  subobjects[2] = subobj_shader_config_to_export_assoc;

  D3D12_STATE_OBJECT_DESC pipeline_desc{};
  pipeline_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  pipeline_desc.NumSubobjects = subobjects.size();
  pipeline_desc.pSubobjects = subobjects.data();

  // Subobject association of type D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG
  // must be defined for all relevant exports, yet no such subobject exists at all.
  // An example of an export needing this association is "RayGen".
  // [ STATE_CREATION ERROR #1194: CREATE_STATE_OBJECT_ERROR]

  // [3]: Pipeline Config
  D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
  pipeline_config.MaxTraceRecursionDepth = 1;
  D3D12_STATE_SUBOBJECT subobj_pipeline_config{};
  subobj_pipeline_config.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
  subobj_pipeline_config.pDesc = &pipeline_config;
  subobjects[3] = subobj_pipeline_config;

  g_device12->CreateStateObject(&pipeline_desc, IID_PPV_ARGS(&rt_state_object));
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