#include "scene.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdexcept>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

extern HWND g_hwnd;
extern unsigned WIN_W, WIN_H;

static void CE(HRESULT x) {
  if (FAILED(x)) {
    printf("ERROR: %X\n", x);
    throw std::exception();
  }
}

void DX12ClearScreenScene::Update(float secs) {
}

void DX12ClearScreenScene::InitDeviceAndCommandQ() {
  unsigned dxgi_factory_flags = 0;
  bool use_warp_device = false;

  ID3D12Debug* debug_controller;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
    debug_controller->EnableDebugLayer();
    dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
    printf("Enabling debug layer\n");
  }

  CE(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory)));
  if (use_warp_device) {
    IDXGIAdapter* warp_adapter;
    CE(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)));
    CE(D3D12CreateDevice(warp_adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    printf("Created a WARP device=%p\n", device);;
  }
  else {
    IDXGIAdapter1* hw_adapter;
    for (int idx = 0; factory->EnumAdapters1(idx, &hw_adapter) != DXGI_ERROR_NOT_FOUND; idx++) {
      DXGI_ADAPTER_DESC1 desc;
      hw_adapter->GetDesc1(&desc);
      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
      else {
        CE(D3D12CreateDevice(hw_adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        printf("Created a hardware device = %p\n", device);
        break;
      }
    }
  }

  assert(device != nullptr);

  {
    D3D12_COMMAND_QUEUE_DESC desc = {
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    };
    CE(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&command_queue)));
  }

  CE(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));

  CE(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
    command_allocator, nullptr, IID_PPV_ARGS(&command_list)));
  CE(command_list->Close());

  CE(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  fence_value = 1;
  fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void DX12ClearScreenScene::InitSwapChain() {
  DXGI_SWAP_CHAIN_DESC1 desc = {
    .Width = WIN_W,
    .Height = WIN_H,
    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
    .SampleDesc = {
      .Count = 1,
    },
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    .BufferCount = 2,
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
  };
  IDXGISwapChain1* swapchain1;
  CE(factory->CreateSwapChainForHwnd(command_queue,
    g_hwnd, &desc, nullptr, nullptr, &swapchain1));
  swapchain = (IDXGISwapChain3*)swapchain1;
  CE(factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER));
  frame_index = swapchain->GetCurrentBackBufferIndex();

  printf("Created swapchain.\n");

  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = 2,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };
    CE(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtv_heap)));
    printf("Created RTV heap.\n");
  }

  rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(
    D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart());
  for (int i = 0; i < FRAME_COUNT; i++) {
    CE(swapchain->GetBuffer(i, IID_PPV_ARGS(&rendertargets[i])));
    device->CreateRenderTargetView(rendertargets[i], nullptr, rtv_handle);
    rtv_handle.Offset(1, rtv_descriptor_size);

    wchar_t buf[100];
    _snwprintf_s(buf, sizeof(buf), L"Render Target Frame %d", i);
    rendertargets[i]->SetName(buf);
  }
  printf("Created RTV and pointed RTVs to backbuffers.\n");
}

void DX12ClearScreenScene::InitPipeline() {
  {
    ID3DBlob* error = nullptr;
    unsigned compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
      "VSMain", "vs_5_0", compile_flags, 0, &VS, &error);
    if (error) printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));

    D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
      "PSMain", "ps_5_0", compile_flags, 0, &PS, &error);
    if (error) printf("Error compiling PS: %s\n", (char*)(error->GetBufferPointer()));
  }

  {
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc;
    root_sig_desc.Init_1_1(0, NULL, 0, NULL,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature, error;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&root_sig_desc,
      D3D_ROOT_SIGNATURE_VERSION_1_1,
      &signature, &error);
    if (signature == nullptr) {
      printf("Could not serialize root signature: %s\n",
        (char*)(error->GetBufferPointer()));
    }

    CE(device->CreateRootSignature(0, signature->GetBufferPointer(),
      signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
    root_signature->SetName(L"Root signature");
  }

  D3D12_INPUT_ELEMENT_DESC input_element_desc[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {
    .pRootSignature = root_signature,
    .VS = CD3DX12_SHADER_BYTECODE(VS),
    .PS = CD3DX12_SHADER_BYTECODE(PS),
    .BlendState = {
      .RenderTarget = {
        {
          .BlendEnable = TRUE,
          .SrcBlend = D3D12_BLEND_SRC_ALPHA,
          .DestBlend = D3D12_BLEND_INV_SRC_ALPHA,
          .BlendOp = D3D12_BLEND_OP_ADD,
          .SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA,
          .DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA,
          .BlendOpAlpha = D3D12_BLEND_OP_ADD,
          .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
        },
      },
    },
    .RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
    .DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT),
    .InputLayout = {
      input_element_desc,
      2
    },
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .NumRenderTargets = 1,
    .RTVFormats = {
      DXGI_FORMAT_R8G8B8A8_UNORM,
    },
    .DSVFormat = DXGI_FORMAT_D32_FLOAT,
    .SampleDesc = {
      .Count = 1,
    },
  };

  CE(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state)));
}

DX12ClearScreenScene::DX12ClearScreenScene() {
  InitDeviceAndCommandQ();
  InitSwapChain();
  InitPipeline();
}

// https://stackoverflow.com/questions/65315241/how-can-i-fix-requires-l-value
template <class T>
constexpr auto& keep(T&& x) noexcept {
  return x;
}

void DX12ClearScreenScene::WaitForPreviousFrame() {
  int value = fence_value++;
  CE(command_queue->Signal(fence, value));
  if (fence->GetCompletedValue() < value) {
    CE(fence->SetEventOnCompletion(value, fence_event));
    CE(WaitForSingleObject(fence_event, INFINITE));
  }
  frame_index = swapchain->GetCurrentBackBufferIndex();
}

void DX12ClearScreenScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, pipeline_state));
  command_list->SetGraphicsRootSignature(root_signature);

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    frame_index, rtv_descriptor_size);
  float bg_color[] = { 1.0f, 1.0f, 0.8f, 1.0f };
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    rendertargets[frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    rendertargets[frame_index],
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PRESENT)));
  CE(command_list->Close());
  command_queue->ExecuteCommandLists(1,
    (ID3D12CommandList* const*)&command_list);
  CE(swapchain->Present(1, 0));
  WaitForPreviousFrame();
}