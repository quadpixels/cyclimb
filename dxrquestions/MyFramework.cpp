#include "MyFramework.h"

#include <assert.h>
#include <stdio.h>

#include <source_location>
#include <stdexcept>
#include <vector>

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <DirectXMath.h>

#ifndef NDEBUG
#include "x64\Debug\g_PixelShaderIvyLeafTriangle.h"
#include "x64\Debug\g_VertexShaderIvyLeafTriangle.h"
#include "x64/Debug/CompiledShaders/raytracing_shaders.hlsl.h"
#else
#include "x64\Release\g_PixelShaderIvyLeafTriangle.h"
#include "x64\Release\g_VertexShaderIvyLeafTriangle.h"
#include "x64/Release/CompiledShaders/raytracing_shaders.hlsl.h"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "../dxrhelloworld/stb_image.h"

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

void MyFramework::do_CreateRootSig(
  ID3D12RootSignature** root_sig,
  uint32_t num_uav, uint32_t num_srv,
  D3D12_ROOT_SIGNATURE_FLAGS flags, bool has_sampler) {
  const uint32_t NTYPES = 2;
  D3D12_ROOT_PARAMETER root_param{};  // Just one root parameter
  std::vector<D3D12_DESCRIPTOR_RANGE> desc_ranges;
  desc_ranges.reserve(NTYPES);

  D3D12_DESCRIPTOR_RANGE_TYPE range_types[] = {
    D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
    D3D12_DESCRIPTOR_RANGE_TYPE_SRV
  };
  uint32_t view_counts[] = {
    num_uav,
    num_srv
  };

  //if (num_uav > 0) {
  for (uint32_t i=0; i<NTYPES; i++) {
    if (view_counts[i] > 0) {
      desc_ranges.push_back(D3D12_DESCRIPTOR_RANGE());
      D3D12_DESCRIPTOR_RANGE& desc_range = desc_ranges.back();

      desc_range.RangeType = range_types[i];
      desc_range.NumDescriptors = view_counts[i];
      desc_range.BaseShaderRegister = 0;
      desc_range.RegisterSpace = 0;
      desc_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }
  }

  root_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_param.DescriptorTable.NumDescriptorRanges = desc_ranges.size();
  root_param.DescriptorTable.pDescriptorRanges = desc_ranges.data();
  root_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
  D3D12_STATIC_SAMPLER_DESC static_sampler{};
  if (!has_sampler) {
    rootsig_desc.NumStaticSamplers = 0;
  }
  else {
    static_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    static_sampler.AddressU = static_sampler.AddressV = static_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    static_sampler.ShaderRegister = 0; // s0
    static_sampler.RegisterSpace = 0;
    static_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootsig_desc.NumStaticSamplers = 1;
    rootsig_desc.pStaticSamplers = &static_sampler;
  }
  rootsig_desc.Flags = flags;
  rootsig_desc.NumParameters = 1;
  rootsig_desc.pParameters = &root_param;
  ID3DBlob* signature, * error;
  CE(D3D12SerializeRootSignature(
    &rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
  CE(device12->CreateRootSignature(
    0, signature->GetBufferPointer(), signature->GetBufferSize(),
    IID_PPV_ARGS(root_sig)));
  signature->Release();
  if (error)
    error->Release();
}

void MyFramework::CreateRtGlobalRootSig(ID3D12RootSignature** out_rootsig) {
  do_CreateRootSig(out_rootsig, 1, 5, D3D12_ROOT_SIGNATURE_FLAG_NONE, true);
}

void MyFramework::CreateHelloTriangleRootSig(ID3D12RootSignature** out_rootsig) {
  do_CreateRootSig(out_rootsig, 0, 2, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, true);
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

void MyFramework::CreateSRVTexture2D(ID3D12Resource* res, ID3D12DescriptorHeap* h, uint32_t idx) {
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srv_desc.Texture2D.MipLevels = 1;
  srv_desc.Texture2D.MostDetailedMip = 0;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(h->GetCPUDescriptorHandleForHeapStart(), idx, cbvsrvuav_descriptor_size);
  device12->CreateShaderResourceView(res, &srv_desc, srv_handle);
}

void MyFramework::CreateSRVAccelerationStructure(ID3D12Resource* res, ID3D12DescriptorHeap* h, uint32_t idx) {
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
  srv_desc.Format = DXGI_FORMAT_UNKNOWN;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.RaytracingAccelerationStructure.Location = res->GetGPUVirtualAddress();
  CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(h->GetCPUDescriptorHandleForHeapStart(), idx, cbvsrvuav_descriptor_size);
  device12->CreateShaderResourceView(nullptr, &srv_desc, srv_handle);
}

void MyFramework::CreateSRVBuffer(ID3D12Resource* res, ID3D12DescriptorHeap* h, uint32_t idx, uint32_t num_elts, uint32_t stride) {
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv_desc.Format = DXGI_FORMAT_UNKNOWN;
  srv_desc.Buffer.FirstElement = 0;
  srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
  srv_desc.Buffer.NumElements = num_elts;
  srv_desc.Buffer.StructureByteStride = stride;
  CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(h->GetCPUDescriptorHandleForHeapStart(), idx, cbvsrvuav_descriptor_size);
  device12->CreateShaderResourceView(res, &srv_desc, srv_handle);
}

uint32_t MyFramework::GetCBVSRVUAVDescriptorSize() {
  return cbvsrvuav_descriptor_size;
}

void MyFramework::CreateMyRtPipeline(MyRtPipeline* my_rt_pipeline,
  ID3D12RootSignature* global_rootsig) {
  // Params
  std::vector<std::wstring> exports = {
    L"MyRaygenShader",
    L"MyClosestHitShader",
    L"MyMissShader",
    L"MyAnyHitShader",
  };
  const wchar_t* hitgroup_name = L"MyHitGroup";

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
  D3D12_HIT_GROUP_DESC hitgroup_desc{};
  hitgroup_desc.HitGroupExport = hitgroup_name;
  hitgroup_desc.ClosestHitShaderImport = exports[1].c_str();
  hitgroup_desc.AnyHitShaderImport = exports[3].c_str();
  subobj_hitgroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
  subobj_hitgroup.pDesc = &hitgroup_desc;
  subobjects.push_back(subobj_hitgroup);
  
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
  size_t sbt_align = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
  D3D12_RESOURCE_DESC sbt_desc{};
  sbt_desc.DepthOrArraySize = 1;
  sbt_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  sbt_desc.Format = DXGI_FORMAT_UNKNOWN;
  sbt_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  sbt_desc.Width = sbt_align * 3;
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
  void* hitgroup_id = my_rt_pipeline->rt_state_object_props->GetShaderIdentifier(hitgroup_name);
  void* miss_shader_id = my_rt_pipeline->rt_state_object_props->GetShaderIdentifier(exports[2].c_str());

  char* mapped{};

  my_rt_pipeline->rt_sbt->Map(0, nullptr, (void**)&mapped);
  memcpy(mapped, raygen_shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  memcpy(mapped + sbt_align, hitgroup_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  memcpy(mapped + 2 * sbt_align, miss_shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  my_rt_pipeline->rt_sbt->Unmap(0, nullptr);


  {
    D3D12_DISPATCH_RAYS_DESC* drd = &(my_rt_pipeline->dispatch_rays_desc);
    drd->Width = WIN_W;
    drd->Height = WIN_H;
    drd->Depth = 1;
    D3D12_GPU_VIRTUAL_ADDRESS sbt_addr = my_rt_pipeline->rt_sbt->GetGPUVirtualAddress();
    drd->RayGenerationShaderRecord.StartAddress = sbt_addr;
    drd->RayGenerationShaderRecord.SizeInBytes = sbt_align;
    drd->HitGroupTable.StartAddress = sbt_addr + sbt_align;
    drd->HitGroupTable.SizeInBytes = sbt_align;
    drd->HitGroupTable.StrideInBytes = sbt_align;
    drd->MissShaderTable.StartAddress = sbt_addr + sbt_align * 2;
    drd->MissShaderTable.SizeInBytes = sbt_align;
    drd->MissShaderTable.StrideInBytes = sbt_align;
  }
}

template<class T>
void MyFramework::CreateVertexBuffer(std::vector<T>& verts, ID3D12Resource** res, D3D12_VERTEX_BUFFER_VIEW* vbv) {  // No index buffer
  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(T) * verts.size())),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(res)));
  void* mapped;
  (*res)->Map(0, nullptr, &mapped);
  memcpy(mapped, verts.data(), sizeof(T)*verts.size());
  (*res)->Unmap(0, nullptr);

  if (vbv) {
    vbv->BufferLocation = (*res)->GetGPUVirtualAddress();
    vbv->SizeInBytes = sizeof(T) * verts.size();
    vbv->StrideInBytes = sizeof(T);
  }
}

void MyFramework::CreateMyPipelineState(ID3D12PipelineState** pso,
  ID3D12RootSignature* root_sig) {
  /*
  * {
    LPCSTR SemanticName;
    UINT SemanticIndex;
    DXGI_FORMAT Format;
    UINT InputSlot;
    UINT AlignedByteOffset;
    D3D12_INPUT_CLASSIFICATION InputSlotClass;
    UINT InstanceDataStepRate;
  */
  D3D12_INPUT_ELEMENT_DESC input_descs[] = {
    {
      "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
    },
    {
      "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
    },
    {
      "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
    },
    {
      "DATA", 0, DXGI_FORMAT_R32_UINT, 0, 40,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
    },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.InputLayout = { input_descs, _countof(input_descs) };
  psoDesc.pRootSignature = root_sig;
  psoDesc.VS.pShaderBytecode = g_VertexShaderIvyLeafTriangle;
  psoDesc.VS.BytecodeLength = sizeof(g_VertexShaderIvyLeafTriangle);
  psoDesc.PS.pShaderBytecode = g_PixelShaderIvyLeafTriangle;
  psoDesc.PS.BytecodeLength = sizeof(g_PixelShaderIvyLeafTriangle);
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  //psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  psoDesc.DepthStencilState.DepthEnable = false;
  psoDesc.DepthStencilState.StencilEnable = false;
  psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  psoDesc.SampleDesc.Count = 1;
  CE(device12->CreateGraphicsPipelineState(
    &psoDesc, IID_PPV_ARGS(pso)));
}

void MyFramework::LoadTextureFromImage(ID3D12Resource** res, const char* filename) {
  int texHeight, texWidth, texChannels;
  stbi_uc* pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
  assert(pixels != nullptr && "Error loading image.");
  size_t imageSize = texHeight * texWidth * 4;
  printf("Image %s size %d x %d x %d\n", filename, texWidth, texHeight, texChannels);

  UINT64 rowPitch = static_cast<UINT64>(texWidth) * 4;
  UINT64 slicePitch = rowPitch * texHeight;

  D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
    DXGI_FORMAT_R8G8B8A8_UNORM, texWidth, texHeight, /*arraySize*/1, /*mipLevels*/1);

  CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
  HRESULT hr = device12->CreateCommittedResource(
    &defaultHeapProps,
    D3D12_HEAP_FLAG_NONE,
    &texDesc,
    D3D12_RESOURCE_STATE_COPY_DEST,   // 初始为 COPY_DEST，待会儿拷贝
    nullptr,
    IID_PPV_ARGS(res));
  if (FAILED(hr)) {
    stbi_image_free(pixels);
    return;
  }

  UINT64 uploadSize = GetRequiredIntermediateSize(*res, 0, 1);
  ID3D12Resource* upload{};
  CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
  auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
  hr = device12->CreateCommittedResource(
    &uploadHeapProps, D3D12_HEAP_FLAG_NONE,
    &uploadDesc,
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&upload));
  if (FAILED(hr)) {
    stbi_image_free(pixels);
    return;
  }

  D3D12_SUBRESOURCE_DATA subres{};
  subres.pData = pixels;
  subres.RowPitch = static_cast<LONG_PTR>(rowPitch);
  subres.SlicePitch = static_cast<LONG_PTR>(slicePitch);

  ID3D12CommandAllocator* command_allocator = GetCommandAllocator();
  ID3D12GraphicsCommandList4* command_list = GetGraphicsCommandList();
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));
  
  UpdateSubresources(command_list, *res, upload, 0, 0, 1, &subres);

  // 3.2 拷贝完切换到着色器可读
  auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
    *res, D3D12_RESOURCE_STATE_COPY_DEST,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  command_list->ResourceBarrier(1, &toSRV);

  // 4) 提交命令并等待完成（演示用，实际可并到你的 frame 提交里）
  command_list->Close();
  ID3D12CommandList* lists[] = { command_list };
  ID3D12CommandQueue* command_queue = GetCommandQueue();
  command_queue->ExecuteCommandLists(1, lists);

  WaitForPreviousFrame();
  upload->Release();
  
  stbi_image_free(pixels);
}

