#include "testshapes.hpp"
#include "camera.hpp"
#include <DirectXMath.h>

// Shapes for testing purposes

unsigned Triangle::program = 0;
GLuint Triangle::vao = 0;
GLuint Triangle::vbo = 0;
ID3D11Buffer* Triangle::d3d11buffer = nullptr;
ID3D11InputLayout* Triangle::d3d11_input_layout = nullptr;
extern glm::mat4 g_projection;
extern Camera* GetCurrentSceneCamera();

extern ID3D11Device* g_device11;
extern ID3D11DeviceContext *g_context11;
extern ID3D11VertexShader* g_vs_default_palette;
extern ID3D11PixelShader* g_ps_default_palette;
extern ID3DBlob *g_vs_default_palette_blob;
extern ID3DBlob *g_ps_default_palette_blob;
extern DirectX::XMMATRIX g_projection_d3d11;
extern ID3D11Buffer* g_perobject_cb_default_palette;
extern void UpdateGlobalPerObjectCB(const DirectX::XMMATRIX* M, const DirectX::XMMATRIX* V, const DirectX::XMMATRIX* P);

float Triangle::base_vertices_and_attrib_ccw[] = {
  0.0f, 0.0f, 0.0f, 0.0f, 128.0f, 0.0f,
  9.0f, 0.0f, 0.0f, 0.0f, 240.0f, 0.0f,
  0.0f, 9.0f, 0.0f, 0.0f, 87.0f,  0.0f,
};

float Triangle::base_vertices_and_attrib_cw[] = {
  0.0f, 0.0f, 0.0f, 128.0f,
  0.0f, 9.0f, 0.0f, 87.0f,
  9.0f, 0.0f, 0.0f, 240.0f,
};

struct DefaultPalettePerObjectCB {
  DirectX::XMMATRIX M, V, P;
};


void Triangle::Init(unsigned prog) {
  if (vao != 0) return;
  program = prog;
  glGenVertexArrays(1, &vao);

  glBindVertexArray(vao);
  {
    glGenBuffers(1, &vbo);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(base_vertices_and_attrib_ccw), base_vertices_and_attrib_ccw, GL_STATIC_DRAW);

    // XYZ Pos
    const size_t stride = 6 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);

    // Normal Idx
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3*sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    // Data
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(4*sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    // AO Index
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(5*sizeof(GLfloat)));
    glEnableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
  glBindVertexArray(0);
}

void Triangle::Init_D3D11() {
  D3D11_BUFFER_DESC desc = { };
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.ByteWidth = sizeof(base_vertices_and_attrib_cw);
  desc.StructureByteStride = sizeof(float) * 4;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA srd = { };
  srd.pSysMem = base_vertices_and_attrib_cw;
  srd.SysMemPitch = sizeof(base_vertices_and_attrib_cw);
  assert(SUCCEEDED(g_device11->CreateBuffer(&desc, &srd, &d3d11buffer)));

  D3D11_INPUT_ELEMENT_DESC inputdesc1[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 1, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // COLOR1
    { "COLOR"   , 2, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // COLOR2
  };
  assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc1, 4, g_vs_default_palette_blob->GetBufferPointer(),
    g_vs_default_palette_blob->GetBufferSize(), &d3d11_input_layout)));
}

Triangle::Triangle() {
}

void Triangle::Render() {
  glUseProgram(program);
  GLuint mLoc = glGetUniformLocation(program, "M");
  GLuint vLoc = glGetUniformLocation(program, "V");
  GLuint pLoc = glGetUniformLocation(program, "P");
  glm::mat4 M(1), V = GetCurrentSceneCamera()->GetViewMatrix(), P = g_projection;
  M = glm::translate(M, pos);
  glUniformMatrix4fv(mLoc, 1, GL_FALSE, &(M[0][0]));
  glUniformMatrix4fv(vLoc, 1, GL_FALSE, &(V[0][0]));
  glUniformMatrix4fv(pLoc, 1, GL_FALSE, &(P[0][0]));
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glUseProgram(0);
}

void Triangle::Render_D3D11() {
  g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const UINT stride = sizeof(float)*4, zero = 0; // Why do I need to specify the stride here again
  g_context11->IASetVertexBuffers(0, 1, &d3d11buffer, &stride, &zero);
  g_context11->IASetInputLayout(d3d11_input_layout);

  DirectX::XMVECTOR pos_d3d;
  pos_d3d.m128_f32[0] = pos.x;
  pos_d3d.m128_f32[1] = pos.y;
  pos_d3d.m128_f32[2] = -pos.z;
  DirectX::XMMATRIX M = DirectX::XMMatrixTranslationFromVector(pos_d3d);
  UpdateGlobalPerObjectCB(&M, nullptr, nullptr);

  g_context11->Draw(3, 0);
}

//

