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
extern int RoundUp(int x, int align);

//#define TEMP_FLAG

// Declared in DxrObjScene.cpp
IDxcBlob* CompileShaderLibrary(LPCWSTR fileName);

void PrintShaderIdentifier(const char* tag, void* id) {
  uint8_t* ptr = reinterpret_cast<uint8_t*>(id);
  printf("Shader ID of %s:", tag);
  for (int i = 0; i < D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; i++) {
    printf(" %02X", int(ptr[i]));
  }
  printf("\n");
}

ObjScene1::ObjScene1() {
  InitDX12Stuff();
  CreateRTPipeline();
  CreateShaderBindingTable();
  CreateAS();
}

void ObjScene1::InitDX12Stuff() {
  printf("[ObjScene1::InitDX12Stuff]\n");
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(
    0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
    nullptr, IID_PPV_ARGS(&command_list)));
  command_list->Close();

  // RT Output Resource
  {
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
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
      IID_PPV_ARGS(&rt_output_resource)));
    rt_output_resource->SetName(L"RT Output Resource");
  }

  // SRV/UAV Heap
  {
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = 1;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CE(g_device12->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_uav_heap)));
    srv_uav_heap->SetName(L"SRV UAV Heap");
  }

  // UAV
  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE uav_handle(srv_uav_heap->GetCPUDescriptorHandleForHeapStart());
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    g_device12->CreateUnorderedAccessView(rt_output_resource, nullptr, &uav_desc, uav_handle);
  }

  // Root Signature, has only one CBV
  {
    D3D12_ROOT_PARAMETER root_params[2];
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[0].DescriptorTable.NumDescriptorRanges = 1;
    D3D12_DESCRIPTOR_RANGE desc_range{};
    desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    desc_range.NumDescriptors = 1;
    desc_range.BaseShaderRegister = 0;
    desc_range.RegisterSpace = 0;
    desc_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_params[0].DescriptorTable.pDescriptorRanges = &desc_range;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_params[1].Descriptor.RegisterSpace = 0;
    root_params[1].Descriptor.ShaderRegister = 0;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC root_sig_desc{};
    root_sig_desc.NumStaticSamplers = 0;
    root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    root_sig_desc.NumParameters = 2;
    root_sig_desc.pParameters = root_params;

    ID3DBlob* signature, * error;
    CE(D3D12SerializeRootSignature(
      &root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    CE(g_device12->CreateRootSignature(
      0, signature->GetBufferPointer(), signature->GetBufferSize(),
      IID_PPV_ARGS(&root_sig)));
    root_sig->SetName(L"Root signature DxrObjScene1");
  }
}

void ObjScene1::CreateRTPipeline() {
#ifndef TEMP_FLAG
  // 1. Root signatures (global and local)
  {
    D3D12_ROOT_PARAMETER root_params[2];

    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[0].DescriptorTable.NumDescriptorRanges = 1;
    D3D12_DESCRIPTOR_RANGE desc_range{};
    desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    desc_range.NumDescriptors = 1;
    desc_range.BaseShaderRegister = 0;
    desc_range.RegisterSpace = 0;
    desc_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_params[0].DescriptorTable.pDescriptorRanges = &desc_range;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_params[1].Descriptor.RegisterSpace = 0;
    root_params[1].Descriptor.ShaderRegister = 0;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
    rootsig_desc.NumStaticSamplers = 0;
    rootsig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rootsig_desc.NumParameters = 2;
    rootsig_desc.pParameters = root_params;

    ID3DBlob* signature, * error;
    CE(D3D12SerializeRootSignature(
      &rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    CE(g_device12->CreateRootSignature(
      0, signature->GetBufferPointer(), signature->GetBufferSize(),
      IID_PPV_ARGS(&global_rootsig)));
    signature->Release();
    if (error)
      error->Release();
  }

  {
    D3D12_ROOT_PARAMETER root_param;
    root_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_param.Constants.Num32BitValues = 8;
    root_param.Constants.ShaderRegister = 0;
    root_param.Constants.RegisterSpace = 0;
    root_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
    rootsig_desc.pParameters = &root_param;
    rootsig_desc.NumParameters = 1;
    rootsig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    ID3DBlob* signature, * error;
    CE(D3D12SerializeRootSignature(
      &rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    CE(g_device12->CreateRootSignature(
      0, signature->GetBufferPointer(), signature->GetBufferSize(),
      IID_PPV_ARGS(&local_rootsig)));
    signature->Release();
    if (error)
      error->Release();
  }

  // 2. RTPSO
  {
    // 7 subobjects:
    //
    // 1 - DXIL library
    // 3 - My 3 hit groups.
    // 1 - Shader config
    // 2 - Local root signature and association
    // 1 - Global root signature
    // 1 - Pipeline config

    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(6);

    // 1. DXIL library
    D3D12_DXIL_LIBRARY_DESC dxil_lib_desc = {};
    IDxcBlob* dxil_library = CompileShaderLibrary(L"shaders/minimal.hlsl");
    D3D12_EXPORT_DESC dxil_lib_exports[1];
    dxil_lib_exports[0].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[0].ExportToRename = nullptr;
    dxil_lib_exports[0].Name = L"RayGen";
    /*dxil_lib_exports[1].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[1].ExportToRename = nullptr;
    dxil_lib_exports[1].Name = L"MyClosestHitShader";
    dxil_lib_exports[2].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[2].ExportToRename = nullptr;
    dxil_lib_exports[2].Name = L"MyMissShader";
    dxil_lib_exports[3].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[3].ExportToRename = nullptr;
    dxil_lib_exports[3].Name = L"MyIntersectionShader";
    dxil_lib_exports[4].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[4].ExportToRename = nullptr;
    dxil_lib_exports[4].Name = L"MyAnyHitShader";*/
    dxil_lib_desc.DXILLibrary.pShaderBytecode = dxil_library->GetBufferPointer();
    dxil_lib_desc.DXILLibrary.BytecodeLength = dxil_library->GetBufferSize();
    dxil_lib_desc.NumExports = 1;
    dxil_lib_desc.pExports = dxil_lib_exports;

    D3D12_STATE_SUBOBJECT subobj_dxil_lib = {};
    subobj_dxil_lib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobj_dxil_lib.pDesc = &dxil_lib_desc;
    subobjects.push_back(subobj_dxil_lib);

    // 2. Hit group
    /*D3D12_STATE_SUBOBJECT subobj_hitgroup = {};
    subobj_hitgroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    D3D12_HIT_GROUP_DESC hitgroup_desc = {};
    hitgroup_desc.ClosestHitShaderImport = L"MyClosestHitShader";
    hitgroup_desc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitgroup_desc.HitGroupExport = L"MyHitGroup";
    subobj_hitgroup.pDesc = &hitgroup_desc;
    subobjects.push_back(subobj_hitgroup);*/

    // 2.1. Hit group for procedural AABBs
    /*D3D12_STATE_SUBOBJECT subobj_hitgroup1 = {};
    subobj_hitgroup1.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    D3D12_HIT_GROUP_DESC hitgroup_desc1 = {};
    hitgroup_desc1.IntersectionShaderImport = L"MyIntersectionShader";
    hitgroup_desc1.ClosestHitShaderImport = L"MyClosestHitShader";
    hitgroup_desc1.Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
    hitgroup_desc1.HitGroupExport = L"MyHitGroup1";
    subobj_hitgroup1.pDesc = &hitgroup_desc1;
    subobjects.push_back(subobj_hitgroup1);*/

    // 2.2 Hit group for anyhit triangles
    /*D3D12_STATE_SUBOBJECT subobj_hitgroup2 = {};
    subobj_hitgroup2.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    D3D12_HIT_GROUP_DESC hitgroup_desc2 = {};
    hitgroup_desc2.AnyHitShaderImport = L"MyAnyHitShader";
    hitgroup_desc2.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitgroup_desc2.HitGroupExport = L"MyHitGroup2";
    subobj_hitgroup2.pDesc = &hitgroup_desc2;
    subobjects.push_back(subobj_hitgroup2);*/

    // 3. Shader config
    D3D12_STATE_SUBOBJECT subobj_shaderconfig = {};
    subobj_shaderconfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    D3D12_RAYTRACING_SHADER_CONFIG shader_config = {};
    shader_config.MaxPayloadSizeInBytes = 16;  // float4 color
    shader_config.MaxAttributeSizeInBytes = 8;  // float2 bary
    subobj_shaderconfig.pDesc = &shader_config;
    subobjects.push_back(subobj_shaderconfig);

    // 4. Local root signature
    D3D12_STATE_SUBOBJECT subobj_local_rootsig = {};
    subobj_local_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
    subobj_local_rootsig.pDesc = &local_rootsig;
    subobjects.push_back(subobj_local_rootsig);

    // 5. Subobject association
    D3D12_STATE_SUBOBJECT subobj_local_rootsig_assoc = {};
    subobj_local_rootsig_assoc.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION local_rootsig_assoc{};
    local_rootsig_assoc.NumExports = 1;
    local_rootsig_assoc.pSubobjectToAssociate = &(subobjects.back());
    local_rootsig_assoc.pExports = &(dxil_lib_exports[0].Name);  // L"MyRaygenShader"
    subobj_local_rootsig_assoc.pDesc = &local_rootsig_assoc;
    subobjects.push_back(subobj_local_rootsig_assoc);

    // 6. Global root signature
    D3D12_STATE_SUBOBJECT subobj_global_rootsig = {};
    subobj_global_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobj_global_rootsig.pDesc = &global_rootsig;
    subobjects.push_back(subobj_global_rootsig);

    // 7. Pipeline config
    D3D12_STATE_SUBOBJECT subobj_pipeline_config = {};
    subobj_pipeline_config.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
    pipeline_config.MaxTraceRecursionDepth = 1;
    subobj_pipeline_config.pDesc = &pipeline_config;
    subobjects.push_back(subobj_pipeline_config);

    D3D12_STATE_OBJECT_DESC rtpso_desc{};
    rtpso_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    rtpso_desc.NumSubobjects = int(subobjects.size());
    rtpso_desc.pSubobjects = subobjects.data();
    CE(g_device12->CreateStateObject(&rtpso_desc, IID_PPV_ARGS(&rt_state_object)));

    rt_state_object->QueryInterface(IID_PPV_ARGS(&rt_state_object_props));
  }
#else
  // 1. Root signatures (global and local)
  {
    D3D12_ROOT_PARAMETER root_params[2];

    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[0].DescriptorTable.NumDescriptorRanges = 1;
    D3D12_DESCRIPTOR_RANGE desc_range{};
    desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    desc_range.NumDescriptors = 1;
    desc_range.BaseShaderRegister = 0;
    desc_range.RegisterSpace = 0;
    desc_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_params[0].DescriptorTable.pDescriptorRanges = &desc_range;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_params[1].Descriptor.RegisterSpace = 0;
    root_params[1].Descriptor.ShaderRegister = 0;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
    rootsig_desc.NumStaticSamplers = 0;
    rootsig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rootsig_desc.NumParameters = 2;
    rootsig_desc.pParameters = root_params;

    ID3DBlob* signature, * error;
    CE(D3D12SerializeRootSignature(
      &rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    CE(g_device12->CreateRootSignature(
      0, signature->GetBufferPointer(), signature->GetBufferSize(),
      IID_PPV_ARGS(&global_rootsig)));
    signature->Release();
    if (error)
      error->Release();
  }

  {
    D3D12_ROOT_PARAMETER root_param;
    root_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_param.Constants.Num32BitValues = 8;
    root_param.Constants.ShaderRegister = 0;
    root_param.Constants.RegisterSpace = 0;
    root_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
    rootsig_desc.pParameters = &root_param;
    rootsig_desc.NumParameters = 1;
    rootsig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    ID3DBlob* signature, * error;
    CE(D3D12SerializeRootSignature(
      &rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    CE(g_device12->CreateRootSignature(
      0, signature->GetBufferPointer(), signature->GetBufferSize(),
      IID_PPV_ARGS(&local_rootsig)));
    signature->Release();
    if (error)
      error->Release();
  }

  // 2. RTPSO
  {
    // 7 subobjects:
    //
    // 1 - DXIL library
    // 3 - My 3 hit groups.
    // 1 - Shader config
    // 2 - Local root signature and association
    // 1 - Global root signature
    // 1 - Pipeline config

    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(9);

    // 1. DXIL library
    D3D12_DXIL_LIBRARY_DESC dxil_lib_desc = {};
    IDxcBlob* dxil_library = CompileShaderLibrary(L"shaders/raytracing_tutorial.hlsl");
    D3D12_EXPORT_DESC dxil_lib_exports[5];
    dxil_lib_exports[0].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[0].ExportToRename = nullptr;
    dxil_lib_exports[0].Name = L"MyRaygenShader";
    dxil_lib_exports[1].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[1].ExportToRename = nullptr;
    dxil_lib_exports[1].Name = L"MyClosestHitShader";
    dxil_lib_exports[2].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[2].ExportToRename = nullptr;
    dxil_lib_exports[2].Name = L"MyMissShader";
    dxil_lib_exports[3].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[3].ExportToRename = nullptr;
    dxil_lib_exports[3].Name = L"MyIntersectionShader";
    dxil_lib_exports[4].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[4].ExportToRename = nullptr;
    dxil_lib_exports[4].Name = L"MyAnyHitShader";
    dxil_lib_desc.DXILLibrary.pShaderBytecode = dxil_library->GetBufferPointer();
    dxil_lib_desc.DXILLibrary.BytecodeLength = dxil_library->GetBufferSize();
    dxil_lib_desc.NumExports = 5;
    dxil_lib_desc.pExports = dxil_lib_exports;

    D3D12_STATE_SUBOBJECT subobj_dxil_lib = {};
    subobj_dxil_lib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobj_dxil_lib.pDesc = &dxil_lib_desc;
    subobjects.push_back(subobj_dxil_lib);

    // 2. Hit group
    D3D12_STATE_SUBOBJECT subobj_hitgroup = {};
    subobj_hitgroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    D3D12_HIT_GROUP_DESC hitgroup_desc = {};
    hitgroup_desc.ClosestHitShaderImport = L"MyClosestHitShader";
    hitgroup_desc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitgroup_desc.HitGroupExport = L"MyHitGroup";
    subobj_hitgroup.pDesc = &hitgroup_desc;
    subobjects.push_back(subobj_hitgroup);

    // 2.1. Hit group for procedural AABBs
    D3D12_STATE_SUBOBJECT subobj_hitgroup1 = {};
    subobj_hitgroup1.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    D3D12_HIT_GROUP_DESC hitgroup_desc1 = {};
    hitgroup_desc1.IntersectionShaderImport = L"MyIntersectionShader";
    hitgroup_desc1.ClosestHitShaderImport = L"MyClosestHitShader";
    hitgroup_desc1.Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
    hitgroup_desc1.HitGroupExport = L"MyHitGroup1";
    subobj_hitgroup1.pDesc = &hitgroup_desc1;
    subobjects.push_back(subobj_hitgroup1);

    // 2.2 Hit group for anyhit triangles
    D3D12_STATE_SUBOBJECT subobj_hitgroup2 = {};
    subobj_hitgroup2.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    D3D12_HIT_GROUP_DESC hitgroup_desc2 = {};
    hitgroup_desc2.AnyHitShaderImport = L"MyAnyHitShader";
    hitgroup_desc2.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitgroup_desc2.HitGroupExport = L"MyHitGroup2";
    subobj_hitgroup2.pDesc = &hitgroup_desc2;
    subobjects.push_back(subobj_hitgroup2);

    // 3. Shader config
    D3D12_STATE_SUBOBJECT subobj_shaderconfig = {};
    subobj_shaderconfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    D3D12_RAYTRACING_SHADER_CONFIG shader_config = {};
    shader_config.MaxPayloadSizeInBytes = 16;  // float4 color
    shader_config.MaxAttributeSizeInBytes = 8;  // float2 bary
    subobj_shaderconfig.pDesc = &shader_config;
    subobjects.push_back(subobj_shaderconfig);

    // 4. Local root signature
    D3D12_STATE_SUBOBJECT subobj_local_rootsig = {};
    subobj_local_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
    subobj_local_rootsig.pDesc = &local_rootsig;
    subobjects.push_back(subobj_local_rootsig);

    // 5. Subobject association
    D3D12_STATE_SUBOBJECT subobj_local_rootsig_assoc = {};
    subobj_local_rootsig_assoc.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION local_rootsig_assoc{};
    local_rootsig_assoc.NumExports = 1;
    local_rootsig_assoc.pSubobjectToAssociate = &(subobjects.back());
    local_rootsig_assoc.pExports = &(dxil_lib_exports[0].Name);  // L"MyRaygenShader"
    subobj_local_rootsig_assoc.pDesc = &local_rootsig_assoc;
    subobjects.push_back(subobj_local_rootsig_assoc);

    // 6. Global root signature
    D3D12_STATE_SUBOBJECT subobj_global_rootsig = {};
    subobj_global_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobj_global_rootsig.pDesc = &global_rootsig;
    subobjects.push_back(subobj_global_rootsig);

    // 7. Pipeline config
    D3D12_STATE_SUBOBJECT subobj_pipeline_config = {};
    subobj_pipeline_config.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
    pipeline_config.MaxTraceRecursionDepth = 1;
    subobj_pipeline_config.pDesc = &pipeline_config;
    subobjects.push_back(subobj_pipeline_config);

    D3D12_STATE_OBJECT_DESC rtpso_desc{};
    rtpso_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    rtpso_desc.NumSubobjects = int(subobjects.size());
    rtpso_desc.pSubobjects = subobjects.data();
    CE(g_device12->CreateStateObject(&rtpso_desc, IID_PPV_ARGS(&rt_state_object)));

    rt_state_object->QueryInterface(IID_PPV_ARGS(&rt_state_object_props));
  }
#endif
}

void ObjScene1::CreateAS() {
  // Triangle
  Vertex triangle_verts[] = {
    { {0.0f, 0.25f, 0.0f} },
    { {0.25f, -0.25f, 0.0f} },
    { {-0.25f, -0.25f, 0.0f} }
  };
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(triangle_verts))),
    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
    IID_PPV_ARGS(&vb_triangle)));
  UINT8* ptr;
  CD3DX12_RANGE read_range(0, 0);
  CE(vb_triangle->Map(0, &read_range, (void**)(&ptr)));
  memcpy(ptr, triangle_verts, sizeof(triangle_verts));
  vb_triangle->Unmap(0, nullptr);

  // BLAS's Geom
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
  printf("[ObjScene1] BLAS prebuild info:\n");
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
    &scratch_desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
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
  command_list->Reset(command_allocator, nullptr);
  command_list->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(blas_result)));
  command_list->Close();
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
  printf("[ObjScene1] Built BLAS.\n");

  // TLAS
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
  tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_inputs.pGeometryDescs = nullptr;
  tlas_inputs.NumDescs = 1;

  g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &info);
  printf("[ObjScene1] TLAS prebuild info:\n");
  printf("  Scratch: %d\n", int(info.ScratchDataSizeInBytes));
  printf("  Result : %d\n", int(info.ResultDataMaxSizeInBytes));
  int instance_desc_size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * 1;

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

  // Instance
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
  CE(command_list->Reset(command_allocator, nullptr));
  command_list->BuildRaytracingAccelerationStructure(&tlas_build_desc, 0, nullptr);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(tlas_result)));
  command_list->Close();
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
  printf("[ObjScene1] Built TLAS.\n");
}

