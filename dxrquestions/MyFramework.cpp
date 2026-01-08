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

#include <nvapi.h>
#ifndef NO_OMM
#include <omm.hpp>
const omm::Cpu::BakeResultDesc* bakeOmmForMask(uint32_t level);
#endif

// https://stackoverflow.com/questions/65315241/how-can-i-fix-requires-l-value
template <class T>
static constexpr auto& keep(T&& x) noexcept {
  return x;
}

#ifndef CE
#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}
#endif

size_t AlignUp(uint32_t s, uint32_t a) {
    return a * ((s - 1) / a + 1);
}

void GlmMat4ToDirectXMatrixColMajor(DirectX::XMMATRIX* out, const glm::mat4& m)
{
    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 4; c++)
        {
            out->r[c].m128_f32[r] = m[c][r];
        }
    }
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
  InitSwapchain(WIN_W, WIN_H);
}

void MyFramework::InitSwapchain(uint32_t w, uint32_t h) {
  // Swapchain
  {
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FRAME_COUNT;
    swapChainDesc.Width = w;
    swapChainDesc.Height = h;
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
  uint32_t num_uav, uint32_t num_srv, uint32_t num_cbv,
  D3D12_ROOT_SIGNATURE_FLAGS flags, bool has_sampler,
  bool add_nvapi_uav, uint32_t nvapi_uav_idx) {
  const uint32_t NTYPES = 3;
  D3D12_ROOT_PARAMETER root_param{};  // Just one root parameter
  std::vector<D3D12_DESCRIPTOR_RANGE> desc_ranges;
  desc_ranges.reserve(NTYPES + 1);

  D3D12_DESCRIPTOR_RANGE_TYPE range_types[] = {
    D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
    D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
    D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
  };
  uint32_t view_counts[] = {
    num_uav,
    num_srv,
    num_cbv,
  };

  //if (num_uav > 0) {
  for (uint32_t i=0; i<NTYPES; i++) {
    if (view_counts[i] > 0) {
      D3D12_DESCRIPTOR_RANGE desc_range{};
      desc_range.RangeType = range_types[i];
      desc_range.NumDescriptors = view_counts[i];
      desc_range.BaseShaderRegister = 0;
      desc_range.RegisterSpace = 0;
      desc_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
      desc_ranges.push_back(desc_range);

      if (i == 0 && add_nvapi_uav == true) {
        desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        desc_range.NumDescriptors = 1;
        desc_range.BaseShaderRegister = nvapi_uav_idx;
        desc_range.RegisterSpace = 0;
        desc_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        desc_ranges.push_back(desc_range);
      }
    }
  }

  if (add_nvapi_uav) {
    D3D12_DESCRIPTOR_RANGE desc_range{};
    desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    desc_range.NumDescriptors = (uint32_t)(-1);
    desc_range.BaseShaderRegister = 0;
    desc_range.RegisterSpace = 2;
    desc_range.OffsetInDescriptorsFromTableStart = 0;
    desc_ranges.push_back(desc_range);

    desc_range.RegisterSpace = 3;
    desc_ranges.push_back(desc_range);
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

void MyFramework::CreateNvapiEnabledGlobalRootSig(ID3D12RootSignature** out_rootsig, uint32_t num_uav, uint32_t num_srv, uint32_t num_cbv, bool add_nvapi_uav, uint32_t nvapi_uav_idx) {
  do_CreateRootSig(out_rootsig, num_uav, num_srv, num_cbv, D3D12_ROOT_SIGNATURE_FLAG_NONE, true, add_nvapi_uav, nvapi_uav_idx);
}

void MyFramework::CreateGlobalRootSig(ID3D12RootSignature** out_rootsig, uint32_t num_uav, uint32_t num_srv, uint32_t num_cbv) {
  do_CreateRootSig(out_rootsig, num_uav, num_srv, num_cbv, D3D12_ROOT_SIGNATURE_FLAG_NONE, true, false, 0);
}

void MyFramework::CreateHelloTriangleRootSig(ID3D12RootSignature** out_rootsig) {
  do_CreateRootSig(out_rootsig, 0, 2, 0, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, true, false, 0);
}

void MyFramework::CreateComputePipeline(ID3D12PipelineState** pso, ID3D12RootSignature* root_sig, const void* shader_bytecode, uint32_t shader_bytecode_length) {
  D3D12_COMPUTE_PIPELINE_STATE_DESC cpsd{};
  cpsd.pRootSignature = root_sig;
  cpsd.CS.pShaderBytecode = shader_bytecode;
  cpsd.CS.BytecodeLength = shader_bytecode_length;
  CE(device12->CreateComputePipelineState(&cpsd, IID_PPV_ARGS(pso)));
}

void MyFramework::CreateRtOutputResource(ID3D12Resource** out_res, uint32_t w, uint32_t h) {
  D3D12_RESOURCE_DESC desc{};
  desc.DepthOrArraySize = 1;
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  desc.Width = w;
  desc.Height = h;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE, &desc,
    D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
    IID_PPV_ARGS(out_res)));
}

void MyFramework::CreateRtOutputResource(ID3D12Resource** out_res) {
  CreateRtOutputResource(out_res, WIN_W, WIN_H);
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
    if (is_nvapi_available && use_rt_validation) {
      NvAPI_D3D12_FlushRaytracingValidationMessages(device12);
    }
  }

  if (swapchain) {
    frame_index = swapchain->GetCurrentBackBufferIndex();
  }
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

void MyFramework::CreateUAVUintBuffer(ID3D12Resource* res,
  uint32_t num_elts, uint32_t stride,
  ID3D12DescriptorHeap* h, uint32_t idx) {
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.CounterOffsetInBytes = 0;
  uav_desc.Buffer.FirstElement = 0;
  uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
  uav_desc.Buffer.NumElements = num_elts;
  uav_desc.Buffer.StructureByteStride = stride;
  CD3DX12_CPU_DESCRIPTOR_HANDLE uav_handle(h->GetCPUDescriptorHandleForHeapStart(), idx, cbvsrvuav_descriptor_size);
  device12->CreateUnorderedAccessView(res, nullptr, &uav_desc, uav_handle);
}

void MyFramework::CreateNullUAV(ID3D12DescriptorHeap* h, uint32_t idx) {
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.CounterOffsetInBytes = 0;
  uav_desc.Buffer.FirstElement = 0;
  uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
  uav_desc.Buffer.NumElements = 256;
  uav_desc.Buffer.StructureByteStride = 256;
  CD3DX12_CPU_DESCRIPTOR_HANDLE uav_handle(h->GetCPUDescriptorHandleForHeapStart(), idx, cbvsrvuav_descriptor_size);
  device12->CreateUnorderedAccessView(nullptr, nullptr, &uav_desc, uav_handle);
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

void MyFramework::CreateCBVBuffer(ID3D12Resource* res, ID3D12DescriptorHeap* h, uint32_t idx, uint32_t len) {
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
  cbv_desc.BufferLocation = res->GetGPUVirtualAddress();
  cbv_desc.SizeInBytes = len;
  CD3DX12_CPU_DESCRIPTOR_HANDLE cbv_handle(h->GetCPUDescriptorHandleForHeapStart(), idx, cbvsrvuav_descriptor_size);
  device12->CreateConstantBufferView(&cbv_desc, cbv_handle);
}

uint32_t MyFramework::GetCBVSRVUAVDescriptorSize() {
  return cbvsrvuav_descriptor_size;
}

void MyFramework::CreateMyRtPipeline(MyRtPipeline* my_rt_pipeline,
  ID3D12RootSignature* global_rootsig, const struct MyRtShaderListInfo& my_shaders) {
  // Params
  std::vector<std::wstring> exports;
  const wchar_t* shdrs[] = {
    my_shaders.raygen_shader,
    my_shaders.closest_hit_shader,
    my_shaders.miss_shader,
    my_shaders.anyhit_shader,
    my_shaders.intersection_shader,
  };
  for (uint32_t i = 0; i < _countof(shdrs); i++) {
    if (shdrs[i] != nullptr) {
      exports.push_back(std::wstring(shdrs[i]));
    }
  }

  const wchar_t* hitgroup_name = my_shaders.hitgroup_name;

  std::vector<D3D12_STATE_SUBOBJECT> subobjects;
  subobjects.reserve(16);

  D3D12_DXIL_LIBRARY_DESC dxil_lib_desc{};
  std::vector<D3D12_EXPORT_DESC> dxil_lib_exports(exports.size());
  for (uint32_t i = 0; i < exports.size(); i++) {
    dxil_lib_exports[i].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[i].ExportToRename = nullptr;
    dxil_lib_exports[i].Name = exports[i].c_str();
  }

  dxil_lib_desc.DXILLibrary.pShaderBytecode = my_shaders.dxil_lib_bytecode;
  dxil_lib_desc.DXILLibrary.BytecodeLength = my_shaders.dxil_lib_length;
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
  hitgroup_desc.Type = my_shaders.hitgroup_type;
  hitgroup_desc.HitGroupExport = hitgroup_name;
  hitgroup_desc.ClosestHitShaderImport = my_shaders.closest_hit_shader;
  hitgroup_desc.AnyHitShaderImport = my_shaders.anyhit_shader;
  hitgroup_desc.IntersectionShaderImport = my_shaders.intersection_shader;
  subobj_hitgroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
  subobj_hitgroup.pDesc = &hitgroup_desc;
  subobjects.push_back(subobj_hitgroup);
  
  // Shader config
  D3D12_STATE_SUBOBJECT subobj_shaderconfig{};
  subobj_shaderconfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  D3D12_RAYTRACING_SHADER_CONFIG shader_config{};
  shader_config.MaxPayloadSizeInBytes = 20;
  shader_config.MaxAttributeSizeInBytes = 8;
  subobj_shaderconfig.pDesc = &shader_config;
  subobjects.push_back(subobj_shaderconfig);

  // Global rootsignature
  D3D12_STATE_SUBOBJECT subobj_global_rootsig{};
  D3D12_GLOBAL_ROOT_SIGNATURE grs{};
  grs.pGlobalRootSignature = global_rootsig;
  subobj_global_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  subobj_global_rootsig.pDesc = &grs;
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
    drd->HitGroupTable.StrideInBytes = 0;
    drd->MissShaderTable.StartAddress = sbt_addr + sbt_align * 2;
    drd->MissShaderTable.SizeInBytes = sbt_align;
    drd->MissShaderTable.StrideInBytes = 0;
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
  //printf("BLAS prebuild info:\n");
  //printf("  Scratch: %d\n", int(info.ScratchDataSizeInBytes));
  //printf("  Result : %d\n", int(info.ResultDataMaxSizeInBytes));

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

// Builds a Single-Instance TLAS.
void MyFramework::BuildTLAS(ID3D12Resource** tlas_result,
  ID3D12Resource* blas_result) {
  
  std::vector<D3D12_RAYTRACING_INSTANCE_DESC> inst_descs(1);
  D3D12_RAYTRACING_INSTANCE_DESC* instance_desc = &(inst_descs[0]);
  instance_desc->InstanceID = 0;
  instance_desc->InstanceContributionToHitGroupIndex = 0;
  instance_desc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
  DirectX::XMMATRIX m = DirectX::XMMatrixIdentity();
  memcpy(instance_desc->Transform, &m, sizeof(instance_desc->Transform));
  instance_desc->AccelerationStructure = blas_result->GetGPUVirtualAddress();
  instance_desc->InstanceMask = 0xFF;

  BuildTLAS(tlas_result, blas_result, inst_descs);
}

void MyFramework::BuildTLAS(ID3D12Resource** tlas_result, ID3D12Resource* blas_result, const std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& inst_descs) {
  size_t N = inst_descs.size();
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
  tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_inputs.pGeometryDescs = nullptr;
  tlas_inputs.NumDescs = N;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
  device12->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &info);
  int instance_desc_size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * N;

  ID3D12Resource* tlas_scratch{};
  CreateBufferForUAVAccess(info.ScratchDataSizeInBytes, &tlas_scratch);
  CreateBufferForAS(info.ResultDataMaxSizeInBytes, tlas_result);

  (*tlas_result)->SetName(L"TLAS result");

  ID3D12Resource* tlas_instances{};
  CreateBufferForCPUSideData(nullptr, instance_desc_size, &tlas_instances);

  D3D12_RAYTRACING_INSTANCE_DESC* instance_descs;
  tlas_instances->Map(0, nullptr, (void**)(&instance_descs));
  memcpy(instance_descs, inst_descs.data(), instance_desc_size);
  tlas_instances->Unmap(0, nullptr);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build_desc{};
  tlas_build_desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_build_desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  tlas_build_desc.Inputs.InstanceDescs = tlas_instances->GetGPUVirtualAddress();
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
  tlas_instances->Release();
}

void MyFramework::CreateBufferForCPUSideData(void* data, uint32_t len, ID3D12Resource** res) {
  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(len)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(res)));
  if (data != nullptr) {
    void* mapped;
    (*res)->Map(0, nullptr, &mapped);
    memcpy(mapped, data, len);
    (*res)->Unmap(0, nullptr);
  }
}

