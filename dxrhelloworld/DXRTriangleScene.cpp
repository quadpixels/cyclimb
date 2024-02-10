#include <sstream>

#include <d3dx12.h>
#include <DirectXMath.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <dxcapi.h>

#include "scene.hpp"
#include <util.hpp>

extern int WIN_W, WIN_H;
extern ID3D12Device5* g_device12;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern int g_rtv_descriptor_size;
extern int g_frame_index;
extern ID3D12Resource* g_rendertargets[];
extern ID3D12CommandQueue* g_command_queue;
extern IDXGISwapChain3* g_swapchain;

void WaitForPreviousFrame();

TriangleScene::TriangleScene() : root_sig(nullptr) {
  InitDX12Stuff();
  CreateAS();
  CreateRaytracingPipeline();
}

void TriangleScene::InitDX12Stuff() {
  // Root signature (empty)
  CD3DX12_ROOT_SIGNATURE_DESC root_sig_desc;
  root_sig_desc.Init(0, nullptr, 0, nullptr,
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
  ID3DBlob* signature, * error;
  CE(D3D12SerializeRootSignature(
    &root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
  CE(g_device12->CreateRootSignature(
    0, signature->GetBufferPointer(), signature->GetBufferSize(),
    IID_PPV_ARGS(&root_sig)));

  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));

  // Shader (hello triangle)
  ID3DBlob* vs_blob, * ps_blob;
  UINT compile_flags = 0;
  CE(D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
    "VSMain", "vs_4_0", 0, 0, &vs_blob, &error));
  if (error) printf("Error creating VS: %s\n", (char*)(error->GetBufferPointer()));
  CE(D3DCompileFromFile(L"shaders/hellotriangle.hlsl", nullptr, nullptr,
    "PSMain", "ps_4_0", 0, 0, &ps_blob, &error));
  if (error) printf("Error creating PS: %s\n", (char*)(error->GetBufferPointer()));

  // Input layout
  D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
      {
        "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
      },
      {
        "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
      }
  };

  // PSO state
  // Describe and create the graphics pipeline state object (PSO).
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
  psoDesc.pRootSignature = root_sig;
  psoDesc.VS = CD3DX12_SHADER_BYTECODE(vs_blob);
  psoDesc.PS = CD3DX12_SHADER_BYTECODE(ps_blob);
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.DepthStencilState.DepthEnable = FALSE;
  psoDesc.DepthStencilState.StencilEnable = FALSE;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  psoDesc.SampleDesc.Count = 1;
  CE(g_device12->CreateGraphicsPipelineState(
    &psoDesc, IID_PPV_ARGS(&pipeline_state)));

  // Command list
  CE(g_device12->CreateCommandList(
    0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
    pipeline_state, IID_PPV_ARGS(&command_list)));

  // Vertex buffer and VBV
  {
    Vertex triangleVerts[] = {
      {{0.0f, 0.25f, 0.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
      {{0.25f, -0.25f, 0.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
      {{-0.25f, -0.25f, 0.0f}, {1.0f, 0.0f, 1.0f, 1.0f}}
    };
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(triangleVerts))),
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&vb_triangle)));
    UINT8* ptr;
    CD3DX12_RANGE read_range(0, 0);
    CE(vb_triangle->Map(0, &read_range, (void**)(&ptr)));
    memcpy(ptr, triangleVerts, sizeof(triangleVerts));
    vb_triangle->Unmap(0, nullptr);
    vbv_triangle.BufferLocation = vb_triangle->GetGPUVirtualAddress();
    vbv_triangle.StrideInBytes = sizeof(Vertex);
    vbv_triangle.SizeInBytes = sizeof(triangleVerts);
  }

  // Dummy global and local rootsigs
  {
    D3D12_ROOT_SIGNATURE_DESC dummy_desc{};
    dummy_desc.NumParameters = 0;
    dummy_desc.pParameters = nullptr;
    dummy_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    assert(SUCCEEDED(D3D12SerializeRootSignature(&dummy_desc, D3D_ROOT_SIGNATURE_VERSION_1,
      &signature, &error)));
    assert(SUCCEEDED(g_device12->CreateRootSignature(0,
      signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&dummy_global_rootsig))));

    dummy_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    assert(SUCCEEDED(D3D12SerializeRootSignature(&dummy_desc, D3D_ROOT_SIGNATURE_VERSION_1,
      &signature, &error)));
    assert(SUCCEEDED(g_device12->CreateRootSignature(0,
      signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&dummy_local_rootsig))));
  }
}

