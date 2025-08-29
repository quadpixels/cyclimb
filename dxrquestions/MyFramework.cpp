#include "MyFramework.h"

#include <assert.h>
#include <stdio.h>

#include <source_location>
#include <stdexcept>
#include <vector>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>

#include "x64/Debug/CompiledShaders/raytracing_shaders.hlsl.h"

// https://stackoverflow.com/questions/65315241/how-can-i-fix-requires-l-value
template <class T>
constexpr auto& keep(T&& x) noexcept {
  return x;
}

#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}

// in main.cpp
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

void MyFramework::InitWindow() {
  WNDCLASS windowClass = { 0 };
  windowClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
  windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
  windowClass.hInstance = NULL;
  windowClass.lpfnWndProc = WndProc;
  windowClass.lpszClassName = L"Window in Console"; //needs to be the same name
  //when creating the window as well
  windowClass.style = CS_HREDRAW | CS_VREDRAW;

  LPCWSTR window_name = L"My Framework!";
  LPCWSTR class_name = L"MyFramework_class";
  HINSTANCE hinstance = GetModuleHandle(nullptr);

  if (!RegisterClass(&windowClass)) {
    printf("Cannot register window class\n");
  }

  hwnd = CreateWindowW(
    windowClass.lpszClassName,
    window_name,
    WS_OVERLAPPEDWINDOW,
    16,
    16,
    WIN_W, WIN_H,
    nullptr, nullptr,
    hinstance, nullptr);

  ShowWindow(hwnd, SW_RESTORE);
}

void MyFramework::InitDeviceAndCommandQ() {
  uint32_t dxgi_factory_flags = 0;
  bool use_warp_device = false;
#ifndef NDEBUG
  const bool use_debug_layer = true;
#else
  const bool use_debug_layer = false;
#endif

  if (use_debug_layer) {
    ID3D12Debug* debug_controller;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
      debug_controller->EnableDebugLayer();
      dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
      printf("Enabling debug layer\n");

      ID3D12Debug1* debug_controller1;
      debug_controller->QueryInterface(IID_PPV_ARGS(&debug_controller1));
      if (debug_controller1) {
        printf("Enabling GPU-based validation\n");
        debug_controller1->SetEnableGPUBasedValidation(true);
      }
    }
  }

  CE(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&dxgi_factory)));

  if (use_warp_device) {
    IDXGIAdapter* warp_adapter;
    CE(dxgi_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)));
    CE(D3D12CreateDevice(warp_adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device12)));
    printf("Created a WARP device=%p\n", device12);
  }
  else {
    IDXGIAdapter1* hw_adapter;
    for (int idx = 0; dxgi_factory->EnumAdapters1(idx, &hw_adapter) != DXGI_ERROR_NOT_FOUND; idx++) {
      DXGI_ADAPTER_DESC1 desc;
      hw_adapter->GetDesc1(&desc);
      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
      else {
        CE(D3D12CreateDevice(hw_adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device12)));
        printf("Created a hardware device = %p, %ls\n", device12, desc.Description);
        break;
      }
    }
  }

  // Check raytracing support
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
  assert(SUCCEEDED(device12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
    &options5, sizeof(options5))));
  if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0) {
    printf("This device supports DXR 1.0.\n");
  }
  if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1) {
    printf("This device supports DXR 1.1.\n");
  }

  assert(device12 != nullptr);

  D3D12_COMMAND_QUEUE_DESC desc = {
    .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
    .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
  };
  CE(device12->CreateCommandQueue(&desc, IID_PPV_ARGS(&command_queue)));

  CE(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
    nullptr, IID_PPV_ARGS(&command_list)));
  command_list->Close();

  CE(device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  fence_value = 1;
  fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

ID3D12Device5* MyFramework::GetDevice() {
  return device12;
}

void MyFramework::InitSwapchain() {
  // Swapchain
  {
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FRAME_COUNT;
    swapChainDesc.Width = WIN_W;
    swapChainDesc.Height = WIN_H;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    CE(dxgi_factory->CreateSwapChainForHwnd(
      command_queue, hwnd, &swapChainDesc, nullptr, nullptr,
      (IDXGISwapChain1**)(&swapchain)));
    printf("Created swapchain.\n");
  }

  // RTV Heap
  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = FRAME_COUNT,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };
    CE(device12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtv_heap)));
    printf("Created RTV heap.\n");

    rtv_descriptor_size = device12->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < FRAME_COUNT; i++) {
      CE(swapchain->GetBuffer(i, IID_PPV_ARGS(&rendertargets[i])));
      wchar_t buf[100];
      _snwprintf_s(buf, sizeof(buf), L"Render target #%d", i);
      rendertargets[i]->SetName(buf);

      device12->CreateRenderTargetView(rendertargets[i], nullptr, rtv_handle);
      rtv_handle.Offset(rtv_descriptor_size);
    }
    printf("Created backbuffers' RTVs\n");
  }

  CE(dxgi_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
  WaitForPreviousFrame();
}

