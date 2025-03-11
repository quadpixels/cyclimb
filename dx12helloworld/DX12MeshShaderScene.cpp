#include "scene.hpp"
#include "util.hpp"
#include <dxcapi.h>

#include "d3dx12.h"
#include "dxcapi.use.h"
static dxc::DxcDllSupport dxc_support;
namespace dxc {
  char const* kDxCompilerLib = "dxcompiler";
}

extern int WIN_W, WIN_H;
extern ID3D12Device* g_device12;
extern IDXGIFactory4* g_factory;
extern ID3D12CommandQueue* g_command_queue;
extern ID3D12Fence* g_fence;
extern int g_fence_value;
extern HANDLE g_fence_event;
extern IDXGISwapChain3* g_swapchain;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern ID3D12Resource* g_rendertargets[];
extern unsigned g_rtv_descriptor_size;
extern int g_frame_index;
void WaitForPreviousFrame();

DX12MeshShaderScene::DX12MeshShaderScene() {
  // Build mesh shader and pixel shader
  dxc_support.Initialize();

  IDxcCompiler* dxc_compiler;
  IDxcOperationResult* dxc_opr_result;
  CE(dxc_support.CreateInstance(CLSID_DxcCompiler, &dxc_compiler));

  ID3DBlob* ms_blob{};
  ID3DBlob* ps_blob{};
  ID3DBlob* source{};

  CE(D3DReadFileToBlob(L"mesh_shader_scene.hlsl", &source));
  HRESULT hr = dxc_compiler->Compile((IDxcBlob*)source, L"MS", L"MSMain", L"ms_6_5", nullptr, 0, nullptr, 0, nullptr, &dxc_opr_result);
  IDxcBlobEncoding* dxc_error;
  CE(dxc_opr_result->GetErrorBuffer(&dxc_error));
  if (SUCCEEDED(hr)) {
    if (dxc_error && dxc_error->GetBufferPointer()) {
      printf("SM 6.5 MS build message: %s\n", dxc_error->GetBufferPointer());
    }
    else {
      printf("SM 6.5 MS build success.\n");
    }
    CE(dxc_opr_result->GetResult((IDxcBlob**)(&ms_blob)));
  }
  else {
    CE(dxc_opr_result->GetErrorBuffer(&dxc_error));
    printf("SM 6.5 MS build failed: %s\n", (char*)(dxc_error->GetBufferPointer()));
  }
  
  hr = dxc_compiler->Compile((IDxcBlob*)source, L"PS", L"PSMain", L"ps_6_5", nullptr, 0, nullptr, 0, nullptr, &dxc_opr_result);
  CE(dxc_opr_result->GetErrorBuffer(&dxc_error));
  if (SUCCEEDED(hr)) {
    if (dxc_error && dxc_error->GetBufferPointer()) {
      printf("SM 6.5 PS build message: %s\n", dxc_error->GetBufferPointer());
    }
    else {
      printf("SM 6.5 PS build success.\n");
    }
    CE(dxc_opr_result->GetResult((IDxcBlob**)(&ps_blob)));
  }
  else {
    CE(dxc_opr_result->GetErrorBuffer(&dxc_error));
    printf("SM 6.5 PS build failed: %s\n", (char*)(dxc_error->GetBufferPointer()));
  }

  // Root signature
  D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
  rootsig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  rootsig_desc.NumParameters = 0;
  rootsig_desc.NumStaticSamplers = 0;
  rootsig_desc.pParameters = nullptr;
  rootsig_desc.pStaticSamplers = nullptr;

  ID3DBlob* signature{}, * error{};
  D3D12SerializeRootSignature(&rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
  if (error)
  {
    printf("Error: %s\n", (char*)(error->GetBufferPointer()));
  }
  CE(g_device12->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
  root_signature->SetName(L"MeshShaderScene root signature");
  signature->Release();
  if (error)
    error->Release();

  // Command allocator
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)));

  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, nullptr, IID_PPV_ARGS(&command_list)));
  command_list->Close();

  // PSO
  D3DX12_MESH_SHADER_PIPELINE_STATE_DESC mspso_desc{};
  mspso_desc.pRootSignature = root_signature;
  mspso_desc.MS.pShaderBytecode = ms_blob->GetBufferPointer();
  mspso_desc.MS.BytecodeLength = ms_blob->GetBufferSize();
  mspso_desc.PS.pShaderBytecode = ps_blob->GetBufferPointer();
  mspso_desc.PS.BytecodeLength = ps_blob->GetBufferSize();
  mspso_desc.NumRenderTargets = 1;
  mspso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  mspso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
  mspso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);      // CW front; cull back
  mspso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);                // Opaque
  mspso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // Less-equal depth test w/ writes; no stencil
  mspso_desc.SampleMask = UINT_MAX;
  mspso_desc.SampleDesc = DefaultSampleDesc();

  CD3DX12_PIPELINE_MESH_STATE_STREAM stream_desc(mspso_desc);
  
  D3D12_PIPELINE_STATE_STREAM_DESC pss_desc{};
  pss_desc.pPipelineStateSubobjectStream = &stream_desc;
  pss_desc.SizeInBytes = sizeof(stream_desc);

  ID3D12Device2* device2{};
  CE(g_device12->QueryInterface(IID_PPV_ARGS(&device2)));
  CE(device2->CreatePipelineState(&pss_desc, IID_PPV_ARGS(&pipeline_state)));
}

void DX12MeshShaderScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, pipeline_state));
  command_list->SetGraphicsRootSignature(root_signature);

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);
  float bg_color[] = { 1.0f, 0.9f, 0.8f, 1.0f };
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));
  
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  command_list->OMSetRenderTargets(1, &handle_rtv, false, nullptr);
  D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1.0f * WIN_W, 1.0f * WIN_H, -100.0f, 100.0f);
  D3D12_RECT scissor = CD3DX12_RECT(0, 0, long(WIN_W), long(WIN_H));
  command_list->RSSetViewports(1, &viewport);
  command_list->RSSetScissorRects(1, &scissor);
  command_list->DispatchMesh(2, 1, 1);

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

void DX12MeshShaderScene::Update(float secs) {
}