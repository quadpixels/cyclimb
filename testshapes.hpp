#ifndef TESTSHAPES_H
#define TESTSHAPES_H
#include <gl/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "shader.hpp"

#include <d3d11.h>
#undef min
#undef max

class Triangle {
public:
  Triangle();
  static void Init(unsigned);
  static void Init_D3D11();
  void Render();
  void Render_D3D11();
private:
  static GLuint vao, vbo;
  static float  base_vertices_and_attrib_ccw[3 * 6];
  static float  base_vertices_and_attrib_cw[3 * 4];
  static unsigned program;

  static ID3D11Buffer* d3d11buffer;
  static ID3D11InputLayout* d3d11_input_layout;
public:
  glm::vec3 pos;
};

class ColorCube {
public:
  ColorCube();
  static void Init(unsigned);
  static void Init_D3D11();
  void Render();
  void Render_D3D11();
private:
  static GLuint vao, vbo;
  static float  base_vertices_and_attrib[36*6];
  static float base_vertices_and_attrib_cw[36 * 6];
  static unsigned program;

  static ID3D11Buffer* d3d11_vertex_buffer;
  static ID3D11InputLayout* d3d11_input_layout;
public:
  glm::vec3 pos;
};

#endif
