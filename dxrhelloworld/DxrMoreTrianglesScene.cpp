#include "scene.hpp"

#include <d3dx12.h>

extern int WIN_W, WIN_H;
extern ID3D12Device5* g_device12;

// In DxrObjScene.cpp
IDxcBlob* CompileShaderLibrary(LPCWSTR fileName);

MoreTrianglesScene::MoreTrianglesScene() {
  // Create command list
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
    nullptr, IID_PPV_ARGS(&command_list)));

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
    // 1 - Triangle hit group
    // 1 - Shader config
    // 2 - Local root signature and association
    // 1 - Global root signature
    // 1 - Pipeline config

    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(7);

    // 1. DXIL library
    D3D12_DXIL_LIBRARY_DESC dxil_lib_desc = {};
    IDxcBlob* dxil_library = CompileShaderLibrary(L"shaders/raytracing_tutorial.hlsl");
    D3D12_EXPORT_DESC dxil_lib_exports[3];
    dxil_lib_exports[0].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[0].ExportToRename = nullptr;
    dxil_lib_exports[0].Name = L"MyRaygenShader";
    dxil_lib_exports[1].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[1].ExportToRename = nullptr;
    dxil_lib_exports[1].Name = L"MyClosestHitShader";
    dxil_lib_exports[2].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[2].ExportToRename = nullptr;
    dxil_lib_exports[2].Name = L"MyMissShader";
    dxil_lib_desc.DXILLibrary.pShaderBytecode = dxil_library->GetBufferPointer();
    dxil_lib_desc.DXILLibrary.BytecodeLength = dxil_library->GetBufferSize();
    dxil_lib_desc.NumExports = 3;
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
    rtpso_desc.NumSubobjects = 7;
    rtpso_desc.pSubobjects = subobjects.data();
    g_device12->CreateStateObject(&rtpso_desc, IID_PPV_ARGS(&rt_state_object));
  }

  // SRV heap
  {
    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.NumDescriptors = 1;
    dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    dhd.NodeMask = 0;
    g_device12->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&srv_uav_heap));
    srv_uav_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }

  // Geometry
  {
    int16_t indices[] = { 0,1,2 };
    const float l = 0.7f;
    Vertex vertices[] = {
      { 0, -l, 1.0 },
      { -l, l, 1.0 },
      { l, l, 1.0 },
    };

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(indices))),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&index_buffer)));
    void* mapped;
    index_buffer->Map(0, nullptr, &mapped);
    memcpy(mapped, indices, sizeof(indices));
    index_buffer->Unmap(0, nullptr);

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices))),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&vertex_buffer)));
    vertex_buffer->Map(0, nullptr, &mapped);
    memcpy(mapped, vertices, sizeof(vertices));
    vertex_buffer->Unmap(0, nullptr);
  }

  // AS
  {
    D3D12_RAYTRACING_GEOMETRY_DESC geom_desc{};
    geom_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geom_desc.Triangles.VertexBuffer.StartAddress = vertex_buffer->GetGPUVirtualAddress();
    geom_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
    geom_desc.Triangles.VertexCount = vertex_buffer->GetDesc().Width / sizeof(Vertex);
    geom_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geom_desc.Triangles.IndexBuffer = index_buffer->GetGPUVirtualAddress();
    geom_desc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
    geom_desc.Triangles.IndexCount = index_buffer->GetDesc().Width / sizeof(int16_t);
    geom_desc.Triangles.Transform3x4 = 0;
    geom_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_buildinfo{};
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_buildinfo{};

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
    tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    tlas_inputs.NumDescs = 1;
    tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_buildinfo);
    blas_inputs = tlas_inputs;
    blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas_inputs.pGeometryDescs = &geom_desc;
    g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&blas_inputs, &blas_buildinfo);
    printf("Scratch space: TLAS = %d, BLAS = %d\n",
      tlas_buildinfo.ScratchDataSizeInBytes, blas_buildinfo.ScratchDataSizeInBytes);
    printf("AS space: TLAS = %d, BLAS = %d\n",
      tlas_buildinfo.ResultDataMaxSizeInBytes, blas_buildinfo.ResultDataMaxSizeInBytes);
    printf("Update space: TLAS = %d, BLAS = %d\n",
      tlas_buildinfo.UpdateScratchDataSizeInBytes, blas_buildinfo.UpdateScratchDataSizeInBytes);

    int scratch_size = std::max(
      blas_buildinfo.ScratchDataSizeInBytes,
      tlas_buildinfo.ScratchDataSizeInBytes);
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(scratch_size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      nullptr,
      IID_PPV_ARGS(&as_scratch)));

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(tlas_buildinfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)),
      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
      nullptr,
      IID_PPV_ARGS(&tlas)));

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(blas_buildinfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)),
      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
      nullptr,
      IID_PPV_ARGS(&blas)));

    ID3D12Resource* instance_descs;
    D3D12_RAYTRACING_INSTANCE_DESC instance_desc{};
    instance_desc.Transform[0][0] = 1;
    instance_desc.Transform[1][1] = 1;
    instance_desc.Transform[2][2] = 1;
    instance_desc.InstanceMask = 1;
    instance_desc.AccelerationStructure = blas->GetGPUVirtualAddress();
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(instance_desc))),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&instance_descs)));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build_desc{};
    tlas_inputs.InstanceDescs = instance_descs->GetGPUVirtualAddress();
    tlas_build_desc.Inputs = tlas_inputs;
    tlas_build_desc.DestAccelerationStructureData = tlas->GetGPUVirtualAddress();
    tlas_build_desc.ScratchAccelerationStructureData = as_scratch->GetGPUVirtualAddress();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_build_desc{};
    blas_build_desc.Inputs = blas_inputs;
    blas_build_desc.DestAccelerationStructureData = blas->GetGPUVirtualAddress();
    blas_build_desc.ScratchAccelerationStructureData = as_scratch->GetGPUVirtualAddress();

    command_list->Close();
    command_list->Reset(command_allocator, nullptr);
    command_list->BuildRaytracingAccelerationStructure(&blas_build_desc, 0, nullptr);
    command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(blas)));
    command_list->BuildRaytracingAccelerationStructure(&tlas_build_desc, 0, nullptr);
  }
}

void MoreTrianglesScene::Render() {
}

void MoreTrianglesScene::Update(float secs) {
}