void MyFramework::do_CreateBuffer(ID3D12Resource** res,
  uint32_t len,
  D3D12_RESOURCE_FLAGS flags,
  D3D12_RESOURCE_STATES state,
  D3D12_HEAP_TYPE heap_type)
{
  D3D12_RESOURCE_DESC scratch_desc{};
  scratch_desc.Alignment = 0;
  scratch_desc.DepthOrArraySize = 1;
  scratch_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  scratch_desc.Flags = flags;
  scratch_desc.Format = DXGI_FORMAT_UNKNOWN;
  scratch_desc.Height = 1;
  scratch_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  scratch_desc.MipLevels = 1;
  scratch_desc.SampleDesc.Count = 1;
  scratch_desc.SampleDesc.Quality = 0;
  scratch_desc.Width = len;

  CE(device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(heap_type)),
    D3D12_HEAP_FLAG_NONE,
    &scratch_desc, state,
    nullptr, IID_PPV_ARGS(res)));
}

void MyFramework::CreateBufferForUAVAccess(uint32_t len, ID3D12Resource** res) {
  do_CreateBuffer(res, len, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
    D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_TYPE_DEFAULT);
}

void MyFramework::CreateBufferForCPUAccess(uint32_t len, ID3D12Resource** res) {
  do_CreateBuffer(res, len,
    D3D12_RESOURCE_FLAG_NONE,
    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_HEAP_TYPE_READBACK);
}

