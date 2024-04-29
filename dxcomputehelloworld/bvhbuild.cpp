#include "bvhtypes.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <exception>
#include <vector>

extern void CE(HRESULT x);
extern ID3D11Device* g_device11;
extern ID3D11DeviceContext* g_context11;

ID3D11ComputeShader* BuildComputeShader(LPCWSTR src_file, LPCSTR entry_point);
ID3D11Buffer* CreateRawBuffer(int size);
ID3D11Buffer* CreateRawBufferCPUWriteable(int size);
ID3D11UnorderedAccessView* CreateBufferUAV(ID3D11Buffer* buffer);
ID3D11Buffer* CreateStructuredBuffer(UINT element_size, UINT count);

int RoundUpToAlign(int x, int align) {
  if (x % align != 0) {
    x += align - (x % align);
  }
  return x;
}

void CornellBoxTest() {
  Triangle h_triangles[] = {
    Triangle(-1.01,0,0.99,1,0,0.99,-0.99,0,-1.04),
    Triangle(1,0,0.99,1,0,-1.04,-0.99,0,-1.04),
    Triangle(-1.02,1.99,0.99,-1.02,1.99,-1.04,1,1.99,0.99),
    Triangle(-1.02,1.99,-1.04,1,1.99,-1.04,1,1.99,0.99),
    Triangle(-0.99,0,-1.04,1,0,-1.04,1,1.99,-1.04),
    Triangle(-0.99,0,-1.04,1,1.99,-1.04,-1.02,1.99,-1.04),
    Triangle(1,0,-1.04,1,0,0.99,1,1.99,-1.04),
    Triangle(1,0,0.99,1,1.99,0.99,1,1.99,-1.04),
    Triangle(-1.01,0,0.99,-0.99,0,-1.04,-1.02,1.99,-1.04),
    Triangle(-1.01,0,0.99,-1.02,1.99,-1.04,-1.02,1.99,0.99),
    Triangle(0.53,0.6,0.75,0.7,0.6,0.17,-0.05,0.6,0.57),
    Triangle(0.7,0.6,0.17,0.13,0.6,0,-0.05,0.6,0.57),
    Triangle(-0.05,0,0.57,-0.05,0.6,0.57,0.13,0,0),
    Triangle(-0.05,0.6,0.57,0.13,0.6,0,0.13,0,0),
    Triangle(0.53,0,0.75,0.53,0.6,0.75,-0.05,0,0.57),
    Triangle(0.53,0.6,0.75,-0.05,0.6,0.57,-0.05,0,0.57),
    Triangle(0.7,0,0.17,0.7,0.6,0.17,0.53,0,0.75),
    Triangle(0.7,0.6,0.17,0.53,0.6,0.75,0.53,0,0.75),
    Triangle(0.13,0,0,0.13,0.6,0,0.7,0,0.17),
    Triangle(0.13,0.6,0,0.7,0.6,0.17,0.7,0,0.17),
    Triangle(0.7,0,0.17,0.7,0.6,0.17,0.53,0,0.75),
    Triangle(0.7,0.6,0.17,0.53,0.6,0.75,0.53,0,0.75),
    Triangle(-0.53,1.2,0.09,0.04,1.2,-0.09,-0.71,1.2,-0.49),
    Triangle(0.04,1.2,-0.09,-0.14,1.2,-0.67,-0.71,1.2,-0.49),
    Triangle(-0.53,0,0.09,-0.53,1.2,0.09,-0.71,0,-0.49),
    Triangle(-0.53,1.2,0.09,-0.71,1.2,-0.49,-0.71,0,-0.49),
    Triangle(-0.71,0,-0.49,-0.71,1.2,-0.49,-0.14,0,-0.67),
    Triangle(-0.71,1.2,-0.49,-0.14,1.2,-0.67,-0.14,0,-0.67),
    Triangle(-0.14,0,-0.67,-0.14,1.2,-0.67,0.04,0,-0.09),
    Triangle(-0.14,1.2,-0.67,0.04,1.2,-0.09,0.04,0,-0.09),
    Triangle(0.04,0,-0.09,0.04,1.2,-0.09,-0.53,0,0.09),
    Triangle(0.04,1.2,-0.09,-0.53,1.2,0.09,-0.53,0,0.09),
    Triangle(0.04,0,-0.09,0.04,1.2,-0.09,-0.53,0,0.09),
    Triangle(0.04,1.2,-0.09,-0.53,1.2,0.09,-0.53,0,0.09),
    Triangle(-0.24,1.98,0.16,-0.24,1.98,-0.22,0.23,1.98,0.16),
    Triangle(-0.24,1.98,-0.22,0.23,1.98,-0.22,0.23,1.98,0.16),
  };
  const int num_triangles = _countof(h_triangles);
  printf("%zu triangles to deal with.\n", num_triangles);

  // Input buffer
  ID3D11Buffer* buf0 = CreateRawBuffer(sizeof(h_triangles));
  ID3D11Buffer* buf0_cpu = CreateRawBufferCPUWriteable(sizeof(h_triangles));
  ID3D11Buffer* buf1 = CreateRawBuffer(sizeof(LeafNode)*num_triangles);
  ID3D11UnorderedAccessView* uav0 = CreateBufferUAV(buf0);
  ID3D11UnorderedAccessView* uav1 = CreateBufferUAV(buf1);
  ID3D11Buffer* buf2 = CreateStructuredBuffer(sizeof(BVHMetaData), 1);
  ID3D11UnorderedAccessView* uav2 = CreateBufferUAV(buf2);

  unsigned zero4[] = { 0,0,0,0 };
  g_context11->ClearUnorderedAccessViewUint(uav2, zero4);

  D3D11_MAPPED_SUBRESOURCE mapped{};
  CE(g_context11->Map(buf0_cpu, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
  memcpy(mapped.pData, (void*)h_triangles, sizeof(h_triangles));
  g_context11->Unmap(buf0_cpu, 0);
  g_context11->CopyResource(buf0, buf0_cpu);

  // Create BVH CB
  ID3D11Buffer* bvh_cb = nullptr;
  D3D11_BUFFER_DESC cb_desc{};
  cb_desc.ByteWidth = RoundUpToAlign(sizeof(BvhCB), 16);
  cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cb_desc.Usage = D3D11_USAGE_DYNAMIC;
  cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  CE(g_device11->CreateBuffer(&cb_desc, nullptr, &bvh_cb));

  const int num_blocks = 2;
  const int num_threads_per_block = 32;

  BvhCB bvhcb{};
  bvhcb.num_threads = num_blocks * num_threads_per_block;
  bvhcb.num_triangles = int(_countof(h_triangles));

  CE(g_context11->Map(bvh_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
  memcpy(mapped.pData, &bvhcb, sizeof(BvhCB));
  g_context11->Unmap(bvh_cb, 0);

  // Shader
  ID3D11ComputeShader* s = BuildComputeShader(L"shaders/bvhstuff.hlsl", "PopulatePrimitiveData");
  g_context11->CSSetShader(s, nullptr, 0);
  g_context11->CSSetConstantBuffers(0, 1, &bvh_cb);
  ID3D11UnorderedAccessView* uavs[] = { uav0, uav1, uav2 };
  g_context11->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
  g_context11->Dispatch(num_blocks, 1, 1);

  // Check # of leaf nodes
  CE(g_context11->Map(buf2, 0, D3D11_MAP_READ, 0, &mapped));
  BVHMetaData metadata;
  memcpy(&metadata, mapped.pData, sizeof(BVHMetaData));
  g_context11->Unmap(buf2, 0);
  metadata.Print();

  // Check leaf nodes
  const int nl = metadata.num_leaf_nodes;
  CE(g_context11->Map(buf1, 0, D3D11_MAP_READ, 0, &mapped));
  std::vector<LeafNode> leaf_nodes(nl);
  memcpy(leaf_nodes.data(), mapped.pData, sizeof(LeafNode) * nl);
  g_context11->Unmap(buf1, 0);
  for (int i = 0; i < nl; i++) {
    leaf_nodes[i].Print();
  }
}