void MyFramework::CreateRtGlobalRootSig(ID3D12RootSignature** out_rootsig) {
  const int SRV_COUNT = 2;  // TLAS, texture

  D3D12_ROOT_PARAMETER root_params[2];
  root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_params[0].DescriptorTable.NumDescriptorRanges = 1;
  D3D12_DESCRIPTOR_RANGE desc_range{};
  desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  desc_range.NumDescriptors = 1;  // RT Output Buffer
  desc_range.BaseShaderRegister = 0;
  desc_range.RegisterSpace = 0;
  desc_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  root_params[0].DescriptorTable.pDescriptorRanges = &desc_range;
  root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_params[1].DescriptorTable.NumDescriptorRanges = 1;
  D3D12_DESCRIPTOR_RANGE desc_range1{};
  desc_range1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  desc_range1.NumDescriptors = 2;  // AS, texture
  desc_range1.BaseShaderRegister = 0;
  desc_range1.RegisterSpace = 0;
  desc_range1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  root_params[1].DescriptorTable.pDescriptorRanges = &desc_range1;
  root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
  rootsig_desc.NumStaticSamplers = 0;
  rootsig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  rootsig_desc.NumParameters = 2;
  rootsig_desc.pParameters = root_params;
  ID3DBlob* signature, * error;
  CE(D3D12SerializeRootSignature(
    &rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
  CE(device12->CreateRootSignature(
    0, signature->GetBufferPointer(), signature->GetBufferSize(),
    IID_PPV_ARGS(out_rootsig)));
  signature->Release();
  if (error)
    error->Release();
}

void MyFramework::CreateRtOutputResource(ID3D12Resource** out_res) {
  D3D12_RESOURCE_DESC desc{};
  desc.DepthOrArraySize = 1;
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  desc.Width = WIN_W;
  desc.Height = WIN_H;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE, &desc,
    D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
    IID_PPV_ARGS(out_res)));
}

void MyFramework::CreateCBVSRVUAVHeap(ID3D12DescriptorHeap** h, ID3D12DescriptorHeap** h_cpu, uint32_t num_descriptors) {
  // UAV heap
  D3D12_DESCRIPTOR_HEAP_DESC dhd{};
  dhd.NumDescriptors = num_descriptors;
  dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  dhd.NodeMask = 0;
  device12->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(h));
  cbvsrvuav_descriptor_size = device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  if (h_cpu) {
    dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device12->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(h_cpu));
  }
}

D3D12_CPU_DESCRIPTOR_HANDLE MyFramework::GetCurrRenderTargetCPUDescriptor() {
  CD3DX12_CPU_DESCRIPTOR_HANDLE ret(rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    frame_index, rtv_descriptor_size);
  return (D3D12_CPU_DESCRIPTOR_HANDLE)ret;
}

ID3D12CommandQueue* MyFramework::GetCommandQueue() {
  return command_queue;
}

ID3D12CommandAllocator* MyFramework::GetCommandAllocator() {
  return command_allocator;
}