void MyFramework::CreateBufferForAS(uint32_t len, ID3D12Resource** res) {
  do_CreateBuffer(res, len, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_HEAP_TYPE_DEFAULT);
}

void MyFramework::BuildDummyOMM(ID3D12Resource* vertex_buffer, uint32_t stride,
  ID3D12Resource** blas_result_omm, ID3D12Resource** tlas_result_omm) {
#ifndef NO_OMM
  ID3D12Resource* omm_index_data_resource;
  ID3D12Resource* omm_array_data_resource, * omm_desc_array_resource;
  // Stolen from OpacityMicroMapsHelper
  // Fill input array
  const omm::Cpu::BakeResultDesc* res_desc = bakeOmmForMask(5);
  printf("OMM data size:        %u\n", res_desc->arrayDataSize);
  printf("OMM desc array count: %u\n", res_desc->descArrayCount);
  static_assert(sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_DESC) == sizeof(omm::Cpu::OpacityMicromapDesc));
  uint32_t omm_desc_size = sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_DESC);
  uint32_t desc_array_size = omm_desc_size * res_desc->descArrayCount;
  CreateBufferForCPUSideData((void*)res_desc->arrayData, res_desc->arrayDataSize, &omm_array_data_resource);
  CreateBufferForCPUSideData((void*)res_desc->descArray, desc_array_size, &omm_desc_array_resource);

  NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_INPUTS omm_inputs{};
  omm_inputs.flags = NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BUILD_FLAG_NONE;
  omm_inputs.numOMMUsageCounts = res_desc->descArrayHistogramCount;
  NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT ommuc{};
  ommuc.count = res_desc->indexHistogram[0].count;
  ommuc.format = (NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT)(res_desc->indexHistogram[0].format);
  ommuc.subdivisionLevel = res_desc->indexHistogram[0].subdivisionLevel;
  omm_inputs.pOMMUsageCounts = &ommuc;
  omm_inputs.inputBuffer = (omm_array_data_resource)->GetGPUVirtualAddress();
  omm_inputs.perOMMDescs.StartAddress = (omm_desc_array_resource)->GetGPUVirtualAddress();
  omm_inputs.perOMMDescs.StrideInBytes = omm_desc_size;

  // Prebuild info
  NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO omm_prebuild_info{};
  NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS omm_getprebuildinfo_params{};
  omm_getprebuildinfo_params.pDesc = &omm_inputs;
  omm_getprebuildinfo_params.pInfo = &omm_prebuild_info;
  omm_getprebuildinfo_params.version = NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS_VER;

  NvAPI_Status status = NvAPI_D3D12_GetRaytracingOpacityMicromapArrayPrebuildInfo(device12, &omm_getprebuildinfo_params);
  if (status != NVAPI_OK) {
    printf("Oh! Could not get OMM prebuild info. exiting\n");
    exit(0);
  }
  printf("OMM: scratch=%u, result=%u\n",
    (uint32_t)(omm_prebuild_info.scratchDataSizeInBytes),
    (uint32_t)(omm_prebuild_info.resultDataMaxSizeInBytes));

  // OMM-BLAS build info
  uint32_t omm_index_data_size = res_desc->indexCount;
  DXGI_FORMAT dxgi_omm_index_format = DXGI_FORMAT_UNKNOWN;
  switch (res_desc->indexFormat) {
  case omm::IndexFormat::UINT_16:
    omm_index_data_size *= sizeof(uint16_t); 
    dxgi_omm_index_format = DXGI_FORMAT_R16_UINT;
    break;
  case omm::IndexFormat::UINT_32:
    omm_index_data_size *= sizeof(uint32_t);
    dxgi_omm_index_format = DXGI_FORMAT_R32_UINT;
    break;
  default: break;
  }
  CreateBufferForCPUSideData((void*)res_desc->indexBuffer, omm_index_data_size, &omm_index_data_resource);

  // Actually build OMM array using Baker's Data
  ID3D12Resource* omm_array_resource{};
  ID3D12Resource* scratch_resource{};
  size_t scratch_size = omm_prebuild_info.scratchDataSizeInBytes;
  CreateBufferForUAVAccess(omm_prebuild_info.resultDataMaxSizeInBytes, &omm_array_resource);
  CreateBufferForUAVAccess(scratch_size, &scratch_resource);
  NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC omm_array_desc{};
  omm_array_desc.destOpacityMicromapArrayData = omm_array_resource->GetGPUVirtualAddress();
  omm_array_desc.inputs = omm_inputs;
  omm_array_desc.scratchOpacityMicromapArrayData = scratch_resource->GetGPUVirtualAddress();
  NVAPI_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_PARAMS build_omm_params{};
  build_omm_params.numPostbuildInfoDescs = 0;
  build_omm_params.pPostbuildInfoDescs = nullptr;
  build_omm_params.version = NVAPI_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_PARAMS_VER;
  build_omm_params.pDesc = &omm_array_desc;

  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));

  status = NvAPI_D3D12_BuildRaytracingOpacityMicromapArray(command_list, &build_omm_params);
  if (status != NVAPI_OK)
  {
    printf("[FAIL]: NvAPI_D3D12_BuildRaytracingOpacityMicromapArray\n");
    std::abort();
  }
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(scratch_resource)));
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(omm_array_resource)));
  command_list->Close();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
  WaitForPreviousFrame();

  scratch_resource->Release();
  printf("Successfully built OMM array.\n");

  NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX geom_desc{};
  geom_desc.flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
  geom_desc.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES_EX;
  geom_desc.ommTriangles = {};
  D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC* triangles = &(geom_desc.ommTriangles.triangles);
  triangles->IndexBuffer = NULL;
  triangles->IndexFormat = DXGI_FORMAT_UNKNOWN;
  triangles->IndexCount = 0;
  triangles->VertexCount = 6;
  triangles->VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  triangles->VertexBuffer.StartAddress = vertex_buffer->GetGPUVirtualAddress();
  triangles->VertexBuffer.StrideInBytes = stride;

  // BLAS prebuild info
  NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX input_desc_ex{};
  input_desc_ex.type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  input_desc_ex.flags = NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE_EX;
  input_desc_ex.numDescs = 1;
  input_desc_ex.descsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  input_desc_ex.geometryDescStrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX);
  input_desc_ex.pGeometryDescs = &geom_desc;

  NVAPI_D3D12_RAYTRACING_GEOMETRY_OMM_ATTACHMENT_DESC* attachment = &(geom_desc.ommTriangles.ommAttachment);
  attachment->opacityMicromapArray = omm_array_resource->GetGPUVirtualAddress();
  attachment->opacityMicromapBaseLocation = 0;
  attachment->opacityMicromapIndexBuffer = {};
  attachment->opacityMicromapIndexBuffer.StartAddress = omm_index_data_resource->GetGPUVirtualAddress();
  attachment->opacityMicromapIndexBuffer.StrideInBytes = (dxgi_omm_index_format == DXGI_FORMAT_R16_UINT) ? 2 : 4;;
  attachment->opacityMicromapIndexFormat = dxgi_omm_index_format;
  attachment->numOMMUsageCounts = res_desc->indexHistogramCount;
  assert(attachment->numOMMUsageCounts == 1);
  attachment->pOMMUsageCounts = &ommuc;

  // TLAS prebuild info for obtaining scratch size
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
  tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_inputs.pGeometryDescs = nullptr;
  tlas_inputs.NumDescs = 1;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_prebuild_info{};
  device12->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_prebuild_info);
  printf("TLAS: scratch=%u, result=%u\n", tlas_prebuild_info.ScratchDataSizeInBytes, tlas_prebuild_info.ResultDataMaxSizeInBytes);

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_prebuild_info = {};
  NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS blas_get_prebuild_info_params = {};
  blas_get_prebuild_info_params.pInfo = &blas_prebuild_info;
  blas_get_prebuild_info_params.pDesc = &input_desc_ex;
  blas_get_prebuild_info_params.version = NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS_VER;
  status = NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx(device12, &blas_get_prebuild_info_params);
  if (status != NVAPI_OK)
  {
    printf("[FAIL]: NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx\n");
    std::abort();
  }
  printf("BLAS: scratch=%u, result=%u\n",
    (uint32_t)(blas_prebuild_info.ScratchDataSizeInBytes),
    (uint32_t)(blas_prebuild_info.ResultDataMaxSizeInBytes));

  scratch_size = std::max(blas_prebuild_info.ScratchDataSizeInBytes, tlas_prebuild_info.ScratchDataSizeInBytes);
  CreateBufferForUAVAccess(scratch_size, &scratch_resource);

  // Build BLAS
  CreateBufferForUAVAccess(blas_prebuild_info.ResultDataMaxSizeInBytes, blas_result_omm);
  NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC_EX blas_build_desc{};
  blas_build_desc.destAccelerationStructureData = (*blas_result_omm)->GetGPUVirtualAddress();
  blas_build_desc.inputs = input_desc_ex;
  blas_build_desc.scratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
  NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS blas_build_params{};
  blas_build_params.numPostbuildInfoDescs = 0;
  blas_build_params.pPostbuildInfoDescs = nullptr;
  blas_build_params.pDesc = &blas_build_desc;
  blas_build_params.version = NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS_VER;

  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));

  status = NvAPI_D3D12_BuildRaytracingAccelerationStructureEx(command_list, &blas_build_params);
  if (status != NVAPI_OK)
  {
    printf("[FAIL]: NvAPI_D3D12_BuildRaytracingAccelerationStructureEx\n");
    std::abort();
  }
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(scratch_resource)));
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(*blas_result_omm)));
  command_list->Close();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
  WaitForPreviousFrame();
  printf("Successfully built BLAS.\n");

  BuildTLAS(tlas_result_omm, *blas_result_omm);  // Builds TLAS consisting of just 1 instance

  // baked omm data, not actual output
  omm_index_data_resource->Release();
  omm_array_data_resource->Release();
  omm_desc_array_resource->Release();
  omm_array_resource->Release();
  scratch_resource->Release();