// Single geometry, no flag
void MyFramework::BuildBLAS(ID3D12Resource** blas_result,
  ID3D12Resource* vertex_buffer, uint32_t stride, uint32_t vertex_count) {
  D3D12_RAYTRACING_GEOMETRY_DESC geom_desc{};
  geom_desc.Triangles.VertexBuffer.StartAddress = vertex_buffer->GetGPUVirtualAddress();
  geom_desc.Triangles.VertexBuffer.StrideInBytes = stride;
  geom_desc.Triangles.VertexCount = vertex_count;
  geom_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  geom_desc.Triangles.IndexBuffer = 0;
  geom_desc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
  geom_desc.Triangles.IndexCount = 0;
  geom_desc.Triangles.Transform3x4 = 0;
  geom_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
  inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  inputs.NumDescs = 1;
  inputs.pGeometryDescs = &geom_desc;
  inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
  device12->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
  printf("BLAS prebuild info:\n");
  printf("  Scratch: %d\n", int(info.ScratchDataSizeInBytes));
  printf("  Result : %d\n", int(info.ResultDataMaxSizeInBytes));

  ID3D12Resource* blas_scratch{};
  D3D12_RESOURCE_DESC scratch_desc{};
  scratch_desc.Alignment = 0;
  scratch_desc.DepthOrArraySize = 1;
  scratch_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  scratch_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  scratch_desc.Format = DXGI_FORMAT_UNKNOWN;
  scratch_desc.Height = 1;
  scratch_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  scratch_desc.MipLevels = 1;
  scratch_desc.SampleDesc.Count = 1;
  scratch_desc.SampleDesc.Quality = 0;
  scratch_desc.Width = info.ScratchDataSizeInBytes;
  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &scratch_desc, D3D12_RESOURCE_STATE_COMMON,
    nullptr, IID_PPV_ARGS(&blas_scratch)));

  D3D12_RESOURCE_DESC result_desc = scratch_desc;
  result_desc.Width = info.ResultDataMaxSizeInBytes;
  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &result_desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
    nullptr, IID_PPV_ARGS(blas_result)));
  (*blas_result)->SetName(L"BLAS result");

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc{};
  build_desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  build_desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  build_desc.Inputs.NumDescs = 1;
  build_desc.Inputs.pGeometryDescs = &geom_desc;
  build_desc.DestAccelerationStructureData = {
    (*blas_result)->GetGPUVirtualAddress()
  };
  build_desc.ScratchAccelerationStructureData = {
    blas_scratch->GetGPUVirtualAddress()
  };
  build_desc.SourceAccelerationStructureData = 0;
  build_desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));

  command_list->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(*blas_result)));

  command_list->Close();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));

  WaitForPreviousFrame();

  blas_scratch->Release();
}