ID3D12GraphicsCommandList4* MyFramework::GetGraphicsCommandList() {
  return command_list;
}

ID3D12Resource* MyFramework::GetCurrentRenderTarget() {
  return rendertargets[frame_index];
}

void MyFramework::WaitForPreviousFrame() {
  int value = fence_value++;
  CE(command_queue->Signal(fence, value));
  if (fence->GetCompletedValue() < value) {
    CE(fence->SetEventOnCompletion(value, fence_event));
    CE(WaitForSingleObject(fence_event, INFINITE));
  }
  frame_index = swapchain->GetCurrentBackBufferIndex();
}

HRESULT MyFramework::Present() {
  return swapchain->Present(1, 0);
}

void MyFramework::CreateUAVTexture2D(ID3D12Resource* res, 
  ID3D12DescriptorHeap* h, uint32_t idx) {
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  CD3DX12_CPU_DESCRIPTOR_HANDLE uav_handle(h->GetCPUDescriptorHandleForHeapStart(), idx, cbvsrvuav_descriptor_size);
  device12->CreateUnorderedAccessView(res, nullptr, &uav_desc, uav_handle);
}

uint32_t MyFramework::GetCBVSRVUAVDescriptorSize() {
  return cbvsrvuav_descriptor_size;
}

void MyFramework::CreateMyRtPipeline(MyRtPipeline* my_rt_pipeline,
  ID3D12RootSignature* global_rootsig) {
  // Params
  std::vector<std::wstring> exports = {
    L"MyRaygenShader"
  };

  std::vector<D3D12_STATE_SUBOBJECT> subobjects;
  subobjects.reserve(16);

  D3D12_DXIL_LIBRARY_DESC dxil_lib_desc{};
  std::vector<D3D12_EXPORT_DESC> dxil_lib_exports(exports.size());
  for (uint32_t i = 0; i < exports.size(); i++) {
    dxil_lib_exports[i].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[i].ExportToRename = nullptr;
    dxil_lib_exports[i].Name = exports[i].c_str();
  }

  dxil_lib_desc.DXILLibrary.pShaderBytecode = g_RaytracingShaders;
  dxil_lib_desc.DXILLibrary.BytecodeLength = sizeof(g_RaytracingShaders);
  dxil_lib_desc.NumExports = exports.size();
  dxil_lib_desc.pExports = dxil_lib_exports.data();

  // DXIL library
  D3D12_STATE_SUBOBJECT subobj_dxil_lib{};
  subobj_dxil_lib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  subobj_dxil_lib.pDesc = &dxil_lib_desc;
  subobjects.push_back(subobj_dxil_lib);

  // Hit group
  D3D12_STATE_SUBOBJECT subobj_hitgroup{};
  
  // Shader config
  D3D12_STATE_SUBOBJECT subobj_shaderconfig{};
  subobj_shaderconfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  D3D12_RAYTRACING_SHADER_CONFIG shader_config{};
  shader_config.MaxPayloadSizeInBytes = 16;
  shader_config.MaxAttributeSizeInBytes = 8;
  subobj_shaderconfig.pDesc = &shader_config;
  subobjects.push_back(subobj_shaderconfig);

  // Global rootsignature
  D3D12_STATE_SUBOBJECT subobj_global_rootsig{};
  subobj_global_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  subobj_global_rootsig.pDesc = &global_rootsig;
  subobjects.push_back(subobj_global_rootsig);

  // Pipeline config
  D3D12_STATE_SUBOBJECT subobj_pipeline_config{
    .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG };
  D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
  pipeline_config.MaxTraceRecursionDepth = 1;
  subobj_pipeline_config.pDesc = &pipeline_config;
  subobjects.push_back(subobj_pipeline_config);

  D3D12_STATE_OBJECT_DESC rtpso_desc{};
  rtpso_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  rtpso_desc.NumSubobjects = subobjects.size();
  rtpso_desc.pSubobjects = subobjects.data();
  CE(device12->CreateStateObject(&rtpso_desc, IID_PPV_ARGS(&(my_rt_pipeline->rt_state_object))));
  my_rt_pipeline->rt_state_object->QueryInterface(IID_PPV_ARGS(&(my_rt_pipeline->rt_state_object_props)));

  size_t sbt_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  D3D12_RESOURCE_DESC sbt_desc{};
  sbt_desc.DepthOrArraySize = 1;
  sbt_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  sbt_desc.Format = DXGI_FORMAT_UNKNOWN;
  sbt_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  sbt_desc.Width = sbt_size;
  sbt_desc.Height = 1;
  sbt_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  sbt_desc.SampleDesc.Count = 1;
  sbt_desc.SampleDesc.Quality = 0;
  sbt_desc.MipLevels = 1;
  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE, &sbt_desc,
    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
    IID_PPV_ARGS(&(my_rt_pipeline->rt_sbt))));

  // Construct SBT
  void* raygen_shader_id = my_rt_pipeline->rt_state_object_props->GetShaderIdentifier(exports[0].c_str());
  char* mapped{};

  my_rt_pipeline->rt_sbt->Map(0, nullptr, (void**)&mapped);
  memcpy(mapped, raygen_shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  my_rt_pipeline->rt_sbt->Unmap(0, nullptr);

  {
    D3D12_DISPATCH_RAYS_DESC* drd = &(my_rt_pipeline->dispatch_rays_desc);
    drd->Width = WIN_W;
    drd->Height = WIN_H;
    drd->Depth = 1;
    drd->RayGenerationShaderRecord.StartAddress = my_rt_pipeline->rt_sbt->GetGPUVirtualAddress();
    drd->RayGenerationShaderRecord.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  }
}

