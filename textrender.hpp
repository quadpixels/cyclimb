#pragma once

#include <map>
#include <string>
#include <vector>
#include <gl/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#ifdef WIN32
#include <d3d11.h>
#include <d3d12.h>
#endif
#include "util.hpp"

struct Character {
	GLuint textureID;
	glm::ivec2 size, bearing;
	GLuint advance;
};

#ifdef WIN32
struct TextCbPerScene {
  DirectX::XMVECTOR screensize; // Assume alignment at float4 boundary
  DirectX::XMMATRIX transform;
  DirectX::XMMATRIX projection;
  DirectX::XMVECTOR textcolor;
};

struct Character_D3D11 {
  ID3D11Texture2D *texture;
  ID3D11ShaderResourceView *srv;
  glm::ivec2 size, bearing;
  GLuint advance;
};

struct Character_D3D12 {
  ID3D12Resource* texture;
  int offset_in_srv_heap;  // Offset in the number of descriptors
  glm::ivec2 size, bearing;
  uint32_t advance;
};

class TextPass {
public:
  TextPass(ID3D12Device* d, ID3D12CommandQueue* cq, ID3D12GraphicsCommandList* cl, ID3D12CommandAllocator* ca) :
    device12(d), command_queue(cq), command_list(cl), command_allocator(ca) {}
  struct CharacterToDisplay {
    Character_D3D12* character;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    int per_scene_cb_index;
  };

  void AllocateConstantBuffers(int n);
  void StartPass() {
    num_per_scene_cbs = 0;
    characters_to_display.clear();
  }
  void InitFreetype();
  void InitD3D12();
  Character_D3D12* CreateOrGetChar(wchar_t ch);
  void AddText(std::wstring text, float x, float y, float scale, glm::vec3 color, glm::mat4 transform);
  void RenderText(ID3D12GraphicsCommandList* command_list);

  ID3D12RootSignature* root_signature;
  ID3D12PipelineState* pipeline_state;
  std::vector<CharacterToDisplay> characters_to_display;

  int num_max_chars;

  int num_per_scene_cbs;
  glm::vec3 last_color;
  glm::mat4 last_transform;


  // CBs
  ID3D12Resource* per_scene_cbs;
  ID3D12DescriptorHeap* srv_heap;
 
  // VBs
  ID3D12Resource* vertex_buffers;

  std::map<wchar_t, Character_D3D12> characters_d3d12;
  
  FT_Face face;

  int srv_descriptor_size;
private:
  ID3D12Device* device12;
  ID3D12CommandQueue* command_queue;
  ID3D12GraphicsCommandList* command_list;
  ID3D12CommandAllocator* command_allocator;
};
#endif

void InitTextRender();
#ifdef WIN32
void InitTextRender_D3D11();
#endif
void RenderText(GraphicsAPI api, std::wstring text, GLfloat x, GLfloat y, GLfloat scale, glm::vec3 color, glm::mat4 transform);
void MeasureTextWidth(std::wstring text, float *w);
