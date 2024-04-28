#include "scene.hpp"
#include <util.hpp>

#include <d3dx12.h>
#include <DirectXMath.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <dxcapi.h>

extern int WIN_W, WIN_H;
extern ID3D12Device5* g_device12;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern int g_rtv_descriptor_size;
extern int g_frame_index;
extern ID3D12Resource* g_rendertargets[];
extern ID3D12CommandQueue* g_command_queue;
extern IDXGISwapChain3* g_swapchain;

void WaitForPreviousFrame();

#include "dxcapi.use.h"
static dxc::DxcDllSupport dxc_support;
namespace dxc {
  char const* kDxCompilerLib = "dxcompiler";
}


TriangleScene::TriangleScene() {
  dxc_support.Initialize();
  InitDX12Stuff();
  CreateAS();
}

void TriangleScene::InitDX12Stuff() {
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, nullptr, IID_PPV_ARGS(&command_list)));
  CE(command_list->Close());

  // Root Sig
  D3D12_DESCRIPTOR_RANGE desc_range{};
  desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  desc_range.RegisterSpace = 0;
  desc_range.BaseShaderRegister = 0;
  desc_range.NumDescriptors = 1;
  desc_range.OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER root_params[3];
  root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_params[0].DescriptorTable.NumDescriptorRanges = 1;
  root_params[0].DescriptorTable.pDescriptorRanges = &desc_range;
  root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  root_params[1].Descriptor.RegisterSpace = 0;
  root_params[1].Descriptor.ShaderRegister = 0;  // u0
  root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_params[2].Descriptor.RegisterSpace = 0;
  root_params[2].Descriptor.ShaderRegister = 0;  // b0
  root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
  rootsig_desc.NumParameters = 3;
  rootsig_desc.pParameters = root_params;
  rootsig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  rootsig_desc.NumStaticSamplers = 0;

  ID3DBlob* sigblob;
  ID3DBlob* error;
  HRESULT hr;
  hr = D3D12SerializeRootSignature(
    &rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigblob, &error);
  assert(SUCCEEDED(hr));
  if (error) {
    printf("Error creating root signature: %s\n", (char*)error->GetBufferPointer());
  }
  CE(g_device12->CreateRootSignature(0, sigblob->GetBufferPointer(), sigblob->GetBufferSize(), IID_PPV_ARGS(&root_sig)));
  sigblob->Release();

  // Shader (hello triangle)
  ID3DBlob* vs_blob = nullptr, * ps_blob = nullptr;
  UINT compile_flags = 0;
  
  if (false) {  // Shader Model 6.0+ cannot use D3DCompileFromFile
    D3DCompileFromFile(L"shaders/hellotriangle_rayquery.hlsl", nullptr, nullptr,
      "VSMain", "vs_4_0", 0, 0, &vs_blob, &error);
    if (error) printf("Error creating VS: %s\n", (char*)(error->GetBufferPointer()));
    D3DCompileFromFile(L"shaders/hellotriangle_rayquery.hlsl", nullptr, nullptr,
      "PSMain", "ps_4_0", 0, 0, &ps_blob, &error);
    if (error) printf("Error creating PS: %s\n", (char*)(error->GetBufferPointer()));
  }
  else {  // Reference: https://posts.tanki.ninja/2019/07/11/Using-DXC-In-Practice/
    IDxcCompiler* dxc_compiler;
    IDxcOperationResult* dxc_opr_result;
    CE(dxc_support.CreateInstance(CLSID_DxcCompiler, &dxc_compiler));
    ID3DBlob* source;
    CE(D3DReadFileToBlob(L"shaders/hellotriangle_rayquery.hlsl", &source));
    HRESULT hr = dxc_compiler->Compile((IDxcBlob*)source, L"PS", L"PSMain", L"ps_6_5", nullptr, 0, nullptr, 0, nullptr, &dxc_opr_result);
    IDxcBlobEncoding* dxc_error;
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

    hr = dxc_compiler->Compile((IDxcBlob*)source, L"VS", L"VSMain", L"vs_6_5", nullptr, 0, nullptr, 0, nullptr, &dxc_opr_result);
    CE(dxc_opr_result->GetErrorBuffer(&dxc_error));
    if (SUCCEEDED(hr)) {
      if (dxc_error && dxc_error->GetBufferPointer()) {
        printf("SM 6.5 VS build message: %s\n", dxc_error->GetBufferPointer());
      }
      else {
        printf("SM 6.5 VS build success.\n");
      }
      CE(dxc_opr_result->GetResult((IDxcBlob**)(&vs_blob)));
    }
    else {
      printf("SM 6.5 VS build failed: %s\n", (char*)(dxc_error->GetBufferPointer()));
    }
  }

  // Input layout for PSO
  D3D12_INPUT_ELEMENT_DESC input_element_desc[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  // PSO
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.InputLayout.NumElements = 2;
  pso_desc.InputLayout.pInputElementDescs = input_element_desc;
  pso_desc.pRootSignature = root_sig;
  pso_desc.VS = CD3DX12_SHADER_BYTECODE(vs_blob);
  pso_desc.PS = CD3DX12_SHADER_BYTECODE(ps_blob);
  pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  pso_desc.DepthStencilState.DepthEnable = false;
  pso_desc.DepthStencilState.StencilEnable = false;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.SampleDesc.Count = 1;
  CE(g_device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state)));

  // vertex buffer
  Vertex verts[] = {
    { { -1.0f,  3.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
    { {  3.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
    { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
  };
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(verts))),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&vb_triangle)));
  UINT8* pData;
  CD3DX12_RANGE readRange(0, 0);
  CE(vb_triangle->Map(0, &readRange, (void**)&pData));
  memcpy(pData, verts, sizeof(verts));
  vb_triangle->Unmap(0, nullptr);

  // VBV
  vbv_triangle.BufferLocation = vb_triangle->GetGPUVirtualAddress();
  vbv_triangle.StrideInBytes = sizeof(Vertex);
  vbv_triangle.SizeInBytes = sizeof(verts);

  // The counter
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&px_counter)));

  // SRV and UAV and CBV heap
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.NumDescriptors = 3;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CE(g_device12->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_uav_heap)));

  // UAV
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Buffer.CounterOffsetInBytes = 0;
  uav_desc.Buffer.FirstElement = 0;
  uav_desc.Buffer.NumElements = 1;
  uav_desc.Buffer.StructureByteStride = 0;
  uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  CD3DX12_CPU_DESCRIPTOR_HANDLE uav_handle(srv_uav_heap->GetCPUDescriptorHandleForHeapStart());
  srv_uav_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  uav_handle.Offset(srv_uav_descriptor_size);
  g_device12->CreateUnorderedAccessView(px_counter, nullptr, &uav_desc, uav_handle);

  // CB for per-scene data
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(256)),
    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
    IID_PPV_ARGS(&cb_scene)));
  
  // and its CBV
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
  cbv_desc.BufferLocation = cb_scene->GetGPUVirtualAddress();
  cbv_desc.SizeInBytes = 256;
  uav_handle.Offset(srv_uav_descriptor_size);
  g_device12->CreateConstantBufferView(&cbv_desc, uav_handle);
}

