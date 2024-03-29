﻿#ifndef _CHUNK_HPP
#define _CHUNK_HPP

#define GLM_FORCE_RADIANS
#include <gl/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#ifdef WIN32
#include <d3d11.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <DirectXMath.h>
#endif
#undef max
#undef min

struct PerObjectCB {
  DirectX::XMMATRIX M, V, P;
};

// Vertex format: 12 floats per vertex
// X X X Y Y Y Z Z Z NormalIDX Data AO

class Chunk;

// For D3D12
class ChunkPass {
public:
  void AllocateConstantBuffers(int n);
  void StartPass() {
    chunk_per_object_cbs.clear();
    chunk_instances.clear();
  }
  void EndPass();
  void InitD3D12DefaultPalette();
  void InitD3D12SimpleDepth();

  // 这个Pass所用
  ID3D12RootSignature* root_signature_default_palette;
  ID3D12PipelineState* pipeline_state_default_palette;
  ID3D12PipelineState* pipeline_state_depth_only;

  // Per-Object CBs
  int num_max_chunks;
  ID3D12Resource* d_per_object_cbs;

  // 每个Chunk的出现时刻
  std::vector<Chunk*> chunk_instances;
  std::vector<PerObjectCB> chunk_per_object_cbs;
};

class Chunk {
public:
  glm::vec3 pos;
  int idx;
  static int size;
  Chunk();
  Chunk(Chunk& other);
  void LoadDefault();
  static unsigned program;
  void BuildBuffers(Chunk* neighbors[26]);
  void Render();
  void Render(const glm::mat4& M);
#ifdef WIN32
  void Render_D3D11();
  void Render_D3D11(const DirectX::XMMATRIX& M);
  void RecordRenderCommand_D3D12(ChunkPass* pass, const DirectX::XMMATRIX& V, const DirectX::XMMATRIX& P);
  void RecordRenderCommand_D3D12(ChunkPass* pass, const DirectX::XMMATRIX& M, const DirectX::XMMATRIX& V, const DirectX::XMMATRIX& P);
#endif
  void SetVoxel(unsigned x, unsigned y, unsigned z, int v);
  int  GetVoxel(unsigned x, unsigned y, unsigned z);
  void Fill(int vox);
  bool is_dirty;
  unsigned char* block;
  unsigned tri_count;
private:
  unsigned vao, vbo;

#ifdef WIN32
  ID3D11Buffer* d3d11_vertex_buffer;
public:
  ID3D12Resource* d3d12_vertex_buffer;
  D3D12_VERTEX_BUFFER_VIEW d3d12_vertex_buffer_view;
private:
#endif

  static float l0;
  int* light;
  inline int IX(int x, int y, int z) {
    return size*size*x + size*y + z;
  }

  int GetOcclusionFactor(const float x0, const float y0, const float z0,
      const int dir, Chunk* neighs[26]);
};

#endif