void ObjScene1::CreateShaderBindingTable() {
#ifndef TEMP_FLAG
  {
    void* raygen_shader_id = rt_state_object_props->GetShaderIdentifier(L"RayGen");
    struct RootArguments {
      RayGenConstantBuffer cb;
    } root_args;
    root_args.cb.viewport.left = -1;
    root_args.cb.viewport.top = -1;
    root_args.cb.viewport.right = 1;
    root_args.cb.viewport.bottom = 1;
    float border = 0.1f;
    float ar = WIN_W * 1.0f / WIN_H;
    if (WIN_W < WIN_H) {
      root_args.cb.stencil = {
        -1 + border, -1 + border * ar,
        1 - border, 1 - border * ar
      };
    }
    else {
      root_args.cb.stencil = {
        -1 + border / ar, -1 + border,
        1 - border / ar, 1 - border
      };
    }
    int shader_record_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(root_args);
    shader_record_size = RoundUp(shader_record_size, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    D3D12_RESOURCE_DESC sbt_desc{};
    sbt_desc.DepthOrArraySize = 1;
    sbt_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    sbt_desc.Format = DXGI_FORMAT_UNKNOWN;
    sbt_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    sbt_desc.Width = shader_record_size;
    sbt_desc.Height = 1;
    sbt_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    sbt_desc.SampleDesc.Count = 1;
    sbt_desc.SampleDesc.Quality = 0;
    sbt_desc.MipLevels = 1;
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE, &sbt_desc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&raygen_sbt_storage)));
    char* mapped;
    raygen_sbt_storage->Map(0, nullptr, (void**)&mapped);
    memcpy(mapped, raygen_shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    mapped += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    memcpy(mapped, &root_args, sizeof(root_args));
    raygen_sbt_storage->Unmap(0, nullptr);
    printf("Raygen's SBT size is %d\n", D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(root_args));
  }