void MyFramework::BuildTLAS(ID3D12Resource** tlas_result,
  ID3D12Resource* blas_result) {
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
  tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_inputs.pGeometryDescs = nullptr;
  tlas_inputs.NumDescs = 1;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
  device12->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &info);
  printf("TLAS prebuild info:\n");
  printf("  Scratch: %d\n", int(info.ScratchDataSizeInBytes));
  printf("  Result : %d\n", int(info.ResultDataMaxSizeInBytes));
  int instance_desc_size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * 1;  // only 1 inst

  D3D12_RESOURCE_DESC scratch_desc{};
  scratch_desc.Alignment = 0;
  scratch_desc.DepthOrArraySize = 1;
  scratch_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  scratch_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  scratch_desc.Format = DXGI_FORMAT_UNKNOWN;
  scratch_desc.Height = 1;
  scratch_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  scratch_desc.MipLevels = 1;
  scratch_desc.SampleDesc.Count = 1;
  scratch_desc.SampleDesc.Quality = 0;
  scratch_desc.Width = info.ScratchDataSizeInBytes;

  ID3D12Resource* tlas_scratch{};

  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &scratch_desc, D3D12_RESOURCE_STATE_COMMON,
    nullptr, IID_PPV_ARGS(&tlas_scratch)));

  D3D12_RESOURCE_DESC tlas_result_desc = scratch_desc;
  tlas_result_desc.Width = info.ResultDataMaxSizeInBytes;
  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &tlas_result_desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
    nullptr, IID_PPV_ARGS(tlas_result)));
  (*tlas_result)->SetName(L"TLAS result");

  ID3D12Resource* tlas_instance{};

  D3D12_RESOURCE_DESC tlas_instance_desc = scratch_desc;
  tlas_instance_desc.Width = instance_desc_size;
  tlas_instance_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &tlas_instance_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr, IID_PPV_ARGS(&tlas_instance)));

  D3D12_RAYTRACING_INSTANCE_DESC* instance_desc;
  tlas_instance->Map(0, nullptr, (void**)(&instance_desc));
  ZeroMemory(instance_desc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
  instance_desc->InstanceID = 0;
  instance_desc->InstanceContributionToHitGroupIndex = 0;
  instance_desc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
  DirectX::XMMATRIX m = DirectX::XMMatrixIdentity();
  memcpy(instance_desc->Transform, &m, sizeof(instance_desc->Transform));
  instance_desc->AccelerationStructure = blas_result->GetGPUVirtualAddress();
  printf("blas_result's GPUVA is %p\n", blas_result->GetGPUVirtualAddress());
  instance_desc->InstanceMask = 0xFF;
  tlas_instance->Unmap(0, nullptr);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build_desc{};
  tlas_build_desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_build_desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  tlas_build_desc.Inputs.InstanceDescs = tlas_instance->GetGPUVirtualAddress();
  tlas_build_desc.Inputs.NumDescs = 1;
  tlas_build_desc.DestAccelerationStructureData = {
    (*tlas_result)->GetGPUVirtualAddress()
  };
  tlas_build_desc.ScratchAccelerationStructureData = {
    tlas_scratch->GetGPUVirtualAddress()
  };
  tlas_build_desc.SourceAccelerationStructureData = 0;
  tlas_build_desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));

  command_list->BuildRaytracingAccelerationStructure(&tlas_build_desc, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(*tlas_result)));

  command_list->Close();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));

  WaitForPreviousFrame();

  tlas_scratch->Release();
  tlas_instance->Release();
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
  const uint32_t num_verts = vertices.size();
  f->CreateRtGlobalRootSig(&global_rootsig);
  f->CreateRtOutputResource(&rt_output_resource);
  f->CreateCBVSRVUAVHeap(&cbvsrvuav_heap, nullptr, 5);
  f->CreateCBVSRVUAVHeap(&texture_srv_heap, nullptr, 2);
  f->CreateUAVTexture2D(rt_output_resource, cbvsrvuav_heap, 0);
  f->CreateVertexBuffer(vertices, &vertex_buffer, &vbv);
  f->CreateMyRtPipeline(&my_rt_pipeline, global_rootsig);
  f->CreateHelloTriangleRootSig(&rast_rootsig);
  f->CreateMyPipelineState(&rast_pipeline, rast_rootsig);
  f->LoadTextureFromImage(&diffuse_texture, "textures/Paris_ivy_leaf_a_diff.png");
  f->LoadTextureFromImage(&alpha_texture, "textures/Paris_ivy_leaf_a_mask.png");
  f->CreateSRVTexture2D(diffuse_texture, texture_srv_heap, 0);
  f->CreateSRVTexture2D(alpha_texture, texture_srv_heap, 1);
  f->BuildBLAS(&blas_result, vertex_buffer, sizeof(Vertex), num_verts);
  f->BuildTLAS(&tlas_result, blas_result);
  f->CreateSRVAccelerationStructure(tlas_result, cbvsrvuav_heap, 1);
  f->CreateSRVTexture2D(diffuse_texture, cbvsrvuav_heap, 2);
  f->CreateSRVTexture2D(alpha_texture, cbvsrvuav_heap, 3);
  f->CreateSRVBuffer(vertex_buffer, cbvsrvuav_heap, 4, num_verts, sizeof(Vertex));
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
  if (is_rt) {
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
  }
  else {
    command_list->SetPipelineState(rast_pipeline);
    command_list->SetGraphicsRootSignature(rast_rootsig);
    command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&texture_srv_heap);
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle_srv(texture_srv_heap->GetGPUDescriptorHandleForHeapStart());
    command_list->SetGraphicsRootDescriptorTable(0, handle_srv);
    D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f * (framework->WIN_W), 1.0f * (framework->WIN_H), 0.0f, 1.0f);
    D3D12_RECT scissor = CD3DX12_RECT(0, 0, long(framework->WIN_W), long(framework->WIN_H));
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor);
    command_list->OMSetRenderTargets(1, &handle_rtv, false, nullptr);
    float blend_factor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    command_list->OMSetBlendFactor(blend_factor);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 1, &vbv);
    command_list->DrawInstanced(6, 1, 0, 0);
    ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  }
  CE(command_list->Close());

  ID3D12CommandQueue* command_queue = framework->GetCommandQueue();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(framework->Present());
  framework->WaitForPreviousFrame();
}

void MyParisIvyLeafScene::Update(float secs) {
}

void MyParisIvyLeafScene::OnKeyDown(uint32_t k) {
  if (k == VK_SPACE) {
    is_rt = !is_rt;
  }
}