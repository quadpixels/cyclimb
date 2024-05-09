#include "scene.hpp"

extern int WIN_W, WIN_H;
extern ID3D12Device5* g_device12;

MoreTrianglesScene::MoreTrianglesScene() {
  // Create command list
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
    IID_PPV_ARGS(&command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
    nullptr, IID_PPV_ARGS(&command_list)));

  // Create root signature
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

  ID3DBlob* signature, * error;
  CE(D3D12SerializeRootSignature(
    &rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
  CE(g_device12->CreateRootSignature(
    0, signature->GetBufferPointer(), signature->GetBufferSize(),
    IID_PPV_ARGS(&global_rootsig)));
}

void MoreTrianglesScene::Render() {
}

void MoreTrianglesScene::Update(float secs) {
}