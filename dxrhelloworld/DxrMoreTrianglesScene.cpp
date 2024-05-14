#include "scene.hpp"

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

// In DxrObjScene.cpp
IDxcBlob* CompileShaderLibrary(LPCWSTR fileName);
int RoundUp(int x, int align);
void WaitForPreviousFrame();

MoreTrianglesScene::MoreTrianglesScene() {
  // Create command list
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
    nullptr, IID_PPV_ARGS(&command_list)));
  command_list->Close();

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

  // Geometry
  const float l = 0.7f;
  {
    int16_t indices[] = { 0,1,2 };
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

    // AABBs
    const float eps = 1e-4;
    D3D12_RAYTRACING_AABB aabbs[2] = {};
    aabbs[0].MinX = -l * 0.4f + 0.5f;
    aabbs[0].MinY = -l * 0.4f - 0.5f;
    aabbs[0].MinZ = 0.4f - eps;
    aabbs[0].MaxX = l * 0.4f + 0.5f;
    aabbs[0].MaxY = l * 0.4f - 0.5f;
    aabbs[0].MaxZ = 0.4f + eps;

    aabbs[1] = aabbs[0];
    aabbs[1].MinY = -l * 0.4f + 0.5f;
    aabbs[1].MaxY = l * 0.4f + 0.5f;

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(aabbs))),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&proc_aabb_buffer)));
    proc_aabb_buffer->Map(0, nullptr, &mapped);
    memcpy(mapped, &(aabbs[0]), sizeof(aabbs));
    proc_aabb_buffer->Unmap(0, nullptr);
  }

  // Triangles in the center for any-hit
  std::vector<Vertex> vertices2;
  for (int i=-2; i<=2; i++) {
    const float l0 = 0.1f;
    Vertex v0(0, -l0, 1.0);
    Vertex v1(-l0, l0, 1.0);
    Vertex v2(l0, l0, 1.0);
    const float dx = l0 * 0.2f;
    v0.x += i * dx;
    v1.x += i * dx;
    v2.x += i * dx;
    v0.z += i * 0.01f;
    v1.z += i * 0.01f;
    v2.z += i * 0.01f;
    vertices2.push_back(v0);
    vertices2.push_back(v1);
    vertices2.push_back(v2);
  };

  {
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(Vertex) * vertices2.size())),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&anyhit_vertex_buffer)));
    char* mapped;
    anyhit_vertex_buffer->Map(0, nullptr, (void**)&mapped);
    memcpy(mapped, vertices2.data(), sizeof(Vertex)* vertices2.size());
    anyhit_vertex_buffer->Unmap(0, nullptr);
  }

  {
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(512)),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&transform_matrices0)));

    char* mapped;
    Mat3x4 mats[2] = {};
    mats[0].m[0][0] = 0.4f;
    mats[0].m[1][1] = 0.4f;
    mats[0].m[2][2] = 0.4f;
    mats[0].m[0][3] = -0.5f;  // left-top
    mats[0].m[1][3] = -0.5f;
    mats[0].m[2][3] = 0.0f;

    mats[1] = mats[0];
    mats[1].m[1][3] = 0.5f;  // left-bottom

    transform_matrices0->Map(0, nullptr, (void**)(&mapped));
    memcpy(mapped, &(mats[0]), sizeof(mats));
    transform_matrices0->Unmap(0, nullptr);

    g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
    WaitForPreviousFrame();
  }

  // AS
  {
    D3D12_RAYTRACING_GEOMETRY_DESC geom_desc[2];
    geom_desc[0].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geom_desc[0].Triangles.VertexBuffer.StartAddress = vertex_buffer->GetGPUVirtualAddress();
    geom_desc[0].Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
    geom_desc[0].Triangles.VertexCount = vertex_buffer->GetDesc().Width / sizeof(Vertex);
    geom_desc[0].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geom_desc[0].Triangles.IndexBuffer = index_buffer->GetGPUVirtualAddress();
    geom_desc[0].Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
    geom_desc[0].Triangles.IndexCount = index_buffer->GetDesc().Width / sizeof(int16_t);
    geom_desc[0].Triangles.Transform3x4 = transform_matrices0->GetGPUVirtualAddress();
    geom_desc[0].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    geom_desc[1] = geom_desc[0];
    geom_desc[1].Triangles.Transform3x4 = transform_matrices0->GetGPUVirtualAddress() + sizeof(float) * 12;

    D3D12_RAYTRACING_GEOMETRY_DESC proc_desc{};
    proc_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
    proc_desc.AABBs.AABBCount = 2;
    proc_desc.AABBs.AABBs.StartAddress = proc_aabb_buffer->GetGPUVirtualAddress();
    proc_desc.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
    proc_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_buildinfo{};
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas0_buildinfo{};
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas1_buildinfo{};
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas2_buildinfo{};

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas0_inputs{};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas1_inputs{};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas2_inputs{};
    tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    tlas_inputs.NumDescs = 3;
    tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_buildinfo);
    blas0_inputs = tlas_inputs;
    blas0_inputs.NumDescs = 2;
    blas0_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas0_inputs.pGeometryDescs = geom_desc;
    
    blas1_inputs = tlas_inputs;
    blas1_inputs.NumDescs = 1;
    blas1_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas1_inputs.pGeometryDescs = &proc_desc;

    D3D12_RAYTRACING_GEOMETRY_DESC geom_desc2;
    geom_desc2.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geom_desc2.Triangles.VertexBuffer.StartAddress = anyhit_vertex_buffer->GetGPUVirtualAddress();
    geom_desc2.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
    geom_desc2.Triangles.VertexCount = vertices2.size();
    geom_desc2.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geom_desc2.Triangles.IndexBuffer = 0;
    geom_desc2.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
    geom_desc2.Triangles.IndexCount = 0;
    geom_desc2.Triangles.Transform3x4 = 0;
    geom_desc2.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

    blas2_inputs = tlas_inputs;
    blas2_inputs.NumDescs = 1;
    blas2_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas2_inputs.pGeometryDescs = &geom_desc2;

    g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&blas0_inputs, &blas0_buildinfo);
    g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&blas1_inputs, &blas1_buildinfo);
    g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&blas1_inputs, &blas2_buildinfo);

    printf("Scratch space: TLAS=%d, BLAS0=%d, BLAS1=%d, BLAS2=%d\n",
      tlas_buildinfo.ScratchDataSizeInBytes,
      blas0_buildinfo.ScratchDataSizeInBytes,
      blas1_buildinfo.ScratchDataSizeInBytes,
      blas2_buildinfo.ScratchDataSizeInBytes);
    printf("AS space: TLAS=%d, BLAS0=%d, BLAS1=%d, BLAS2=%d\n",
      tlas_buildinfo.ResultDataMaxSizeInBytes,
      blas0_buildinfo.ResultDataMaxSizeInBytes,
      blas1_buildinfo.ResultDataMaxSizeInBytes,
      blas2_buildinfo.ResultDataMaxSizeInBytes);
    printf("Update space: TLAS=%d, BLAS0=%d, BLAS1=%d, BLAS2=%d\n",
      tlas_buildinfo.UpdateScratchDataSizeInBytes,
      blas0_buildinfo.UpdateScratchDataSizeInBytes,
      blas1_buildinfo.UpdateScratchDataSizeInBytes,
      blas2_buildinfo.UpdateScratchDataSizeInBytes);

    size_t scratch_size = std::max(blas0_buildinfo.ScratchDataSizeInBytes, tlas_buildinfo.ScratchDataSizeInBytes);
    scratch_size = std::max(blas1_buildinfo.ScratchDataSizeInBytes, scratch_size);
    scratch_size = std::max(blas2_buildinfo.ScratchDataSizeInBytes, scratch_size);

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
      &keep(CD3DX12_RESOURCE_DESC::Buffer(blas0_buildinfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)),
      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
      nullptr,
      IID_PPV_ARGS(&blas0)));

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(blas1_buildinfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)),
      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
      nullptr,
      IID_PPV_ARGS(&blas1)));

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(blas2_buildinfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)),
      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
      nullptr,
      IID_PPV_ARGS(&blas2)));

    ID3D12Resource* instance_descs;
    D3D12_RAYTRACING_INSTANCE_DESC instance_descs_cpu[3] = {};
    instance_descs_cpu[0].Transform[0][0] = 1;
    instance_descs_cpu[0].Transform[1][1] = 1;
    instance_descs_cpu[0].Transform[2][2] = 1;
    instance_descs_cpu[0].InstanceMask = 1;
    instance_descs_cpu[0].AccelerationStructure = blas0->GetGPUVirtualAddress();
    instance_descs_cpu[1].Transform[0][0] = 1;
    instance_descs_cpu[1].Transform[1][1] = 1;
    instance_descs_cpu[1].Transform[2][2] = 1;
    instance_descs_cpu[1].InstanceMask = 1;
    instance_descs_cpu[1].AccelerationStructure = blas1->GetGPUVirtualAddress();
    instance_descs_cpu[1].InstanceContributionToHitGroupIndex = 1;  // Use hit group 1, intersection shader
    instance_descs_cpu[2].Transform[0][0] = 1;
    instance_descs_cpu[2].Transform[1][1] = 1;
    instance_descs_cpu[2].Transform[2][2] = 1;
    instance_descs_cpu[2].InstanceMask = 1;
    instance_descs_cpu[2].AccelerationStructure = blas2->GetGPUVirtualAddress();
    instance_descs_cpu[2].InstanceContributionToHitGroupIndex = 2; // Anyhit shader

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(sizeof(instance_descs_cpu))),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&instance_descs)));
    char* mapped;
    instance_descs->Map(0, nullptr, (void**)&mapped);
    memcpy(mapped, &(instance_descs_cpu[0]), sizeof(instance_descs_cpu));
    instance_descs->Unmap(0, nullptr);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build_desc{};
    tlas_inputs.InstanceDescs = instance_descs->GetGPUVirtualAddress();
    tlas_build_desc.Inputs = tlas_inputs;
    tlas_build_desc.DestAccelerationStructureData = tlas->GetGPUVirtualAddress();
    tlas_build_desc.ScratchAccelerationStructureData = as_scratch->GetGPUVirtualAddress();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas0_build_desc{};
    blas0_build_desc.Inputs = blas0_inputs;
    blas0_build_desc.DestAccelerationStructureData = blas0->GetGPUVirtualAddress();
    blas0_build_desc.ScratchAccelerationStructureData = as_scratch->GetGPUVirtualAddress();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas1_build_desc{};
    blas1_build_desc.Inputs = blas1_inputs;
    blas1_build_desc.DestAccelerationStructureData = blas1->GetGPUVirtualAddress();
    blas1_build_desc.ScratchAccelerationStructureData = as_scratch->GetGPUVirtualAddress();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas2_build_desc{};
    blas2_build_desc.Inputs = blas2_inputs;
    blas2_build_desc.DestAccelerationStructureData = blas2->GetGPUVirtualAddress();
    blas2_build_desc.ScratchAccelerationStructureData = as_scratch->GetGPUVirtualAddress();

    command_list->Reset(command_allocator, nullptr);
    command_list->BuildRaytracingAccelerationStructure(&blas0_build_desc, 0, nullptr);
    command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(blas0)));
    // Need to have a barrier between building 2 BLAS's
    command_list->BuildRaytracingAccelerationStructure(&blas1_build_desc, 0, nullptr);
    command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(blas1)));
    command_list->BuildRaytracingAccelerationStructure(&blas2_build_desc, 0, nullptr);
    command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(blas2)));
    command_list->BuildRaytracingAccelerationStructure(&tlas_build_desc, 0, nullptr);
    command_list->Close();
    g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
  }

  // Shader binding table
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

  // Output resource
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

    // UAV heap
    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.NumDescriptors = 2;
    dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    dhd.NodeMask = 0;
    g_device12->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&srv_uav_heap));
    srv_uav_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  
    CD3DX12_CPU_DESCRIPTOR_HANDLE uav_handle(srv_uav_heap->GetCPUDescriptorHandleForHeapStart(), 0, srv_uav_descriptor_size);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    g_device12->CreateUnorderedAccessView(rt_output_resource, nullptr, &uav_desc, uav_handle);
  }

  // SRV of TLAS
  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(srv_uav_heap->GetCPUDescriptorHandleForHeapStart(), 1, srv_uav_descriptor_size);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress();
    g_device12->CreateShaderResourceView(nullptr, &srv_desc, srv_handle);
  }

}

void MoreTrianglesScene::Render() {
  CD3DX12_CPU_DESCRIPTOR_HANDLE handle_rtv(
    g_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
    g_frame_index, g_rtv_descriptor_size);

  float bg_color[] = { 0.8f, 1.0f, 1.0f, 1.0f };
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    g_rendertargets[g_frame_index],
    D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET)));

  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    rt_output_resource,
    D3D12_RESOURCE_STATE_COPY_SOURCE,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS)));

  command_list->SetComputeRootSignature(global_rootsig);
  command_list->SetDescriptorHeaps(1, &srv_uav_heap);

  CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav(
    srv_uav_heap->GetGPUDescriptorHandleForHeapStart());
  command_list->SetComputeRootDescriptorTable(0, handle_uav);
  command_list->SetComputeRootShaderResourceView(1, tlas->GetGPUVirtualAddress());

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

void MoreTrianglesScene::Update(float secs) {
}