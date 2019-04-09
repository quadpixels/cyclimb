#include "util.hpp"
#include <fstream>

unsigned GetElapsedMillis() {
  return glutGet(GLUT_ELAPSED_TIME);
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