// ======================================= Helper functions ===============
static void ResourceBarrierTransition(ID3D12GraphicsCommandList4* cmdlist,
  ID3D12Resource* res,
  D3D12_RESOURCE_STATES from,
  D3D12_RESOURCE_STATES to) {
  cmdlist->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    res,
    from,
    to)));
}

// ======================================= Scenes ====================
MyParisIvyLeafScene::MyParisIvyLeafScene(MyFramework* f) : MyScene(f) {
  f->CreateRtGlobalRootSig(&global_rootsig);
  f->CreateRtOutputResource(&rt_output_resource);
  f->CreateCBVSRVUAVHeap(&cbvsrvuav_heap, nullptr, 1);
  f->CreateUAVTexture2D(rt_output_resource, cbvsrvuav_heap, 0);
  f->CreateMyRtPipeline(&my_rt_pipeline, global_rootsig);
  printf("Created rootsig.\n");
}

void MyParisIvyLeafScene::Render() {
  // Clear screen

  D3D12_CPU_DESCRIPTOR_HANDLE handle_rtv = framework->GetCurrRenderTargetCPUDescriptor();
  float bg_color[] = { 0.8f, 1.0f, 1.0f, 1.0f };
  ID3D12CommandAllocator* command_allocator = framework->GetCommandAllocator();
  ID3D12GraphicsCommandList4* command_list = framework->GetGraphicsCommandList();
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));
  ID3D12Resource* rendertarget = framework->GetCurrentRenderTarget();
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&cbvsrvuav_heap);
  command_list->SetComputeRootSignature(global_rootsig);
  CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav(cbvsrvuav_heap->GetGPUDescriptorHandleForHeapStart(), 0, framework->GetCBVSRVUAVDescriptorSize());
  command_list->SetComputeRootDescriptorTable(0, handle_uav);
  command_list->SetPipelineState1(my_rt_pipeline.rt_state_object);
  command_list->DispatchRays(&(my_rt_pipeline.dispatch_rays_desc));
  ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
  command_list->CopyResource(rendertarget, rt_output_resource);
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
  CE(command_list->Close());

  ID3D12CommandQueue* command_queue = framework->GetCommandQueue();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(framework->Present());
  framework->WaitForPreviousFrame();
}

void MyParisIvyLeafScene::Update(float secs) {
}