#else
  {
    void* raygen_shader_id = rt_state_object_props->GetShaderIdentifier(L"MyRaygenShader");
    struct RootArguments {
      RayGenConstantBuffer cb;
    } root_args;
    root_args.cb.viewport.left = -1;
    root_args.cb.viewport.top = -1;
    root_args.cb.viewport.right = 1;
    root_args.cb.viewport.bottom = 1;
    float border = 0.1f;
    float ar = WIN_W * 1.0f / WIN_H;
    if (WIN_W < WIN_H) {
      root_args.cb.stencil = {
        -1 + border, -1 + border * ar,
        1 - border, 1 - border * ar
      };
    }
    else {
      root_args.cb.stencil = {
        -1 + border / ar, -1 + border,
        1 - border / ar, 1 - border
      };
    }
    int shader_record_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(root_args);
    shader_record_size = RoundUp(shader_record_size, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    D3D12_RESOURCE_DESC sbt_desc{};
    sbt_desc.DepthOrArraySize = 1;
    sbt_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    sbt_desc.Format = DXGI_FORMAT_UNKNOWN;
    sbt_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    sbt_desc.Width = shader_record_size;
    sbt_desc.Height = 1;
    sbt_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    sbt_desc.SampleDesc.Count = 1;
    sbt_desc.SampleDesc.Quality = 0;
    sbt_desc.MipLevels = 1;
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE, &sbt_desc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&raygen_sbt_storage)));
    char* mapped;
    raygen_sbt_storage->Map(0, nullptr, (void**)&mapped);
    memcpy(mapped, raygen_shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    mapped += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    memcpy(mapped, &root_args, sizeof(root_args));
    raygen_sbt_storage->Unmap(0, nullptr);
    printf("Raygen's SBT size is %d\n", D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(root_args));

    void* miss_shader_id = rt_state_object_props->GetShaderIdentifier(L"MyMissShader");
    shader_record_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    sbt_desc.Width = shader_record_size;
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE, &sbt_desc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&miss_sbt_storage)));
    miss_sbt_storage->Map(0, nullptr, (void**)&mapped);
    memcpy(mapped, miss_shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    miss_sbt_storage->Unmap(0, nullptr);

    void* hit_shader_id = rt_state_object_props->GetShaderIdentifier(L"MyHitGroup");
    void* hit_shader_id1 = rt_state_object_props->GetShaderIdentifier(L"MyHitGroup1");
    void* hit_shader_id2 = rt_state_object_props->GetShaderIdentifier(L"MyHitGroup2");
    shader_record_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    sbt_desc.Width = shader_record_size * 3;
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE, &sbt_desc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&hit_sbt_storage)));
    hit_sbt_storage->Map(0, nullptr, (void**)&mapped);
    memcpy(mapped, hit_shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(mapped + 32, hit_shader_id1, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(mapped + 64, hit_shader_id2, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    hit_sbt_storage->Unmap(0, nullptr);
  }
#endif
}

void ObjScene1::Render() {
  // Just clear RTV
  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);

  float bg_color[] = { 0.8f, 1.0f, 0.8f, 1.0f };
  CE(command_list->Reset(command_allocator, nullptr));
  command_list->SetGraphicsRootSignature(root_sig);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    rt_output_resource,
    D3D12_RESOURCE_STATE_COPY_SOURCE,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS)));
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);

  command_list->SetDescriptorHeaps(1, &srv_uav_heap);

