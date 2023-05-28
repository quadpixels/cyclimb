#include <assert.h>
#include <stdio.h>
#include <wchar.h>

#include <stdexcept>

#include <d3d12.h>
#include <D3Dcompiler.h>
#include "d3dx12.h"
#include <wrl/client.h>
#include <dxgi1_4.h>

const int FRAME_COUNT = 2;

ID3D12Device* g_device;
IDXGIFactory4* g_factory;
ID3D12CommandQueue* g_command_queue;
ID3D12CommandAllocator* g_command_allocator;
ID3D12GraphicsCommandList* g_command_list;

IDXGISwapChain3* g_swapchain;

ID3D12DescriptorHeap* g_rtv_heap;
ID3D12Resource* g_rendertargets[FRAME_COUNT];
unsigned g_rtv_descriptor_size;

ID3DBlob *g_VS, *g_PS;
ID3D12RootSignature* g_root_signature;

ID3D12PipelineState* g_pipeline_state;

int g_frame_index = 0;

CD3DX12_VIEWPORT g_viewport;
CD3DX12_RECT g_scissorrect;

extern HWND g_hwnd;
extern unsigned WIN_W, WIN_H;

using Microsoft::WRL::ComPtr;

void CE(HRESULT x) {
  if (FAILED(x)) {
    printf("ERROR: %X\n", x);
    throw std::exception();
  }
}

// 1. IDXGIFactory, IDXGIAdapter, ID3D12Device, ID3D12CommandQueue, ID3D12CommandAllocator, ID3D12GraphicsCommandList
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
    CE(D3D12CreateDevice(warp_adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)));
    printf("Created a WARP device=%p\n", g_device);;
  }
  else {
    IDXGIAdapter1* hw_adapter;
    for (int idx = 0; g_factory->EnumAdapters1(idx, &hw_adapter) != DXGI_ERROR_NOT_FOUND; idx++) {
      DXGI_ADAPTER_DESC1 desc;
      hw_adapter->GetDesc1(&desc);
      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
      else {
        CE(D3D12CreateDevice(hw_adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)));
        printf("Created a hardware device = %p\n", g_device);
        break;
      }
    }
  }

  assert(g_device != nullptr);

  g_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, WIN_W, WIN_H, -100.0f, 100.0f);
  g_scissorrect = CD3DX12_RECT(0, 0, WIN_W, WIN_H);

  {
    D3D12_COMMAND_QUEUE_DESC desc = {
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    };
    CE(g_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_command_queue)));
  }

  CE(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&g_command_allocator)));

  CE(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
    g_command_allocator, nullptr, IID_PPV_ARGS(&g_command_list)));
}

// 2. IDXGISwapChain
void InitSwapChain() {
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
  IDXGISwapChain1* swapchain;
  CE(g_factory->CreateSwapChainForHwnd(g_command_queue,
    g_hwnd, &desc, nullptr, nullptr, &swapchain));
  g_swapchain = (IDXGISwapChain3*)swapchain;
  CE(g_factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER));
  g_frame_index = g_swapchain->GetCurrentBackBufferIndex();

  printf("Created swapchain.\n");

  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = 2,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };
    CE(g_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_rtv_heap)));
    printf("Created RTV heap.\n");
  }

  g_rtv_descriptor_size = g_device->GetDescriptorHandleIncrementSize(
    D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(g_rtv_heap->GetCPUDescriptorHandleForHeapStart());
  for (int i = 0; i < FRAME_COUNT; i++) {
    CE(g_swapchain->GetBuffer(i, IID_PPV_ARGS(&g_rendertargets[i])));
    g_device->CreateRenderTargetView(g_rendertargets[i], nullptr, rtv_handle);
    rtv_handle.Offset(1, g_rtv_descriptor_size);

    wchar_t buf[100];
    _snwprintf_s(buf, sizeof(buf), L"Render Target Frame %d", i);
    g_rendertargets[i]->SetName(buf);
  }
  printf("Created RTV and pointed RTVs to backbuffers.\n");
}

void InitPipeline() {
  {
    ID3DBlob* error = nullptr;
    unsigned compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
      "VSMain", "vs_5_0", compile_flags, 0, &g_VS, &error);
    if (error) printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));

    D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
      "PSMain", "ps_5_0", compile_flags, 0, &g_PS, &error);
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

    CE(g_device->CreateRootSignature(0, signature->GetBufferPointer(),
      signature->GetBufferSize(), IID_PPV_ARGS(&g_root_signature)));
    g_root_signature->SetName(L"Root signature");
  }

  D3D12_INPUT_ELEMENT_DESC input_element_desc[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {
    .pRootSignature = g_root_signature,
    .VS = CD3DX12_SHADER_BYTECODE(g_VS),
    .PS = CD3DX12_SHADER_BYTECODE(g_PS),
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

  CE(g_device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&g_pipeline_state)));
}

// 4. 

void Render_DX12() {
  CE(g_command_allocator->Reset());

}