#endif
}

void MyFramework::BuildDummyLSS(
  ID3D12Resource** blas_result,
  ID3D12Resource** tlas_result,
  ID3D12Resource* lss_pos_resource, ID3D12Resource* lss_pos_resource1,
  ID3D12Resource* lss_radii_resource,
  ID3D12Resource* lss_indices_list_resource,
  ID3D12Resource* lss_indices_successive_resource,
  uint32_t vert_count,
  NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE endcap_mode,
  NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT prim_format,
  int case_idx,
  bool is_update
) {
  NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC lss_desc{};
  lss_desc.endcapMode = endcap_mode;
  if (case_idx == 2) {
      lss_desc.indexBuffer.StartAddress = NULL;
      lss_desc.indexBuffer.StrideInBytes = 0;
      lss_desc.indexFormat = DXGI_FORMAT_UNKNOWN;
      lss_desc.indexCount = 0;
  }
  else {
    lss_desc.indexBuffer.StartAddress = lss_indices_list_resource->GetGPUVirtualAddress();
    lss_desc.indexBuffer.StrideInBytes = sizeof(uint32_t);
    lss_desc.indexFormat = DXGI_FORMAT_R32_UINT;
    lss_desc.indexCount = ((vert_count)-1) * 2;
  }
  lss_desc.primitiveCount = vert_count - 1;
  lss_desc.primitiveFormat = NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_LIST;
  lss_desc.vertexCount = vert_count;
  lss_desc.vertexPositionBuffer.StartAddress = lss_pos_resource->GetGPUVirtualAddress();
  lss_desc.vertexPositionBuffer.StrideInBytes = sizeof(float) * 3;
  lss_desc.vertexPositionFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  lss_desc.vertexRadiusBuffer.StartAddress = lss_radii_resource->GetGPUVirtualAddress();
  lss_desc.vertexRadiusBuffer.StrideInBytes = sizeof(float);
  lss_desc.vertexRadiusFormat = DXGI_FORMAT_R32_FLOAT;

  NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX geom_descs[2]{};
  geom_descs[0].flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
  geom_descs[0].type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_LSS_EX;
  geom_descs[0].lss = lss_desc;

  if (case_idx == 0) {
    geom_descs[1] = geom_descs[0];
    NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC lss_desc1 = lss_desc;
    lss_desc1.indexBuffer.StartAddress = lss_indices_successive_resource->GetGPUVirtualAddress();
    lss_desc1.indexCount = vert_count - 1;
    lss_desc1.vertexPositionBuffer.StartAddress = lss_pos_resource1->GetGPUVirtualAddress();
    lss_desc1.primitiveFormat = NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_SUCCESSIVE_IMPLICIT;
    geom_descs[1].lss = lss_desc1;
  }

  NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX blas_input_ex{};
  blas_input_ex.type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  blas_input_ex.flags = NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE_EX;
  switch (case_idx) {
  case 0:
    blas_input_ex.numDescs = 2; break;
  case 1: case 2:
    blas_input_ex.numDescs = 1; break;
  }
  blas_input_ex.descsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  blas_input_ex.geometryDescStrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX);
  blas_input_ex.pGeometryDescs = &(geom_descs[0]);

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_prebuild_info = {};
  NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS blas_get_prebuild_info_params = {};
  blas_get_prebuild_info_params.pInfo = &blas_prebuild_info;
  blas_get_prebuild_info_params.pDesc = &blas_input_ex;
  blas_get_prebuild_info_params.version = NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS_VER;
  NvAPI_Status status = NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx(device12, &blas_get_prebuild_info_params);
  if (status != NVAPI_OK)
  {
    printf("[FAIL]: NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx\n");
    std::abort();
  }

  ID3D12Resource* scratch_resource{};
  CreateBufferForUAVAccess(blas_prebuild_info.ScratchDataSizeInBytes, &scratch_resource);

  // Build BLAS
  CreateBufferForUAVAccess(blas_prebuild_info.ResultDataMaxSizeInBytes, blas_result);
  NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC_EX blas_build_desc{};
  blas_build_desc.destAccelerationStructureData = (*blas_result)->GetGPUVirtualAddress();
  blas_build_desc.inputs = blas_input_ex;
  blas_build_desc.scratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
  NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS blas_build_params{};
  blas_build_params.numPostbuildInfoDescs = 0;
  blas_build_params.pPostbuildInfoDescs = nullptr;
  blas_build_params.pDesc = &blas_build_desc;
  blas_build_params.version = NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS_VER;

  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));

  status = NvAPI_D3D12_BuildRaytracingAccelerationStructureEx(command_list, &blas_build_params);
  if (status != NVAPI_OK)
  {
    printf("[FAIL]: NvAPI_D3D12_BuildRaytracingAccelerationStructureEx\n");
    std::abort();
  }
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(scratch_resource)));
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(*blas_result)));
  command_list->Close();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
  WaitForPreviousFrame();
  BuildTLAS(tlas_result, *blas_result);

  scratch_resource->Release();
}

