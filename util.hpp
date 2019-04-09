#ifndef _UTIL_HPP
#define _UTIL_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdlib.h>
#include <stdio.h>
#include <GL/glew.h>
#include <GL/freeglut.h>
#include <vector>
#include <string>

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
private:
  static GLuint vao, vbo;
  static float  vertices[3*6];
  void do_render(GLuint tex);
};

class DirectionalLight {
public:
  glm::mat4 P, V;
  glm::vec3 dir, pos;
  DirectionalLight(const glm::vec3& _dir, const glm::vec3& _pos);
};

unsigned GetElapsedMillis();
std::vector<std::string> ReadLinesFromFile(const char* fn);
std::vector<std::string> SplitStringBySpace(std::string x);

#endif
