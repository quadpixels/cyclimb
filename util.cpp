#include "util.hpp"
#include <fstream>
#include <Windows.h> // GetTickCount

extern ID3D11Device* g_device11;
extern ID3D11DeviceContext* g_context11;
extern ID3D11VertexShader* g_vs_default_palette, * g_vs_simpletexture;
extern ID3D11PixelShader* g_ps_default_palette, * g_ps_simpletexture;
extern ID3DBlob* g_vs_default_palette_blob;
extern ID3DBlob* g_ps_default_palette_blob;
extern DirectX::XMMATRIX g_projection_d3d11;
extern ID3D11Buffer* g_perobject_cb_default_palette;
extern void UpdateGlobalPerObjectCB(const DirectX::XMMATRIX* M, const DirectX::XMMATRIX* V, const DirectX::XMMATRIX* P);
extern ID3DBlob* g_vs_textrender_blob, * g_ps_textrender_blob;
extern ID3D11BlendState* g_blendstate11;
extern ID3D11Buffer* g_simpletexture_cb;

void GlmMat4ToDirectXMatrix(DirectX::XMMATRIX* out, const glm::mat4& m) {
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      out->r[c].m128_f32[r] = m[c][r];
    }
  }
  //out->r[3].m128_f32[2] *= -1;
}

unsigned GetElapsedMillis() {
  return GetTickCount();
}

void MyCheckGLError(const char* tag) {
  GLenum err = glGetError();
  if (err != GL_NO_ERROR) {
    printf("[%s] OpenGL Error: %d\n", tag, err);
    assert(0);
  }
}

unsigned FullScreenQuad::program = 0;
unsigned FullScreenQuad::program_depth = 0;
unsigned FullScreenQuad::vao     = 0;
unsigned FullScreenQuad::vbo     = 0;

float FullScreenQuad::vertices[] = {
    -1.0f,  1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f,
     1.0f,  1.0f, 0.0f,

     1.0f,  1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f,
     1.0f, -1.0f, 0.0f,
};

FullScreenQuad::FullScreenQuad() { }

void FullScreenQuad::Init(unsigned prog, unsigned prog_depth) {
  if (vao != 0) return;
  program = prog;
  program_depth = prog_depth;

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  {
    glGenBuffers(1, &vbo);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // XYZ Pos
    const size_t stride = 3 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
  glBindVertexArray(0);
  MyCheckGLError("fsquad build buffer");
}

void FullScreenQuad::do_render(unsigned tex) {
  glBindVertexArray(vao);

  glDisable(GL_DEPTH_TEST);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  GLuint texLoc = glGetUniformLocation(program, "tex");
  glUniform1i(texLoc, 0);

  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindVertexArray(0);

  glEnable(GL_DEPTH_TEST);

  MyCheckGLError("render fs quad");
}

void FullScreenQuad::Render(unsigned tex) {
  glUseProgram(program);
  do_render(tex);
  glUseProgram(0);
}

void FullScreenQuad::RenderDepth(unsigned tex) {
  glUseProgram(program_depth);
  do_render(tex);
  glUseProgram(0);
}

float FullScreenQuad::quad_vertices_and_attrib[] = {
  -1.0,  1.0, 0.0, 0.0,
   1.0,  1.0, 1.0, 0.0,
   1.0, -1.0, 1.0, 1.0,

   1.0, -1.0, 1.0, 1.0,
  -1.0, -1.0, 0.0, 1.0,
  -1.0,  1.0, 0.0, 0.0,
};

FullScreenQuad::FullScreenQuad(ID3D11ShaderResourceView* _srv) {
  texture_srv = _srv;
}

void FullScreenQuad::Init_D3D11() {
  // Vertex Buffer
  D3D11_BUFFER_DESC desc = { };
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.ByteWidth = sizeof(quad_vertices_and_attrib);
  desc.StructureByteStride = sizeof(float) * 4;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA srd = { };
  srd.pSysMem = quad_vertices_and_attrib;
  srd.SysMemPitch = sizeof(quad_vertices_and_attrib);
  assert(SUCCEEDED(g_device11->CreateBuffer(&desc, &srd, &d3d11_vertex_buffer)));

  D3D11_INPUT_ELEMENT_DESC inputdesc1[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
  };
  assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc1, 2, g_vs_textrender_blob->GetBufferPointer(),
    g_vs_textrender_blob->GetBufferSize(), &d3d11_input_layout)));


}

void FullScreenQuad::Render_D3D11() {
  g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const unsigned int stride = sizeof(float) * 4;
  const unsigned int offset = 0;
  g_context11->IASetInputLayout(d3d11_input_layout);
  g_context11->IASetVertexBuffers(0, 1, &d3d11_vertex_buffer, &stride, &offset);
  g_context11->VSSetShader(g_vs_simpletexture, nullptr, 0);
  g_context11->PSSetShader(g_ps_simpletexture, nullptr, 0);
  g_context11->PSSetShaderResources(0, 1, &texture_srv);
  g_context11->PSSetConstantBuffers(0, 1, &g_simpletexture_cb);
  float blend_factor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
  g_context11->OMSetBlendState(g_blendstate11, blend_factor, 0xFFFFFFFF);
  g_context11->VSSetConstantBuffers(0, 0, nullptr);
  g_context11->PSSetConstantBuffers(0, 0, nullptr);
  g_context11->Draw(6, 0);
}

ID3D11Buffer* FullScreenQuad::d3d11_vertex_buffer;
ID3D11InputLayout* FullScreenQuad::d3d11_input_layout;