void MyFramework::BuildDummyTriNVAPI(
  ID3D12Resource** blas_result,
  ID3D12Resource** tlas_result,
  ID3D12Resource* tri_verts_resource,
  uint32_t vert_count
) {
  NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX tri_desc{};
  tri_desc.triangles.IndexBuffer = 0;
  tri_desc.triangles.IndexCount = 0;
  tri_desc.triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
  tri_desc.triangles.Transform3x4 = 0;
  tri_desc.triangles.VertexBuffer.StartAddress = tri_verts_resource->GetGPUVirtualAddress();
  tri_desc.triangles.VertexBuffer.StrideInBytes = 12;
  tri_desc.triangles.VertexCount = vert_count;
  tri_desc.triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  tri_desc.flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
  tri_desc.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES_EX;
  
  NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX blas_input_ex{};
  blas_input_ex.type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  blas_input_ex.flags = NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE_EX;
  blas_input_ex.numDescs = 1;
  blas_input_ex.descsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  blas_input_ex.geometryDescStrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX);
  blas_input_ex.pGeometryDescs = &tri_desc;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_prebuild_info = {};
  NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS blas_get_prebuild_info_params = {};
  blas_get_prebuild_info_params.pInfo = &blas_prebuild_info;
  blas_get_prebuild_info_params.pDesc = &blas_input_ex;
  blas_get_prebuild_info_params.version = NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS_VER;
  NvAPI_Status status = NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx(device12, &blas_get_prebuild_info_params);
  if (status != NVAPI_OK)
  {
    printf("[FAIL]: NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx\n");
    std::abort();
  }

  ID3D12Resource* scratch_resource{};
  CreateBufferForUAVAccess(blas_prebuild_info.ScratchDataSizeInBytes, &scratch_resource);

  // Build BLAS
  CreateBufferForAS(blas_prebuild_info.ResultDataMaxSizeInBytes, blas_result);
  NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC_EX blas_build_desc{};
  blas_build_desc.destAccelerationStructureData = (*blas_result)->GetGPUVirtualAddress();
  blas_build_desc.inputs = blas_input_ex;
  blas_build_desc.scratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
  NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS blas_build_params{};
  blas_build_params.numPostbuildInfoDescs = 0;
  blas_build_params.pPostbuildInfoDescs = nullptr;
  blas_build_params.pDesc = &blas_build_desc;
  blas_build_params.version = NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS_VER;

  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(scratch_resource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)));

  status = NvAPI_D3D12_BuildRaytracingAccelerationStructureEx(command_list, &blas_build_params);
  if (status != NVAPI_OK)
  {
    printf("[FAIL]: NvAPI_D3D12_BuildRaytracingAccelerationStructureEx\n");
    std::abort();
  }

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(scratch_resource)));
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(*blas_result)));
  command_list->Close();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
  WaitForPreviousFrame();
  BuildTLAS(tlas_result, *blas_result);

  scratch_resource->Release();
}

