#ifndef _UTIL_HPP
#define _UTIL_HPP

#include <gl/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdlib.h>
#include <stdio.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include "shader.hpp"
#include <unordered_map>

#include <d3d11.h>
#undef min
#undef max
#include <DirectXMath.h>

enum GraphicsAPI {
  ClimbOpenGL,
  ClimbD3D11,
};

struct DefaultPalettePerSceneCB {
  DirectX::XMVECTOR dir_light;
  DirectX::XMMATRIX lightPV;
  DirectX::XMVECTOR cam_pos;
  //int spotlightCount;
  //DirectX::XMMATRIX spotlightPV[16]; // Projection-View matrix
  //DirectX::XMVECTOR spotlightColors[16];
};

struct VolumetricLightCB {
  int spotlightCount;
  int forceAlwaysOn;
  float aspect, fovy;
  DirectX::XMVECTOR cam_pos;
  DirectX::XMMATRIX spotlightPV[16]; // Projection-View matrix
  DirectX::XMVECTOR spotlightColors[16];
};

void MyCheckGLError(const char* tag);
void PrintMat4(const glm::mat4&, const char*);

class FullScreenQuad {
public:
  FullScreenQuad();
  void Render(GLuint texture);
  void RenderDepth(GLuint texture);
  void RenderWithBlend(GLuint texture);
  static void Init(unsigned, unsigned);
  static unsigned program, program_depth;

  FullScreenQuad(ID3D11ShaderResourceView* _srv);
  void Render_D3D11();

  static void Init_D3D11();

  static ID3D11Buffer* d3d11_vertex_buffer;
  static ID3D11InputLayout* d3d11_input_layout;
  static float quad_vertices_and_attrib[4 * 6];

private:
  static GLuint vao, vbo;
  static float  vertices[3*6];
  void do_render(GLuint tex);

  ID3D11ShaderResourceView* texture_srv;
};

// Not very efficient -- updated every draw call every frame
class ImageSprite2D {
public:
  static ID3D11Buffer* d3d11_vertex_buffer, *d3d11_vertex_buffer_staging;
  static void Init_D3D11();
  ImageSprite2D(ID3D11ShaderResourceView* img_srv, RECT src_rect);
  ImageSprite2D(ID3D11ShaderResourceView* img_srv, RECT src_rect, glm::vec2 pos, glm::vec2 hext);
  glm::vec2 dest_pos, dest_hext; // Image space pos and half extent
  RECT src_rect;
  void Render_D3D11();
private:
  void init_srv(ID3D11ShaderResourceView* img_srv, RECT src_rect);
  ID3D11ShaderResourceView* tex_srv, *vert_buffer_srv;
  static std::unordered_map<ID3D11ShaderResourceView*, std::pair<int, int> > tex_dims;
  static ID3D11InputLayout* d3d11_input_layout;
  static float quad_vertices_and_attrib[24];
};

// Added hack: ratio=1
class DirectionalLight {
public:
  glm::mat4 P, V;
  glm::vec3 dir, pos;
  float fov;

  bool is_spotlight_hack;

  DirectX::XMMATRIX GetP_D3D11_DXMath(); // The first flavor
  DirectX::XMMATRIX GetP_D3D11_GLM();    // The second flavor
  DirectX::XMMATRIX GetV_D3D11();
  DirectX::XMVECTOR GetDir_D3D11();
  DirectX::XMMATRIX GetPV_D3D11();

  DirectionalLight(const glm::vec3& _dir, const glm::vec3& _pos);
  DirectionalLight(const glm::vec3& _dir, const glm::vec3& _pos, const glm::vec3& up, const float fov);
};

unsigned GetElapsedMillis();
std::vector<std::string> ReadLinesFromFile(const char* fn);
std::vector<std::string> SplitStringBySpace(std::string x);

#endif
