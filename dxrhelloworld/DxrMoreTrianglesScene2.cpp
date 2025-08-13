#include "scene.hpp"

#include "dxrhelloworld/x64/Debug/CompiledShaders/raytracing_moretriangles2.hlsl.h"

#include <d3dx12.h>
#include <dxgi1_4.h>

extern int WIN_W, WIN_H;
extern ID3D12Device5* g_device12;
extern ID3D12CommandQueue* g_command_queue;
extern ID3D12DescriptorHeap* g_rtv_heap;
extern int g_rtv_descriptor_size;
extern int g_frame_index;
extern ID3D12Resource* g_rendertargets[];
extern IDXGISwapChain3* g_swapchain;

// In DxrObjScene.cpp
int RoundUp(int x, int align);
void WaitForPreviousFrame();

// Front-facing, back-facing
MoreTrianglesScene2::MoreTrianglesScene2() {
  // Create command list
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
    nullptr, IID_PPV_ARGS(&command_list)));
  command_list->Close();

  // 1. RootSig
  {
    D3D12_ROOT_PARAMETER root_params[3]{};
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[0].DescriptorTable.NumDescriptorRanges = 1;
    D3D12_DESCRIPTOR_RANGE desc_range{};
    desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    desc_range.NumDescriptors = 2;
    desc_range.BaseShaderRegister = 0;
    desc_range.RegisterSpace = 0;
    desc_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_params[0].DescriptorTable.pDescriptorRanges = &desc_range;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_params[1].Descriptor.RegisterSpace = 0;
    root_params[1].Descriptor.ShaderRegister = 0;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_params[2].Constants.Num32BitValues = 1;  // ray flag
    root_params[2].Constants.ShaderRegister = 0;
    root_params[2].Constants.RegisterSpace = 0;
    root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
    rootsig_desc.NumStaticSamplers = 0;
    rootsig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rootsig_desc.NumParameters = _countof(root_params);
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

  // 2. RTPSO
  {
    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(5);

    // 1. DXIL library
    D3D12_DXIL_LIBRARY_DESC dxil_lib_desc{};
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
    dxil_lib_desc.DXILLibrary.pShaderBytecode = g_ShadersMoreTrianglesScene2;
    dxil_lib_desc.DXILLibrary.BytecodeLength = sizeof(g_ShadersMoreTrianglesScene2);
    dxil_lib_desc.NumExports = _countof(dxil_lib_exports);
    dxil_lib_desc.pExports = dxil_lib_exports;

    D3D12_STATE_SUBOBJECT subobj_dxil_lib{};
    subobj_dxil_lib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobj_dxil_lib.pDesc = &dxil_lib_desc;
    subobjects.push_back(subobj_dxil_lib);

    // 2. Hit Group for triangles
    D3D12_STATE_SUBOBJECT subobj_hitgroup{};
    subobj_hitgroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    D3D12_HIT_GROUP_DESC hitgroup_desc{};
    hitgroup_desc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitgroup_desc.ClosestHitShaderImport = L"MyClosestHitShader";
    hitgroup_desc.HitGroupExport = L"TriangleHitGroup";
    subobj_hitgroup.pDesc = &hitgroup_desc;
    subobjects.push_back(subobj_hitgroup);

    // 3. Shader config
    D3D12_STATE_SUBOBJECT subobj_shaderconfig{};
    subobj_shaderconfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    D3D12_RAYTRACING_SHADER_CONFIG shader_config{};
    shader_config.MaxAttributeSizeInBytes = 8; // bary
    shader_config.MaxPayloadSizeInBytes = 16;
    subobj_shaderconfig.pDesc = &shader_config;
    subobjects.push_back(subobj_shaderconfig);

    // 4. Global root signature
    D3D12_STATE_SUBOBJECT subobj_rootsig{};
    subobj_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobj_rootsig.pDesc = &global_rootsig;
    subobjects.push_back(subobj_rootsig);

    // 5. Pipeline config
    D3D12_STATE_SUBOBJECT subobj_pipelineconfig{};
    subobj_pipelineconfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
    pipeline_config.MaxTraceRecursionDepth = 1;
    subobj_pipelineconfig.pDesc = &pipeline_config;
    subobjects.push_back(subobj_pipelineconfig);

    D3D12_STATE_OBJECT_DESC rtpso_desc{};
    rtpso_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    rtpso_desc.NumSubobjects = subobjects.size();
    rtpso_desc.pSubobjects = subobjects.data();
    CE(g_device12->CreateStateObject(&rtpso_desc, IID_PPV_ARGS(&rt_state_object)));
    rt_state_object->QueryInterface(IID_PPV_ARGS(&rt_state_object_props));
  }

  // Geometry
  {
    const float l = 0.2f;
    const float x_ofst = 0.5f;
    //  ^ +Y 
    //  |
    //  +---> +X
    Vertex vertices[] = {
      // Clockwise (CW)
      { -l, -l, 0 },
      { 0,  l,  0 },
      { l,  -l, 0 },

      // Counter-clockwise (CCW)
      { l, -l, 0 },
      { 0,  l, 0 },
      {-l, -l, 0 }
    };

    for (uint32_t i = 0; i < 3; i++) {
      vertices[i].x -= x_ofst;
    }
    for (uint32_t i = 3; i < 6; i++) {
      vertices[i].x += x_ofst;
    }

    uint16_t indices[] = { 0,1,2,3,4,5 };

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
    D3D12_RAYTRACING_GEOMETRY_DESC geom_desc[1]{};
    geom_desc[0].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geom_desc[0].Triangles.VertexBuffer.StartAddress = vertex_buffer->GetGPUVirtualAddress();
    geom_desc[0].Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
    geom_desc[0].Triangles.VertexCount = vertex_buffer->GetDesc().Width / sizeof(Vertex);
    geom_desc[0].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geom_desc[0].Triangles.IndexBuffer = index_buffer->GetGPUVirtualAddress();
    geom_desc[0].Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
    geom_desc[0].Triangles.IndexCount = index_buffer->GetDesc().Width / sizeof(int16_t);
    geom_desc[0].Triangles.Transform3x4 = NULL;
    geom_desc[0].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_buildinfo{};
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas0_buildinfo{};

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas0_inputs{};

    tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    tlas_inputs.NumDescs = 1;
    tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_buildinfo);

    blas0_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blas0_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    blas0_inputs.NumDescs = _countof(geom_desc);
    blas0_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas0_inputs.pGeometryDescs = geom_desc;
    g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&blas0_inputs, &blas0_buildinfo);

    printf("[MoreTrianglesScene2] tlas scratch=%u, blas_scratch=%u\n",
      tlas_buildinfo.ScratchDataSizeInBytes,
      blas0_buildinfo.ScratchDataSizeInBytes);

    size_t scratch_size = std::max(blas0_buildinfo.ScratchDataSizeInBytes, tlas_buildinfo.ScratchDataSizeInBytes);

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
      D3D12_HEAP_FLAG_NONE,
      &keep(CD3DX12_RESOURCE_DESC::Buffer(scratch_size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)),
      D3D12_RESOURCE_STATE_COMMON,
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

    // BLAS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas0_build_desc{};
    blas0_build_desc.Inputs = blas0_inputs;
    blas0_build_desc.DestAccelerationStructureData = blas0->GetGPUVirtualAddress();
    blas0_build_desc.ScratchAccelerationStructureData = as_scratch->GetGPUVirtualAddress();

    // TLAS
    ID3D12Resource* instance_descs;
    D3D12_RAYTRACING_INSTANCE_DESC instance_descs_cpu[1] = {};
    instance_descs_cpu[0].Transform[0][0] = 1;
    instance_descs_cpu[0].Transform[1][1] = 1;
    instance_descs_cpu[0].Transform[2][2] = 1;
    instance_descs_cpu[0].InstanceMask = 1;
    instance_descs_cpu[0].AccelerationStructure = blas0->GetGPUVirtualAddress();
    instance_descs_cpu[0].InstanceContributionToHitGroupIndex = 0;  // Use hit group 0

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

    command_list->Reset(command_allocator, nullptr);
    command_list->BuildRaytracingAccelerationStructure(&blas0_build_desc, 0, nullptr);
    command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::UAV(blas0)));
    command_list->BuildRaytracingAccelerationStructure(&tlas_build_desc, 0, nullptr);
    command_list->Close();
    g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&command_list));
  }

  // SBT
  {
    int shader_record_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(RayGenConstantBuffer);
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

    sbt_desc.Width = shader_record_size;  // 64
    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE, &sbt_desc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&miss_sbt_storage)));

    CE(g_device12->CreateCommittedResource(
      &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
      D3D12_HEAP_FLAG_NONE, &sbt_desc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&hit_sbt_storage)));
  }

  // Output Resources
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
    dhd.NumDescriptors = 3;
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
    CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(srv_uav_heap->GetCPUDescriptorHandleForHeapStart(), 2, srv_uav_descriptor_size);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress();
    g_device12->CreateShaderResourceView(nullptr, &srv_desc, srv_handle);
  }
}