void MyFramework::BuildDummyProcedural(
  ID3D12Resource** blas_result,
  ID3D12Resource** tlas_result,
  ID3D12Resource* proc_aabb_buffer,
  uint32_t aabb_count
) {
  D3D12_RAYTRACING_GEOMETRY_DESC proc_desc{};
  proc_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
  proc_desc.AABBs.AABBCount = aabb_count;
  proc_desc.AABBs.AABBs.StartAddress = proc_aabb_buffer->GetGPUVirtualAddress();
  proc_desc.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
  proc_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
  blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  blas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  blas_inputs.NumDescs = 1;
  blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  blas_inputs.pGeometryDescs = &proc_desc;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
  tlas_inputs = blas_inputs;
  tlas_inputs.NumDescs = 1;
  tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_inputs.InstanceDescs = {};

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_buildinfo{}, tlas_buildinfo{};
  device12->GetRaytracingAccelerationStructurePrebuildInfo(&blas_inputs, &blas_buildinfo);
  device12->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_buildinfo);
  
  CreateBufferForAS(blas_buildinfo.ResultDataMaxSizeInBytes, blas_result);
  CreateBufferForAS(tlas_buildinfo.ResultDataMaxSizeInBytes, tlas_result);

  size_t scratch_sz = std::max(blas_buildinfo.ScratchDataSizeInBytes, tlas_buildinfo.ScratchDataSizeInBytes);
  ID3D12Resource* scratch_resource{};
  CreateBufferForUAVAccess(scratch_sz, &scratch_resource);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_build_desc{};
  blas_build_desc.Inputs = blas_inputs;
  blas_build_desc.DestAccelerationStructureData = (*blas_result)->GetGPUVirtualAddress();
  blas_build_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
  
  ID3D12Resource* instance_descs{};
  D3D12_RAYTRACING_INSTANCE_DESC instance_descs_cpu{};
  instance_descs_cpu.Transform[0][0] = 1;
  instance_descs_cpu.Transform[1][1] = 1;
  instance_descs_cpu.Transform[2][2] = 1;
  instance_descs_cpu.InstanceMask = 0xFF;
  instance_descs_cpu.AccelerationStructure = (*blas_result)->GetGPUVirtualAddress();
  CreateBufferForCPUSideData(&instance_descs_cpu, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), &instance_descs);

  tlas_inputs.InstanceDescs = instance_descs->GetGPUVirtualAddress();

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build_desc{};
  tlas_build_desc.Inputs = tlas_inputs;
  tlas_build_desc.DestAccelerationStructureData = (*tlas_result)->GetGPUVirtualAddress();
  tlas_build_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();

  command_list->Reset(command_allocator, nullptr);
  command_list->BuildRaytracingAccelerationStructure(&blas_build_desc, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(*blas_result)));
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(scratch_resource)));
  command_list->BuildRaytracingAccelerationStructure(&tlas_build_desc, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(*tlas_result)));
  command_list->Close();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  WaitForPreviousFrame();

  scratch_resource->Release();
  instance_descs->Release();
}

// Validation callback
static void __stdcall myValidationMessageCallback(void* pUserData, NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY severity, const char* messageCode, const char* message, const char* messageDetails)
{
  const char* severityString = "unknown";
  switch (severity)
  {
    case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_ERROR: severityString = "error"; break;
    case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_WARNING: severityString = "warning"; break;
  }
  fprintf(stderr, "Ray Tracing Validation message: %s: [%s] %s\n%s", severityString, messageCode, message, messageDetails);
  fflush(stderr);
}

bool MyFramework::InitNVAPI() {
  NvAPI_Status status = NvAPI_Initialize();
  if (status == NVAPI_OK) {
    printf("NVAPI inited.\n");
    is_nvapi_available = true;
  }
  else {
    printf("NVAPI init failed = %d\n", (int)status);
    return false;
  }

#ifndef NDEBUG
  InitRayTracingValidation();
#endif

  size_t lss_data_size = sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC);
  printf("sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC) = %zu\n", lss_data_size);
  NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAPS lssCaps = NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAP_NONE;
  NvAPI_D3D12_GetRaytracingCaps(device12, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_LINEAR_SWEPT_SPHERES, &lssCaps, sizeof(NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAPS));
  printf("lssCaps = %u, %s\n",
    (uint32_t)(lssCaps),
    lssCaps == NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAP_NONE ? "LSS not supported" : "LSS supported");
  if (lssCaps != NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAP_NONE) {
    is_lss_available = true;
  }

  NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAPS ommCaps = NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_NONE;
  NvAPI_D3D12_GetRaytracingCaps(device12, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_OPACITY_MICROMAP, &ommCaps, sizeof(ommCaps));
  printf("ommCaps = %u, %s\n",
    (uint32_t)(ommCaps),
    ommCaps == NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_NONE ? "OMM not supported" : "OMM supported");
  if (ommCaps != NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_NONE) {
    is_omm_available = true;
  }

  NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS params = {};
  params.version = NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER;
  params.flags = 0;
  if (is_lss_available) {
    params.flags |= NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_LSS_SUPPORT;
    params.flags |= NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_SPHERE_SUPPORT;
  }
  if (is_omm_available) {
    params.flags |= NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_OMM_SUPPORT;
  }
  status = NvAPI_D3D12_SetCreatePipelineStateOptions(device12, &params);
  if (status == NVAPI_OK) {
    printf("Successfully set pipeline creation options.\n");
  }
  else {
    printf("Oh! could not set pipeline creation options.\n");
  }
  return true;
}