#ifndef TEMP_FLAG
  CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav(
    srv_uav_heap->GetGPUDescriptorHandleForHeapStart());
  command_list->SetComputeRootSignature(global_rootsig);
  command_list->SetComputeRootDescriptorTable(0, handle_uav);
  command_list->SetComputeRootShaderResourceView(1, NULL);
  D3D12_DISPATCH_RAYS_DESC desc{};
  desc.RayGenerationShaderRecord.StartAddress = raygen_sbt_storage->GetGPUVirtualAddress();
  desc.RayGenerationShaderRecord.SizeInBytes = 64;
  desc.Width = WIN_W;
  desc.Height = WIN_H;
  desc.Depth = 1;
#else
  CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav(
    srv_uav_heap->GetGPUDescriptorHandleForHeapStart());
  command_list->SetComputeRootSignature(global_rootsig);
  command_list->SetComputeRootDescriptorTable(0, handle_uav);
  command_list->SetComputeRootShaderResourceView(1, NULL);
  D3D12_DISPATCH_RAYS_DESC desc{};
  desc.RayGenerationShaderRecord.StartAddress = raygen_sbt_storage->GetGPUVirtualAddress();
  desc.RayGenerationShaderRecord.SizeInBytes = 64;
  desc.MissShaderTable.StartAddress = miss_sbt_storage->GetGPUVirtualAddress();
  desc.MissShaderTable.SizeInBytes = 32;
  desc.MissShaderTable.StrideInBytes = 32;
  desc.HitGroupTable.StartAddress = hit_sbt_storage->GetGPUVirtualAddress();
  desc.HitGroupTable.SizeInBytes = 64;
  desc.HitGroupTable.StrideInBytes = 32;
  desc.Width = WIN_W;
  desc.Height = WIN_H;
  desc.Depth = 1;
#endif
  command_list->SetPipelineState1(rt_state_object);
  command_list->DispatchRays(&desc);

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    rt_output_resource,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    D3D12_RESOURCE_STATE_COPY_SOURCE)));

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_COPY_DEST)));
  command_list->CopyResource(g_rendertargets[g_frame_index], rt_output_resource);
  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_COPY_DEST,
    D3D12_RESOURCE_STATE_PRESENT)));

  CE(command_list->Close());
  g_command_queue->ExecuteCommandLists(1,
    (ID3D12CommandList* const*)&command_list);
  CE(g_swapchain->Present(1, 0));
  WaitForPreviousFrame();
}

void ObjScene1::Update(float secs) {
}