void MoreTrianglesScene2::Render() {
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

  command_list->ResourceBarrier(1, &keep(CD3DX12_RESOURCE_BARRIER::Transition(
    rt_output_resource,
    D3D12_RESOURCE_STATE_COPY_SOURCE,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS)));

  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);

  command_list->SetComputeRootSignature(global_rootsig);
  command_list->SetDescriptorHeaps(1, &srv_uav_heap);

  D3D12_DISPATCH_RAYS_DESC desc{};
  desc.RayGenerationShaderRecord.StartAddress = raygen_sbt_storage->GetGPUVirtualAddress();
  desc.RayGenerationShaderRecord.SizeInBytes = 64;
  //desc.MissShaderTable.StartAddress = miss_sbt_storage->GetGPUVirtualAddress();
  //desc.MissShaderTable.SizeInBytes = 32;
  //desc.MissShaderTable.StrideInBytes = 32;
  //desc.HitGroupTable.StartAddress = hit_sbt_storage->GetGPUVirtualAddress();
  //desc.HitGroupTable.SizeInBytes = 64;
  //desc.HitGroupTable.StrideInBytes = 64;
  //desc.CallableShaderTable.StartAddress = 0;
  //desc.CallableShaderTable.SizeInBytes = 32;
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

void MoreTrianglesScene2::Update(float secs) {
  RayGenConstantBuffer cb{};
  void* raygen_shader_id = rt_state_object_props->GetShaderIdentifier(L"MyRaygenShader");
  void* miss_shader_id = rt_state_object_props->GetShaderIdentifier(L"MyMissShader");
  void* hit_shader_id = rt_state_object_props->GetShaderIdentifier(L"TriangleHitGroup");

  char* mapped;
  raygen_sbt_storage->Map(0, nullptr, (void**)&mapped);
  memcpy(mapped, raygen_shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  mapped += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  memcpy(mapped, &cb, sizeof(cb));
  raygen_sbt_storage->Unmap(0, nullptr);

  miss_sbt_storage->Map(0, nullptr, (void**)&mapped);
  memcpy(mapped, miss_shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  mapped += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  memcpy(mapped, &cb, sizeof(cb));
  miss_sbt_storage->Unmap(0, nullptr);

  hit_sbt_storage->Map(0, nullptr, (void**)&mapped);
  memcpy(mapped, hit_shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  memcpy(mapped + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, &cb, sizeof(cb));
  hit_sbt_storage->Unmap(0, nullptr);
}