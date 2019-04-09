#ifndef TESTSHAPES_H
#define TESTSHAPES_H
#include <GL/glew.h>
#include <GL/freeglut.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "shader.hpp"

class Triangle {
public:
  Triangle();
  static void Init(unsigned);
  void Render();
private:
  static GLuint vao, vbo;
  static float  base_vertices_and_attrib[3*4];
  static unsigned program;
public:
  glm::vec3 pos;
};

class ColorCube {
public:
  ColorCube();
  static void Init(unsigned);
  void Render();
private:
  static GLuint vao, vbo;
  static float  base_vertices_and_attrib[36*4];
  static unsigned program;
public:
  glm::vec3 pos;
};

#endif