void TriangleScene::CreateAS() {
  printf("Creating AS for a single triangle\n");
  // BLAS
  D3D12_RAYTRACING_GEOMETRY_DESC geom_desc{};
  geom_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geom_desc.Triangles.VertexBuffer.StartAddress = vb_triangle->GetGPUVirtualAddress();
  geom_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
  geom_desc.Triangles.VertexCount = 3;
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
  g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
  printf("BLAS prebuild info:\n");
  printf("  Scratch: %d\n", int(info.ScratchDataSizeInBytes));
  printf("  Result : %d\n", int(info.ResultDataMaxSizeInBytes));

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
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &scratch_desc, D3D12_RESOURCE_STATE_COMMON,
    nullptr, IID_PPV_ARGS(&blas_scratch)));

  D3D12_RESOURCE_DESC result_desc = scratch_desc;
  result_desc.Width = info.ResultDataMaxSizeInBytes;
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &result_desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
    nullptr, IID_PPV_ARGS(&blas_result)));
  blas_result->SetName(L"BLAS result");

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc{};
  build_desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  build_desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  build_desc.Inputs.NumDescs = 1;
  build_desc.Inputs.pGeometryDescs = &geom_desc;
  build_desc.DestAccelerationStructureData = {
    blas_result->GetGPUVirtualAddress()
  };
  build_desc.ScratchAccelerationStructureData = {
    blas_scratch->GetGPUVirtualAddress()
  };
  build_desc.SourceAccelerationStructureData = 0;
  build_desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
  CE(command_list->Reset(command_allocator, pipeline_state));
  command_list->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(blas_result)));
  command_list->Close();
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
  printf("Built BLAS.\n");

  // 2. TLAS
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs = inputs;
  tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_inputs.pGeometryDescs = nullptr;
  tlas_inputs.NumDescs = 1;

  g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &info);
  printf("TLAS prebuild info:\n");
  printf("  Scratch: %d\n", int(info.ScratchDataSizeInBytes));
  printf("  Result : %d\n", int(info.ResultDataMaxSizeInBytes));
  int instance_desc_size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * 1;  // only 1 inst

  D3D12_RESOURCE_DESC tlas_scratch_desc = scratch_desc;
  tlas_scratch_desc.Width = info.ScratchDataSizeInBytes;
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &tlas_scratch_desc, D3D12_RESOURCE_STATE_COMMON,
    nullptr, IID_PPV_ARGS(&tlas_scratch)));

  D3D12_RESOURCE_DESC tlas_result_desc = scratch_desc;
  tlas_result_desc.Width = info.ResultDataMaxSizeInBytes;
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
    D3D12_HEAP_FLAG_NONE,
    &tlas_result_desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
    nullptr, IID_PPV_ARGS(&tlas_result)));
  tlas_result->SetName(L"TLAS result");

  D3D12_RESOURCE_DESC tlas_instance_desc = scratch_desc;
  tlas_instance_desc.Width = instance_desc_size;
  tlas_instance_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  CE(g_device12->CreateCommittedResource(
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
    tlas_result->GetGPUVirtualAddress()
  };
  tlas_build_desc.ScratchAccelerationStructureData = {
    tlas_scratch->GetGPUVirtualAddress()
  };
  tlas_build_desc.SourceAccelerationStructureData = 0;
  tlas_build_desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
  CE(command_list->Reset(command_allocator, pipeline_state));
  command_list->BuildRaytracingAccelerationStructure(&tlas_build_desc, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(tlas_result)));
  command_list->Close();
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
  printf("Built TLAS.\n");
  
  // SRV for AS
  CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(srv_uav_heap->GetCPUDescriptorHandleForHeapStart());
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  srv_desc.Format = DXGI_FORMAT_UNKNOWN;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.RaytracingAccelerationStructure.Location = tlas_result->GetGPUVirtualAddress();
  g_device12->CreateShaderResourceView(nullptr, &srv_desc, srv_handle);
}

void TriangleScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, pipeline_state));

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);
  float bg_color[] = { 1.0f, 1.0f, 0.8f, 1.0f };
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_uav_cpu(
    srv_uav_heap->GetCPUDescriptorHandleForHeapStart(),
    1, srv_uav_descriptor_size);
  CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav_gpu(
    srv_uav_heap->GetGPUDescriptorHandleForHeapStart(),
    1, srv_uav_descriptor_size);
  
  UINT uav_clearcolor[4] = { 1,2,3,4 };
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    px_counter,
    D3D12_RESOURCE_STATE_GENERIC_READ,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS)));
  command_list->ClearUnorderedAccessViewUint(handle_uav_gpu, handle_uav_cpu, px_counter, uav_clearcolor, 0, nullptr);

  command_list->SetGraphicsRootSignature(root_sig);
  command_list->SetDescriptorHeaps(1, &srv_uav_heap);
  command_list->SetGraphicsRootDescriptorTable(0, srv_uav_heap->GetGPUDescriptorHandleForHeapStart());
  command_list->SetGraphicsRootUnorderedAccessView(1, px_counter->GetGPUVirtualAddress());
  command_list->SetGraphicsRootConstantBufferView(2, cb_scene->GetGPUVirtualAddress());
  D3D12_VIEWPORT viewport{};
  viewport.Height = WIN_H;
  viewport.Width = WIN_W;
  viewport.MinDepth = -100.0f;
  viewport.MaxDepth = 100.0f;
  D3D12_RECT scissor{};
  scissor.right = WIN_W;
  scissor.bottom = WIN_H;

  command_list->OMSetRenderTargets(1, &handle_rtv, false, nullptr);
  command_list->RSSetViewports(1, &viewport);
  command_list->RSSetScissorRects(1, &scissor);
  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list->IASetVertexBuffers(0, 1, &vbv_triangle);
  command_list->DrawInstanced(3, 1, 0, 0);

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PRESENT)));
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    px_counter,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    D3D12_RESOURCE_STATE_GENERIC_READ)));

  CE(command_list->Close());
  g_command_queue->ExecuteCommandLists(1,
    (ID3D12CommandList* const*)&command_list);
  CE(g_swapchain->Present(1, 0));
  WaitForPreviousFrame();
}

void TriangleScene::Update(float secs) {
  TriSceneCB cb{};
  cb.WIN_W = WIN_W;
  cb.WIN_H = WIN_H;
  cb.use_counter = use_counter;

  char* mapped = nullptr;
  CD3DX12_RANGE readRange(0, 0);
  cb_scene->Map(0, &readRange, (void**)(&mapped));
  memcpy(mapped, &cb, sizeof(cb));
  cb_scene->Unmap(0, nullptr);
}