unsigned ColorCube::program = 0;
GLuint   ColorCube::vao = 0, ColorCube::vbo = 0;
ID3D11InputLayout* ColorCube::d3d11_input_layout = nullptr;
ID3D11Buffer* ColorCube::d3d11_vertex_buffer = nullptr;
float ColorCube::base_vertices_and_attrib[] = {
  -0.5f, -0.5f, -0.5f,  8.0f, 0.0f, 0.0f,
   0.5f,  0.5f, -0.5f,  8.0f, 0.0f, 0.0f,
   0.5f, -0.5f, -0.5f,  8.0f, 0.0f, 0.0f,
   0.5f,  0.5f, -0.5f,  8.0f, 0.0f, 0.0f,
  -0.5f, -0.5f, -0.5f,  8.0f, 0.0f, 0.0f,
  -0.5f,  0.5f, -0.5f,  8.0f, 0.0f, 0.0f,

  -0.5f, -0.5f,  0.5f,  19.0f, 0.0f, 0.0f,
   0.5f, -0.5f,  0.5f,  19.0f, 0.0f, 0.0f,
   0.5f,  0.5f,  0.5f,  19.0f, 0.0f, 0.0f,
   0.5f,  0.5f,  0.5f,  19.0f, 0.0f, 0.0f,
  -0.5f,  0.5f,  0.5f,  19.0f, 0.0f, 0.0f,
  -0.5f, -0.5f,  0.5f,  19.0f, 0.0f, 0.0f,

  -0.5f,  0.5f,  0.5f,  25.0f, 0.0f, 0.0f,
  -0.5f,  0.5f, -0.5f,  25.0f, 0.0f, 0.0f,
  -0.5f, -0.5f, -0.5f,  25.0f, 0.0f, 0.0f,
  -0.5f, -0.5f, -0.5f,  25.0f, 0.0f, 0.0f,
  -0.5f, -0.5f,  0.5f,  25.0f, 0.0f, 0.0f,
  -0.5f,  0.5f,  0.5f,  25.0f, 0.0f, 0.0f,

   0.5f,  0.5f,  0.5f,  88.0f, 0.0f, 0.0f,
   0.5f, -0.5f, -0.5f,  88.0f, 0.0f, 0.0f,
   0.5f,  0.5f, -0.5f,  88.0f, 0.0f, 0.0f,
   0.5f, -0.5f, -0.5f,  88.0f, 0.0f, 0.0f,
   0.5f,  0.5f,  0.5f,  88.0f, 0.0f, 0.0f,
   0.5f, -0.5f,  0.5f,  88.0f, 0.0f, 0.0f,

  -0.5f, -0.5f, -0.5f,  127.0f, 0.0f, 0.0f,
   0.5f, -0.5f, -0.5f,  127.0f, 0.0f, 0.0f,
   0.5f, -0.5f,  0.5f,  127.0f, 0.0f, 0.0f,
   0.5f, -0.5f,  0.5f,  127.0f, 0.0f, 0.0f,
  -0.5f, -0.5f,  0.5f,  127.0f, 0.0f, 0.0f,
  -0.5f, -0.5f, -0.5f,  127.0f, 0.0f, 0.0f,

  -0.5f,  0.5f, -0.5f,  189.0f, 0.0f, 0.0f,
   0.5f,  0.5f,  0.5f,  189.0f, 0.0f, 0.0f,
   0.5f,  0.5f, -0.5f,  189.0f, 0.0f, 0.0f,
   0.5f,  0.5f,  0.5f,  189.0f, 0.0f, 0.0f,
  -0.5f,  0.5f, -0.5f,  189.0f, 0.0f, 0.0f,
  -0.5f,  0.5f,  0.5f,  189.0f, 0.0f, 0.0f,
};

