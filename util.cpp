#include "util.hpp"
#include <fstream>
#include <Windows.h> // GetTickCount

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

DirectionalLight::DirectionalLight(const glm::vec3& _dir, const glm::vec3& _pos) {
  dir = glm::normalize(_dir); pos = _pos;
  //P = glm::ortho(-200.f, 200.f, -200.f, 200.f, -100.f, 499.f); // for voxelshooter
  P = glm::ortho(-500.f, 500.f, -500.f, 500.f, -500.f, 500.f);
  V = glm::lookAt(pos, pos + dir,
      glm::normalize(
          glm::cross(dir, glm::cross(glm::vec3(0.f,1.f,0.f), dir))));
}

DirectX::XMMATRIX DirectionalLight::GetP_D3D11_DXMath() {
  DirectX::XMMATRIX P_D3D11 = DirectX::XMMatrixOrthographicOffCenterLH(-500.0f, 500.0f, -500.0f, 500.0f, -500.0f, 500.0f);
  return P_D3D11;
}

DirectX::XMMATRIX DirectionalLight::GetP_D3D11_GLM() {
  //DirectX::XMMATRIX P_D3D11 = DirectX::XMMatrixOrthographicOffCenterLH(-500.0f, 500.0f, -500.0f, 500.0f, -500.0f, 500.0f);
  //return P_D3D11;
  glm::mat4 P = glm::ortho(-500.0f, 500.0f, -500.0f, 500.0f, 500.0f, -500.0f);
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