void MyFramework::Deinit() {
  if (device12) {
    device12->Release();
  }
}

void MyFramework::SetHwnd(HWND h) {
  this->hwnd = h;
}

void MyFramework::InitRayTracingValidation() {
  if (!is_nvapi_available) return;
  void* myCallbackData;
  void* handle;
  NvAPI_Status ret = NvAPI_D3D12_EnableRaytracingValidation(device12, NVAPI_D3D12_RAYTRACING_VALIDATION_FLAG_NONE);
  if (ret != NVAPI_OK) {
    printf("Error enabling RT validation: %d\n", ret);
  }
  NvAPI_D3D12_RegisterRaytracingValidationMessageCallback(
    device12, &myValidationMessageCallback,
    (void*)&myCallbackData, &handle);
  use_rt_validation = true;
}

bool MyFramework::IsOMMSupported() {
  return is_omm_available;
}

#ifdef USE_IMGUI
// Stolen from https://github.com/ocornut/imgui/blob/master/examples/example_win32_directx12/main.cpp
// Simple free list based allocator
struct ExampleDescriptorHeapAllocator
{
  ID3D12DescriptorHeap* Heap = nullptr;
  D3D12_DESCRIPTOR_HEAP_TYPE  HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
  D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu;
  D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu;
  UINT                        HeapHandleIncrement;
  ImVector<int>               FreeIndices;

  void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
  {
    IM_ASSERT(Heap == nullptr && FreeIndices.empty());
    Heap = heap;
    D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
    HeapType = desc.Type;
    HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
    HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
    HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
    FreeIndices.reserve((int)desc.NumDescriptors);
    for (int n = desc.NumDescriptors; n > 0; n--)
      FreeIndices.push_back(n - 1);
  }
  void Destroy()
  {
    Heap = nullptr;
    FreeIndices.clear();
  }
  void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
  {
    IM_ASSERT(FreeIndices.Size > 0);
    int idx = FreeIndices.back();
    FreeIndices.pop_back();
    out_cpu_desc_handle->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
    out_gpu_desc_handle->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
  }
  void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
  {
    int cpu_idx = (int)((out_cpu_desc_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
    int gpu_idx = (int)((out_gpu_desc_handle.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
    IM_ASSERT(cpu_idx == gpu_idx);
    FreeIndices.push_back(cpu_idx);
  }
};
struct ExampleDescriptorHeapAllocator g_imguiSrvDescHeapAlloc;
#endif

#ifdef USE_IMGUI
void MyFramework::InitImGUIForGLFW(GLFWwindow* w)
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

  // Setup Platform/Renderer backends
  ImGui_ImplDX12_InitInfo init_info = {};
  init_info.Device = this->device12;
  init_info.CommandQueue = this->command_queue;
  init_info.NumFramesInFlight = FRAME_COUNT;
  init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;  // Or your render target format.

  D3D12_DESCRIPTOR_HEAP_DESC desc{};
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = 64;
  desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  device12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&imgui_heap));
  g_imguiSrvDescHeapAlloc.Create(device12, imgui_heap);

  init_info.SrvDescriptorHeap = imgui_heap;
  init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) {
    return g_imguiSrvDescHeapAlloc.Alloc(out_cpu_handle, out_gpu_handle);
    };
  init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {
    return g_imguiSrvDescHeapAlloc.Free(cpu_handle, gpu_handle);
    };
  ImGui_ImplDX12_Init(&init_info);
  ImGui_ImplGlfw_InitForOther(w, true);
}
#endif

void MyFramework::CreateLineDrawingPipeline() {
  // 1. Shader
  // 2. Rootsig
  // 3. Pipeline
  // 4. CB
}