void TriangleScene::CreateAS() {
  // 1. BLAS
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
  command_list->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(blas_result)));

  command_list->Close();
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
  WaitForPreviousFrame();
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
  WaitForPreviousFrame();
  printf("Built TLAS.\n");
}

IDxcBlob* CompileShaderLibrary(LPCWSTR fileName) {
  static IDxcCompiler* pCompiler = nullptr;
  static IDxcLibrary* pLibrary = nullptr;
  static IDxcIncludeHandler* dxcIncludeHandler;

  HRESULT hr;

  // Initialize the DXC compiler and compiler helper
  if (!pCompiler)
  {
    CE(DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), reinterpret_cast<void**>(&pCompiler)));
    CE(DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), reinterpret_cast<void**>(&pLibrary)));
    CE(pLibrary->CreateIncludeHandler(&dxcIncludeHandler));
  }
  // Open and read the file
  std::ifstream shaderFile(fileName);
  if (shaderFile.good() == false)
  {
    throw std::logic_error("Cannot find shader file");
  }
  std::stringstream strStream;
  strStream << shaderFile.rdbuf();
  std::string sShader = strStream.str();

  // Create blob from the string
  IDxcBlobEncoding* pTextBlob;
  CE(pLibrary->CreateBlobWithEncodingFromPinned(
    LPBYTE(sShader.c_str()), static_cast<uint32_t>(sShader.size()), 0, &pTextBlob));

  // Compile
  IDxcOperationResult* pResult;
  CE(pCompiler->Compile(pTextBlob, fileName, L"", L"lib_6_3", nullptr, 0, nullptr, 0,
    dxcIncludeHandler, &pResult));

  // Verify the result
  HRESULT resultCode;
  CE(pResult->GetStatus(&resultCode));
  if (FAILED(resultCode))
  {
    IDxcBlobEncoding* pError;
    hr = pResult->GetErrorBuffer(&pError);
    if (FAILED(hr))
    {
      throw std::logic_error("Failed to get shader compiler error");
    }

    // Convert error blob to a string
    std::vector<char> infoLog(pError->GetBufferSize() + 1);
    memcpy(infoLog.data(), pError->GetBufferPointer(), pError->GetBufferSize());
    infoLog[pError->GetBufferSize()] = 0;

    std::string errorMsg = "Shader Compiler Error:\n";
    errorMsg.append(infoLog.data());

    MessageBoxA(nullptr, errorMsg.c_str(), "Error!", MB_OK);
    throw std::logic_error("Failed compile shader");
  }

  IDxcBlob* pBlob;
  CE(pResult->GetResult(&pBlob));
  return pBlob;
}