DirectionalLight::DirectionalLight(const glm::vec3& _dir, const glm::vec3& _pos) {
  dir = glm::normalize(_dir); pos = _pos;
  //P = glm::ortho(-200.f, 200.f, -200.f, 200.f, -100.f, 499.f); // for voxelshooter
  P = glm::ortho(-500.f, 500.f, -500.f, 500.f, -500.f, 500.f);

  glm::vec3 tmp(0, 1, 0);
  if (abs(glm::dot(glm::vec3(0, 1, 0), _dir) > 0.9)) {
    tmp = glm::vec3(0.4, 1, 0);
  }

  V = glm::lookAt(pos, pos + dir,
      glm::normalize(
          glm::cross(dir, glm::cross(tmp, dir))));
  is_spotlight_hack = false;
}

DirectionalLight::DirectionalLight(const glm::vec3& _dir, const glm::vec3& _pos, const glm::vec3& up, const float fov) {
  dir = glm::normalize(_dir); pos = _pos;
  P = glm::perspective(fov, 1.0f, 499.0f, 0.1f);
  V = glm::lookAt(pos, pos + dir, glm::normalize(up));
  is_spotlight_hack = true;
  this->fov = fov;
}

DirectX::XMMATRIX DirectionalLight::GetP_D3D11_DXMath() {
  DirectX::XMMATRIX P_D3D11;
  if (!is_spotlight_hack)
    P_D3D11 = DirectX::XMMatrixOrthographicOffCenterLH(-500.0f, 500.0f, -500.0f, 500.0f, -500.0f, 500.0f);
  else
    P_D3D11 = DirectX::XMMatrixPerspectiveFovLH(this->fov, 1.0f, .1f, 4999.0f);
    
  return P_D3D11;
}

DirectX::XMMATRIX DirectionalLight::GetP_D3D11_GLM() {
  //DirectX::XMMATRIX P_D3D11 = DirectX::XMMatrixOrthographicOffCenterLH(-500.0f, 500.0f, -500.0f, 500.0f, -500.0f, 500.0f);
  //return P_D3D11;
  //printf("[GetP_D3D11_GLM] is_spotlight_hack=%d\n", is_spotlight_hack);
  glm::mat4 P;
  if (!is_spotlight_hack) P = glm::ortho(-500.0f, 500.0f, -500.0f, 500.0f, 500.0f, -500.0f);
  else
    P = glm::perspective(this->fov, 1.0f, .1f, 4999.f);

  DirectX::XMMATRIX ret;
  GlmMat4ToDirectXMatrix(&ret, P);
  return ret;
}

DirectX::XMMATRIX DirectionalLight::GetV_D3D11() {
  using namespace DirectX; // For operator+
  XMVECTOR dir1;
  dir1.m128_f32[0] = dir.x; dir1.m128_f32[1] = dir.y; dir1.m128_f32[2] = -dir.z;
  XMVECTOR pos1;
  pos1.m128_f32[0] = pos.x; pos1.m128_f32[1] = pos.y; pos1.m128_f32[2] = -pos.z;
  XMVECTOR focus1 = dir1 + pos1;

  XMVECTOR posy1;
  posy1.m128_f32[0] = 0; posy1.m128_f32[1] = 1; posy1.m128_f32[2] = 0;

  if (dir.x==0 && dir.z==0) {
    posy1.m128_f32[0] = 0.707;
    posy1.m128_f32[1] = 0.707;
  }

  XMVECTOR up1 = XMVector3Cross(dir1, XMVector3Cross(posy1, dir1));
  
  XMMATRIX V_D3D11 = DirectX::XMMatrixLookAtLH(pos1, focus1, up1);
  return V_D3D11;
}

DirectX::XMMATRIX DirectionalLight::GetPV_D3D11() {
  using namespace DirectX;
  return GetV_D3D11() * GetP_D3D11_GLM(); // Association order ????
}

DirectX::XMVECTOR DirectionalLight::GetDir_D3D11() {
  DirectX::XMVECTOR dir1;
  dir1.m128_f32[0] = dir.x;
  dir1.m128_f32[1] = dir.y;
  dir1.m128_f32[2] = -dir.z;
  return dir1;
}

void PrintMat4(const glm::mat4& m, const char* tag) {
  printf("%s =\n", tag);
  for (int i=0; i<4; i++)
    printf("%5g, %5g, %5g, %5g\n", m[i][0], m[i][1], m[i][2], m[i][3]);
}

void FullScreenQuad::RenderWithBlend(unsigned tex) {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  Render(tex);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glDisable(GL_BLEND);
  MyCheckGLError("renderwithblend");
}

std::vector<std::string> ReadLinesFromFile(const char* fn) {
  std::vector<std::string> ret;
  std::ifstream ifs(fn);
  std::string line;
  if (ifs.good()) {
    while (std::getline(ifs, line)) {
      ret.push_back(line);
    }
  }
  return ret;
}

std::vector<std::string> SplitStringBySpace(std::string x) {
  int idx0 = -1, idx1 = 0;
  std::vector<std::string> ret;
  while (idx1 < int(x.size())) {
    if (x[idx1] != ' ') {
      idx0 = idx1;
      while (idx1 + 1< int(x.size()) && x[idx1+1] != ' ') idx1 ++;
      ret.push_back(x.substr(idx0, idx1-idx0+1));
      idx1++;
    } else idx1++;
  }
  return ret;
}