// ======================================= Helper functions ===============
void ResourceBarrierTransition(ID3D12GraphicsCommandList4* cmdlist,
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
  const uint32_t num_pixels = f->WIN_H * f->WIN_W;
  const uint32_t cb_size = 256;  // Multiple of 256
  f->CreateNvapiEnabledGlobalRootSig(&global_rootsig, 2, 4, 1, false, 0);
  f->CreateRtOutputResource(&rt_output_resource);
  f->CreateBufferForUAVAccess(num_pixels * 4, &my_debug_resource);
  my_debug_resource->SetName(L"My debug resource");
  f->CreateCBVSRVUAVHeap(&cbvsrvuav_heap, nullptr, 7);
  f->CreateCBVSRVUAVHeap(&texture_srv_heap, nullptr, 2);
  f->CreateUAVTexture2D(rt_output_resource, cbvsrvuav_heap, 0);
  f->CreateUAVUintBuffer(my_debug_resource, num_pixels, sizeof(uint32_t), cbvsrvuav_heap, 1);
  f->CreateVertexBuffer(vertices, &vertex_buffer, &vbv);
  MyFramework::MyRtShaderListInfo info{};
  info.raygen_shader = L"MyRaygenShader";
  info.closest_hit_shader = L"MyClosestHitShader";
  info.miss_shader = L"MyMissShader";
  info.anyhit_shader = L"MyAnyHitShader";
  info.hitgroup_name = L"MyHitGroup";
  info.dxil_lib_bytecode = (void*)g_RaytracingShaders;
  info.dxil_lib_length = sizeof(g_RaytracingShaders);
  f->CreateMyRtPipeline(&my_rt_pipeline, global_rootsig, info);
  f->CreateHelloTriangleRootSig(&rast_rootsig);
  f->CreateMyPipelineState(&rast_pipeline, rast_rootsig);
  f->LoadTextureFromImage(&diffuse_texture, "textures/Paris_ivy_leaf_a_diff.png");
  f->LoadTextureFromImage(&alpha_texture, "textures/Paris_ivy_leaf_a_mask.png");
  f->CreateSRVTexture2D(diffuse_texture, texture_srv_heap, 0);
  f->CreateSRVTexture2D(alpha_texture, texture_srv_heap, 1);
  f->BuildBLAS(&blas_result, vertex_buffer, sizeof(Vertex), num_verts);
  f->BuildTLAS(&tlas_result, blas_result);
  f->CreateSRVAccelerationStructure(tlas_result, cbvsrvuav_heap, 2);
  f->CreateSRVTexture2D(diffuse_texture, cbvsrvuav_heap, 3);
  f->CreateSRVTexture2D(alpha_texture, cbvsrvuav_heap, 4);
  f->CreateSRVBuffer(vertex_buffer, cbvsrvuav_heap, 5, num_verts, sizeof(Vertex));
  f->CreateBufferForCPUSideData(nullptr, cb_size, &my_cb_resource);
  f->CreateCBVBuffer(my_cb_resource, cbvsrvuav_heap, 6, cb_size);
  f->CreateBufferForCPUAccess(num_pixels * sizeof(uint32_t), &my_debug_resource_cpu);

  // OMM stuff.
  if (f->IsOMMSupported()) {
    f->BuildDummyOMM(vertex_buffer, sizeof(Vertex), &blas_result_omm, &tlas_result_omm);
    f->CreateCBVSRVUAVHeap(&cbvsrvuav_heap_omm, nullptr, 7);
    f->CreateUAVTexture2D(rt_output_resource, cbvsrvuav_heap_omm, 0);
    f->CreateUAVUintBuffer(my_debug_resource, num_pixels, sizeof(uint32_t), cbvsrvuav_heap_omm, 1);
    f->CreateSRVAccelerationStructure(tlas_result_omm, cbvsrvuav_heap_omm, 2);
    f->CreateSRVTexture2D(diffuse_texture, cbvsrvuav_heap_omm, 3);
    f->CreateSRVTexture2D(alpha_texture, cbvsrvuav_heap_omm, 4);
    f->CreateSRVBuffer(vertex_buffer, cbvsrvuav_heap_omm, 5, num_verts, sizeof(Vertex));
    f->CreateCBVBuffer(my_cb_resource, cbvsrvuav_heap_omm, 6, cb_size);
  }
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
    if (my_cb_cpu.is_dump_debuginfo) {
      ResourceBarrierTransition(command_list, my_debug_resource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    command_list->SetComputeRootSignature(global_rootsig);
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav;
    if (!is_omm) {
      command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&cbvsrvuav_heap);
      handle_uav = CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvsrvuav_heap->GetGPUDescriptorHandleForHeapStart(), 0, framework->GetCBVSRVUAVDescriptorSize());
    }
    else {
      command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&cbvsrvuav_heap_omm);
      handle_uav = CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvsrvuav_heap_omm->GetGPUDescriptorHandleForHeapStart(), 0, framework->GetCBVSRVUAVDescriptorSize());
    }
    command_list->SetComputeRootDescriptorTable(0, handle_uav);
    command_list->SetPipelineState1(my_rt_pipeline.rt_state_object);
    command_list->DispatchRays(&(my_rt_pipeline.dispatch_rays_desc));
    ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    command_list->CopyResource(rendertarget, rt_output_resource);
    ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    if (my_cb_cpu.is_dump_debuginfo) {
      ResourceBarrierTransition(command_list, my_debug_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
      command_list->CopyResource(my_debug_resource_cpu, my_debug_resource);
      ResourceBarrierTransition(command_list, my_debug_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    }
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

  if (my_cb_cpu.is_dump_debuginfo) {
    uint32_t* mapped;
    my_debug_resource_cpu->Map(0, nullptr, (void**)&mapped);
    printf("Print something\n");
    const uint32_t npixels = framework->WIN_H * framework->WIN_W;
    const uint32_t stepsize = npixels / 100;
    for (uint32_t i = 0; i < npixels; i += stepsize) {
      printf("[%u] = %x\n", i, mapped[i]);
    }
    if (prim_idx_mapping_state == PrimIdxMappingState::GetNonOMMResult) {
      prim_idxes_non_omm.clear();
      prim_idxes_omm.clear();
      for (uint32_t i = 0; i < npixels; i += stepsize) {
        prim_idxes_non_omm.push_back(mapped[i]);
      }
      prim_idx_mapping_state = PrimIdxMappingState::GetOMMResult;
      is_omm = true;
    } else if (prim_idx_mapping_state == PrimIdxMappingState::GetOMMResult) {
      for (uint32_t i = 0; i < npixels; i += stepsize) {
        prim_idxes_omm.push_back(mapped[i]);
      }
      int mapped_miss = -999, mapped_prim0 = -999, mapped_prim1 = -999;
      for (uint32_t i = 0; i < prim_idxes_omm.size(); i ++) {
        int pidx_nonomm = prim_idxes_non_omm[i];
        int pidx_omm = prim_idxes_omm[i];
        auto do_update_map = [&](int& mapped_pidx, int target) {
          printf("nonomm=%d  omm=%d  tgt=%d\n",
            pidx_nonomm, pidx_omm, target);
          if (pidx_omm == -1) return;  // HACK
          if (pidx_nonomm == target) {
            if (mapped_pidx == -999) {
              mapped_pidx = pidx_omm;
            }
            else {
              if (mapped_pidx != pidx_omm) {
                mapped_pidx = -998;
              }
            }
          }
        };
        do_update_map(mapped_miss, -1);
        do_update_map(mapped_prim0, 0);
        do_update_map(mapped_prim1, 1);
      }
      printf("[mapping] miss=0x%x, prim0=0x%x, prim1=0x%x\n",
        mapped_miss, mapped_prim0, mapped_prim1);
      my_cb_cpu.omm_primidx0 = mapped_prim0;
      my_cb_cpu.omm_primidx1 = mapped_prim1;
      prim_idx_mapping_state = PrimIdxMappingState::NotStarted;
    }
    my_debug_resource_cpu->Unmap(0, nullptr);
    if (prim_idx_mapping_state == PrimIdxMappingState::NotStarted) {
      my_cb_cpu.is_dump_debuginfo = false;
    }
  }
}

void MyParisIvyLeafScene::Update(float secs) {
  void* mapped;
  my_cb_cpu.is_omm = is_omm;
  my_cb_resource->Map(0, nullptr, &mapped);
  memcpy(mapped, &my_cb_cpu, sizeof(MyConstantBufferStruct));
  my_cb_resource->Unmap(0, nullptr);
}

void MyParisIvyLeafScene::OnKeyDown(uint32_t k) {
  if (k == VK_SPACE) {
    is_rt = !is_rt;
  }
  else if (k == 'O' || k == 'o') {
    is_omm = !(is_omm);
    printf("is_omm = %d\n", is_omm);
  }
  else if (k == 'd' || k == 'D') {  // Dump + Debug (map OMM's primidx to normal idx)
    is_omm = false;
    my_cb_cpu.is_dump_debuginfo = true;
    prim_idx_mapping_state = PrimIdxMappingState::GetNonOMMResult;
  }
}