float ColorCube::base_vertices_and_attrib_cw[] = {
  -0.5f, -0.5f,  0.5f, 0.0f, 8.0f, 0.0f,
   0.5f, -0.5f,  0.5f, 0.0f, 8.0f, 0.0f,
   0.5f,  0.5f,  0.5f, 0.0f, 8.0f, 0.0f,
   0.5f,  0.5f,  0.5f, 0.0f, 8.0f, 0.0f,
  -0.5f,  0.5f,  0.5f, 0.0f, 8.0f, 0.0f,
  -0.5f, -0.5f,  0.5f, 0.0f, 8.0f, 0.0f,

  -0.5f, -0.5f, -0.5f, 0.0f, 19.0f, 0.0f,
   0.5f,  0.5f, -0.5f, 0.0f, 19.0f, 0.0f,
   0.5f, -0.5f, -0.5f, 0.0f, 19.0f, 0.0f,
   0.5f,  0.5f, -0.5f, 0.0f, 19.0f, 0.0f,
  -0.5f, -0.5f, -0.5f, 0.0f, 19.0f, 0.0f,
  -0.5f,  0.5f, -0.5f, 0.0f, 19.0f, 0.0f,

  -0.5f,  0.5f, -0.5f, 0.0f, 25.0f, 0.0f,
  -0.5f, -0.5f,  0.5f, 0.0f, 25.0f, 0.0f,
  -0.5f,  0.5f,  0.5f, 0.0f, 25.0f, 0.0f,
  -0.5f, -0.5f,  0.5f, 0.0f, 25.0f, 0.0f,
  -0.5f,  0.5f, -0.5f, 0.0f, 25.0f, 0.0f,
  -0.5f, -0.5f, -0.5f, 0.0f, 25.0f, 0.0f,

   0.5f,  0.5f, -0.5f, 0.0f, 88.0f, 0.0f,
   0.5f,  0.5f,  0.5f, 0.0f, 88.0f, 0.0f,
   0.5f, -0.5f,  0.5f, 0.0f, 88.0f, 0.0f,
   0.5f, -0.5f,  0.5f, 0.0f, 88.0f, 0.0f,
   0.5f, -0.5f, -0.5f, 0.0f, 88.0f, 0.0f,
   0.5f,  0.5f, -0.5f, 0.0f, 88.0f, 0.0f,

  -0.5f, -0.5f,  0.5f, 0.0f, 127.0f, 0.0f,
   0.5f, -0.5f, -0.5f, 0.0f, 127.0f, 0.0f,
   0.5f, -0.5f,  0.5f, 0.0f, 127.0f, 0.0f,
   0.5f, -0.5f, -0.5f, 0.0f, 127.0f, 0.0f,
  -0.5f, -0.5f,  0.5f, 0.0f, 127.0f, 0.0f,
  -0.5f, -0.5f, -0.5f, 0.0f, 127.0f, 0.0f,

  -0.5f,  0.5f,  0.5f, 0.0f, 189.0f, 0.0f,
   0.5f,  0.5f,  0.5f, 0.0f, 189.0f, 0.0f,
   0.5f,  0.5f, -0.5f, 0.0f, 189.0f, 0.0f,
   0.5f,  0.5f, -0.5f, 0.0f, 189.0f, 0.0f,
  -0.5f,  0.5f, -0.5f, 0.0f, 189.0f, 0.0f,
  -0.5f,  0.5f,  0.5f, 0.0f, 189.0f, 0.0f,
};
ColorCube::ColorCube() {

}
void ColorCube::Init(unsigned prog) {
  if (vao != 0) return;
  program = prog;
  glGenVertexArrays(1, &vao);

  glBindVertexArray(vao);
  {
    glGenBuffers(1, &vbo);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(base_vertices_and_attrib), base_vertices_and_attrib, GL_STATIC_DRAW);

    // XYZ Pos
    const size_t stride = 6 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);

    // Normal Idx
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    // Data
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(4 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    // AO Index
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(5 * sizeof(GLfloat)));
    glEnableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
  glBindVertexArray(0);
}

void ColorCube::Init_D3D11() {
  // Vertex Buffer
  D3D11_BUFFER_DESC desc = { };
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.ByteWidth = sizeof(base_vertices_and_attrib_cw);
  desc.StructureByteStride = sizeof(float) * 6;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA srd = { };
  srd.pSysMem = base_vertices_and_attrib_cw;
  srd.SysMemPitch = sizeof(base_vertices_and_attrib_cw);
  assert(SUCCEEDED(g_device11->CreateBuffer(&desc, &srd, &d3d11_vertex_buffer)));

  D3D11_INPUT_ELEMENT_DESC inputdesc1[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 1, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 2, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
  };
  assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc1, 4, g_vs_default_palette_blob->GetBufferPointer(),
    g_vs_default_palette_blob->GetBufferSize(), &d3d11_input_layout)));
}

void ColorCube::Render() {
  glUseProgram(program);
  GLuint mLoc = glGetUniformLocation(program, "M");
  GLuint vLoc = glGetUniformLocation(program, "V");
  GLuint pLoc = glGetUniformLocation(program, "P");
  glm::mat4 M(1), V = GetCurrentSceneCamera()->GetViewMatrix(), P = g_projection;
  M = glm::translate(M, pos);
  glUniformMatrix4fv(mLoc, 1, GL_FALSE, &(M[0][0]));
  glUniformMatrix4fv(vLoc, 1, GL_FALSE, &(V[0][0]));
  glUniformMatrix4fv(pLoc, 1, GL_FALSE, &(P[0][0]));
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindVertexArray(0);
  glUseProgram(0);
}

void ColorCube::Render_D3D11() {
  g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const UINT stride = sizeof(float) * 6, zero = 0; // I need to specify the stride here again
  g_context11->IASetVertexBuffers(0, 1, &d3d11_vertex_buffer, &stride, &zero);
  g_context11->IASetInputLayout(d3d11_input_layout);
  //g_context11->VSSetShader(g_vs_default_palette, nullptr, 0);
  //g_context11->PSSetShader(g_ps_default_palette, nullptr, 0);

  DirectX::XMVECTOR pos_d3d;
  pos_d3d.m128_f32[0] = pos.x;
  pos_d3d.m128_f32[1] = pos.y;
  pos_d3d.m128_f32[2] = -pos.z;
  DirectX::XMMATRIX M = DirectX::XMMatrixTranslationFromVector(pos_d3d);
  UpdateGlobalPerObjectCB(&M, nullptr, nullptr);

  g_context11->VSSetConstantBuffers(0, 1, &g_perobject_cb_default_palette);
  g_context11->Draw(36, 0);
}