void TriangleScene::CreateRaytracingPipeline() {
  raygen_library = CompileShaderLibrary(L"shaders/RayGen.hlsl");
  miss_library = CompileShaderLibrary(L"shaders/Miss.hlsl");
  hit_library = CompileShaderLibrary(L"shaders/Hit.hlsl");

  raygen_export = {};
  raygen_export.Name = L"RayGen";
  raygen_export.ExportToRename = nullptr;
  raygen_export.Flags = D3D12_EXPORT_FLAG_NONE;
  raygen_lib_desc = {};
  raygen_lib_desc.DXILLibrary.BytecodeLength = raygen_library->GetBufferSize();
  raygen_lib_desc.DXILLibrary.pShaderBytecode = raygen_library->GetBufferPointer();
  raygen_lib_desc.NumExports = 1;
  raygen_lib_desc.pExports = &raygen_export;

  miss_export = {};
  miss_export.Name = L"Miss";
  miss_export.ExportToRename = nullptr;
  miss_export.Flags = D3D12_EXPORT_FLAG_NONE;
  miss_lib_desc = {};
  miss_lib_desc.DXILLibrary.BytecodeLength = miss_library->GetBufferSize();
  miss_lib_desc.DXILLibrary.pShaderBytecode = miss_library->GetBufferPointer();
  miss_lib_desc.NumExports = 1;
  miss_lib_desc.pExports = &miss_export;

  hit_export = {};
  hit_export.Name = L"ClosestHit";
  hit_export.ExportToRename = nullptr;
  hit_export.Flags = D3D12_EXPORT_FLAG_NONE;
  hit_lib_desc = {};
  hit_lib_desc.DXILLibrary.BytecodeLength = hit_library->GetBufferSize();
  hit_lib_desc.DXILLibrary.pShaderBytecode = hit_library->GetBufferPointer();
  hit_lib_desc.NumExports = 1;
  hit_lib_desc.pExports = &hit_export;

  // Local root signatures
  // 1. Raygen
  D3D12_ROOT_PARAMETER root_params_raygen[1];
  D3D12_DESCRIPTOR_RANGE ranges_raygen[2];
  ranges_raygen[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges_raygen[0].NumDescriptors = 1;
  ranges_raygen[0].BaseShaderRegister = 0;  // 对应 "register(b0)"
  ranges_raygen[0].RegisterSpace = 0;
  ranges_raygen[0].OffsetInDescriptorsFromTableStart = 0;
  ranges_raygen[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges_raygen[1].NumDescriptors = 1;
  ranges_raygen[1].BaseShaderRegister = 0;  // 对应 "register(b0)"
  ranges_raygen[1].RegisterSpace = 0;
  ranges_raygen[1].OffsetInDescriptorsFromTableStart = 1;
  root_params_raygen[0] = {};
  root_params_raygen[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_params_raygen[0].DescriptorTable.NumDescriptorRanges = 2;
  root_params_raygen[0].DescriptorTable.pDescriptorRanges = ranges_raygen;
  root_params_raygen[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rootsig_raygen_desc{};
  rootsig_raygen_desc.NumParameters = 1;
  rootsig_raygen_desc.pParameters = root_params_raygen;
  rootsig_raygen_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
  rootsig_raygen_desc.NumStaticSamplers = 0;

  ID3DBlob* sigblob_raygen;
  ID3DBlob* error;
  HRESULT hr;
  hr = D3D12SerializeRootSignature(
    &rootsig_raygen_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigblob_raygen, &error);
  assert(SUCCEEDED(hr));
  if (error) {
    printf("Error creating Raygen local root signature: %s\n", (char*)error->GetBufferPointer());
  }
  CE(g_device12->CreateRootSignature(0, sigblob_raygen->GetBufferPointer(), sigblob_raygen->GetBufferSize(), IID_PPV_ARGS(&raygen_rootsig)));
  sigblob_raygen->Release();

  // 2. Miss
  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootsig_miss_desc;
  rootsig_miss_desc.Init_1_1(0, NULL, 0, NULL,
    D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
  ID3DBlob * sigblob_miss;
  hr = D3DX12SerializeVersionedRootSignature(
    &rootsig_miss_desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &sigblob_miss, &error);
  assert(SUCCEEDED(hr));
  if (error) {
    printf("Error creating Miss local root signature: %s\n", (char*)error->GetBufferPointer());
  }
  CE(g_device12->CreateRootSignature(0, sigblob_miss->GetBufferPointer(), sigblob_miss->GetBufferSize(), IID_PPV_ARGS(&miss_rootsig)));

  // 3. Closest Hit
  CD3DX12_ROOT_PARAMETER1 root_params_hit[1];
  root_params_hit[0].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);
  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootsig_hit_desc;
  rootsig_hit_desc.Init_1_1(1, root_params_hit, 0, NULL,
    D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
  ID3DBlob * sigblob_hit;
  hr = D3DX12SerializeVersionedRootSignature(
    &rootsig_hit_desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &sigblob_hit, &error);
  if (error) {
    printf("Error creating Hit local root signature: %s\n", (char*)error->GetBufferPointer());
  }
  CE(g_device12->CreateRootSignature(0, sigblob_hit->GetBufferPointer(), sigblob_hit->GetBufferSize(), IID_PPV_ARGS(&hit_rootsig)));

  printf("Created local root signatures.\n");

  // Hit group
  hitgroup_desc = {};
  hitgroup_desc.HitGroupExport = L"HitGroup";
  hitgroup_desc.ClosestHitShaderImport = L"ClosestHit";
  hitgroup_desc.AnyHitShaderImport = nullptr;
  hitgroup_desc.IntersectionShaderImport = nullptr;

  // Associate root signatures to symbols
  // raygen_rootsig -> RayGen
  // miss_rootsig   -> Miss
  // hit_rootsig    -> ClosestHit

  int max_payload_size = 4 * sizeof(float);  // RGB + distance
  int max_attrib_size = 2 * sizeof(float);
  int max_recursion_depth = 1;

  // Fill up subobjects
  int subobject_count =
    3 +  // 3 DXIL libraries
    1 +  // 1 Hit Group
    1 +  // Shader configuration
    1 +  // Shader payload association
    2 * 3 +  // Root sig and association
    2 +  // Empty global and local rootsig
    1;   // Final pipeline subobject
  std::vector<D3D12_STATE_SUBOBJECT> subobjects(subobject_count);
  int curr_idx = 0;

  // [0]
  D3D12_STATE_SUBOBJECT subobj_raygen_lib = {};
  subobj_raygen_lib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  subobj_raygen_lib.pDesc = &raygen_lib_desc;
  subobjects[curr_idx++] = subobj_raygen_lib;

  // [1]
  D3D12_STATE_SUBOBJECT subobj_miss_lib = {};
  subobj_miss_lib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  subobj_miss_lib.pDesc = &miss_lib_desc;
  subobjects[curr_idx++] = subobj_miss_lib;

  // [2]
  D3D12_STATE_SUBOBJECT subobj_hit_lib = {};
  subobj_hit_lib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  subobj_hit_lib.pDesc = &hit_lib_desc;
  subobjects[curr_idx++] = subobj_hit_lib;

  // [3]
  D3D12_STATE_SUBOBJECT subobj_hitgroup = {};
  subobj_hitgroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
  subobj_hitgroup.pDesc = &hitgroup_desc;
  subobjects[curr_idx++] = subobj_hitgroup;

  // [4]
  D3D12_RAYTRACING_SHADER_CONFIG shaderconfig = {};
  shaderconfig.MaxPayloadSizeInBytes = max_payload_size;
  shaderconfig.MaxAttributeSizeInBytes = max_attrib_size;
  D3D12_STATE_SUBOBJECT subobj_shaderconfig = {};
  subobj_shaderconfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  subobj_shaderconfig.pDesc = &shaderconfig;
  subobjects[curr_idx++] = subobj_shaderconfig;

  // [5]
  std::vector<std::wstring> exported_symbols = { L"RayGen", L"Miss", L"HitGroup" };
  std::vector<LPCWSTR> exported_symbol_ptrs;
  for (int i = 0; i < 3; i++) exported_symbol_ptrs.push_back(exported_symbols[i].c_str());
  D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shader_payload_assoc;
  shader_payload_assoc.NumExports = 3;
  shader_payload_assoc.pExports = exported_symbol_ptrs.data();
  shader_payload_assoc.pSubobjectToAssociate = &(subobjects[curr_idx - 1]);
  D3D12_STATE_SUBOBJECT subobj_assoc = {};
  subobj_assoc.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
  subobj_assoc.pDesc = &shader_payload_assoc;
  subobjects[curr_idx++] = subobj_assoc;

  // Local root signatures and associations
  // [6]
  D3D12_STATE_SUBOBJECT subobj_raygen_rootsig{};
  subobj_raygen_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
  subobj_raygen_rootsig.pDesc = &raygen_rootsig;
  subobjects[curr_idx++] = subobj_raygen_rootsig;

  // [7]
  D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assoc_raygen{};
  assoc_raygen.NumExports = 1;
  assoc_raygen.pExports = &(exported_symbol_ptrs[0]);
  assoc_raygen.pSubobjectToAssociate = &(subobjects[curr_idx - 1]);
  D3D12_STATE_SUBOBJECT subobj_raygen_assoc{};
  subobj_raygen_assoc.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
  subobj_raygen_assoc.pDesc = &assoc_raygen;
  subobjects[curr_idx++] = subobj_raygen_assoc;

  // [8]
  D3D12_STATE_SUBOBJECT subobj_miss_rootsig{};
  subobj_miss_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
  subobj_miss_rootsig.pDesc = &miss_rootsig;
  subobjects[curr_idx++] = subobj_miss_rootsig;

  // [9]
  D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assoc_miss{};
  assoc_miss.NumExports = 1;
  assoc_miss.pExports = &(exported_symbol_ptrs[1]);
  assoc_miss.pSubobjectToAssociate = &(subobjects[curr_idx - 1]);
  D3D12_STATE_SUBOBJECT subobj_miss_assoc{};
  subobj_miss_assoc.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
  subobj_miss_assoc.pDesc = &assoc_miss;
  subobjects[curr_idx++] = subobj_miss_assoc;

  // [10]
  D3D12_STATE_SUBOBJECT subobj_hit_rootsig{};
  subobj_hit_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
  subobj_hit_rootsig.pDesc = &hit_rootsig;
  subobjects[curr_idx++] = subobj_hit_rootsig;

  // [11]
  D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assoc_hit{};
  assoc_hit.NumExports = 1;
  assoc_hit.pExports = &(exported_symbol_ptrs[2]);
  assoc_hit.pSubobjectToAssociate = &(subobjects[curr_idx - 1]);
  D3D12_STATE_SUBOBJECT subobj_hit_assoc{};
  subobj_hit_assoc.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
  subobj_hit_assoc.pDesc = &assoc_hit;
  subobjects[curr_idx++] = subobj_hit_assoc;

  // [12]
  D3D12_STATE_SUBOBJECT subobj_dummy_global_rootsig{};
  subobj_dummy_global_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  subobj_dummy_global_rootsig.pDesc = &dummy_global_rootsig;
  subobjects[curr_idx++] = subobj_dummy_global_rootsig;

  // [13]
  D3D12_STATE_SUBOBJECT subobj_dummy_local_rootsig{};
  subobj_dummy_local_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
  subobj_dummy_local_rootsig.pDesc = &dummy_local_rootsig;
  subobjects[curr_idx++] = subobj_dummy_local_rootsig;

  // [14]
  D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
  pipeline_config.MaxTraceRecursionDepth = 1;
  D3D12_STATE_SUBOBJECT subobj_pipeline_config{};
  subobj_pipeline_config.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
  subobj_pipeline_config.pDesc = &pipeline_config;
  subobjects[curr_idx++] = subobj_pipeline_config;

  D3D12_STATE_OBJECT_DESC pipeline_desc{};
  pipeline_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  pipeline_desc.NumSubobjects = curr_idx;
  pipeline_desc.pSubobjects = subobjects.data();
  assert(SUCCEEDED(g_device12->CreateStateObject(&pipeline_desc, IID_PPV_ARGS(&rt_state_object))));
  printf("Created RT pipeline state object.\n");
}

void TriangleScene::Render() {
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, pipeline_state));
  command_list->SetGraphicsRootSignature(root_sig);

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);
  float bg_color[] = { 1.0f, 1.0f, 0.8f, 1.0f };
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
  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list->IASetVertexBuffers(0, 1, &vbv_triangle);
  command_list->DrawInstanced(3, 1, 0, 0);

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

void TriangleScene::Update(float secs) {

}