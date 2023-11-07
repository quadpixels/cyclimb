#ifndef _SCENE_HPP
#define _SCENE_HPP

#include <map>
#include <string>
#include <vector>

#include <d3d12.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include "d3dx12.h"
#include <glm/glm.hpp>
#include <wrl/client.h>
#include <dxgi1_4.h>

#include "chunk.hpp"
#include "chunkindex.hpp"
#include "camera.hpp"
#include "sprite.hpp"
#include "util.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

class Scene {
public:
  virtual void Render() = 0;
  virtual void Update(float secs) = 0;
};

class DX12ClearScreenScene : public Scene {
public:
  DX12ClearScreenScene();
  void Render() override;
  void Update(float secs) override;
private:
  void InitPipelineAndCommandList();
  ID3D12CommandAllocator* command_allocator;
  ID3D12GraphicsCommandList* command_list;
  ID3DBlob* VS, * PS;

  ID3D12RootSignature* root_signature;
  ID3D12PipelineState* pipeline_state;
};

class DX12HelloTriangleScene : public Scene {
public:
  struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
  };
  DX12HelloTriangleScene();
  void Render() override;
  void Update(float secs) override;
  static constexpr int FRAME_COUNT = 2;
private:
  void InitPipelineAndCommandList();
  void InitResources();
  ID3D12Resource* vertex_buffer;
  ID3D12CommandAllocator* command_allocator;
  ID3D12GraphicsCommandList* command_list;
  ID3DBlob* VS, *PS;
  ID3D12RootSignature* root_signature;
  ID3D12PipelineState* pipeline_state;
  D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;

  ID3D12DescriptorHeap* cbv_heap;
  int cbv_descriptor_size;
  ID3D12Resource* cbvs;  // CBV resource, N copies of the CBV for N triangles.
};

struct PerSceneCB {
  DirectX::XMVECTOR dir_light;
  DirectX::XMMATRIX lightPV;
  DirectX::XMVECTOR cam_pos;
};

class DX12ChunksScene : public Scene {
public:
  DX12ChunksScene();
  void Render() override;
  void Update(float secs) override;
  static constexpr int FRAME_COUNT = 2;
private:
  void InitCommandList();
  void InitResources();
  ID3D12CommandAllocator* command_allocator;
  ID3D12GraphicsCommandList* command_list;

  Chunk* chunk;
  ChunkIndex* chunk_index;
  ChunkPass *chunk_pass_depth, *chunk_pass_normal;
  ChunkSprite* chunk_sprite;

  float total_secs;

  // CB's heap, resource, view and descriptor
  ID3D12DescriptorHeap* cbv_heap;
  PerSceneCB h_per_scene_cb;
  ID3D12Resource* d_per_scene_cb;
  int cbv_descriptor_size;
  Camera* camera;
  DirectionalLight* dir_light;
  void UpdatePerSceneCB(const DirectX::XMVECTOR* dir_light, const DirectX::XMMATRIX* lightPV, const DirectX::XMVECTOR* camPos);
  DirectX::XMMATRIX projection_matrix;

  // DSV's heap
  ID3D12DescriptorHeap* dsv_heap;
  int dsv_descriptor_size;
  ID3D12Resource* depth_buffer;

  // Rendertarget
  ID3D12Resource* gbuffer;
  ID3D12DescriptorHeap* rtv_heap;  // For GBuffer

  // Empty shadow map
  ID3D12Resource* shadow_map;

  // Backdrop
  ID3D12Resource* backdrop_vert_buf;
  D3D12_VERTEX_BUFFER_VIEW backdrop_vbv;
};

class DX12TextScene : public Scene {
public:
  DX12TextScene();
  void Render() override;
  void Update(float secs) override;
  static constexpr int FRAME_COUNT = 2;
  void AddText(const std::wstring& t, glm::vec2 pos);

  // Texture atlas 中的Character
  struct Character_D3D12 {
    ID3D12Resource* texture;
    int offset_in_srv_heap;  // Offset in the number of descriptors
    glm::ivec2 size, bearing;
    uint32_t advance;
  };
  // 要显示的Character
  struct CharacterToDisplay {
    Character_D3D12* character;
    D3D12_VERTEX_BUFFER_VIEW vbv;
  };
private:
  void InitCommandList();
  void InitResources();
  void InitFreetype();
  void ClearCharactersToDisplay();
  Character_D3D12* CreateOrGetChar(wchar_t ch);
  ID3D12CommandAllocator* command_allocator;
  ID3D12GraphicsCommandList* command_list;
  ID3DBlob* VS, * PS;
  ID3D12RootSignature* root_signature_text_render;
  ID3D12PipelineState* pipeline_state_text_render;

  std::vector<CharacterToDisplay> characters_to_display;
  std::wstring text_to_display;
  glm::vec2 text_pos;

  std::vector<ID3D12Resource*> constant_buffers;
  ID3D12DescriptorHeap* cbv_heap;

  std::map<wchar_t, Character_D3D12> characters_d3d12;
  ID3D12Resource* vertex_buffers;
  ID3D12DescriptorHeap* srv_heap;
  int srv_descriptor_size;
  FT_Face face;
};

#endif