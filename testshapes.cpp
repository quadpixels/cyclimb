#include "testshapes.hpp"

// Shapes for testing purposes

unsigned Triangle::program = 0;
GLuint Triangle::vao = 0;
GLuint Triangle::vbo = 0;

float Triangle::base_vertices_and_attrib[] = {
  0.0f, 0.0f, 0.0f, 128.0f,
  9.0f, 0.0f, 0.0f, 240.0f,
  0.0f, 9.0f, 0.0f, 87.0f,
};

void Triangle::Init(unsigned prog) {
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

Triangle::Triangle() {
}

void Triangle::Render() {
  glUseProgram(program);
  GLuint mLoc = glGetUniformLocation(program, "M");
  glm::mat4 M(1);
  M = glm::translate(M, pos);
  glUniformMatrix4fv(mLoc, 1, GL_FALSE, &(M[0][0]));
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glUseProgram(0);
}

//

unsigned ColorCube::program = 0;
GLuint   ColorCube::vao = 0, ColorCube::vbo = 0;
float ColorCube::base_vertices_and_attrib[] = {
  -0.5f, -0.5f, -0.5f,  8.0f,
   0.5f,  0.5f, -0.5f,  8.0f,
   0.5f, -0.5f, -0.5f,  8.0f,
   0.5f,  0.5f, -0.5f,  8.0f,
  -0.5f, -0.5f, -0.5f,  8.0f,
  -0.5f,  0.5f, -0.5f,  8.0f,

  -0.5f, -0.5f,  0.5f,  19.0f,
   0.5f, -0.5f,  0.5f,  19.0f,
   0.5f,  0.5f,  0.5f,  19.0f,
   0.5f,  0.5f,  0.5f,  19.0f,
  -0.5f,  0.5f,  0.5f,  19.0f,
  -0.5f, -0.5f,  0.5f,  19.0f,

  -0.5f,  0.5f,  0.5f,  25.0f,
  -0.5f,  0.5f, -0.5f,  25.0f,
  -0.5f, -0.5f, -0.5f,  25.0f,
  -0.5f, -0.5f, -0.5f,  25.0f,
  -0.5f, -0.5f,  0.5f,  25.0f,
  -0.5f,  0.5f,  0.5f,  25.0f,

   0.5f,  0.5f,  0.5f,  88.0f,
   0.5f, -0.5f, -0.5f,  88.0f,
   0.5f,  0.5f, -0.5f,  88.0f,
   0.5f, -0.5f, -0.5f,  88.0f,
   0.5f,  0.5f,  0.5f,  88.0f,
   0.5f, -0.5f,  0.5f,  88.0f,

  -0.5f, -0.5f, -0.5f,  127.0f,
   0.5f, -0.5f, -0.5f,  127.0f,
   0.5f, -0.5f,  0.5f,  127.0f,
   0.5f, -0.5f,  0.5f,  127.0f,
  -0.5f, -0.5f,  0.5f,  127.0f,
  -0.5f, -0.5f, -0.5f,  127.0f,

  -0.5f,  0.5f, -0.5f,  189.0f,
   0.5f,  0.5f,  0.5f,  189.0f,
   0.5f,  0.5f, -0.5f,  189.0f,
   0.5f,  0.5f,  0.5f,  189.0f,
  -0.5f,  0.5f, -0.5f,  189.0f,
  -0.5f,  0.5f,  0.5f,  189.0f,
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

    const size_t stride = 4 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3*sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
  glBindVertexArray(0);
}
void ColorCube::Render() {
  glUseProgram(program);
  GLuint mLoc = glGetUniformLocation(program, "M");
  glm::mat4 M(1);
  M = glm::translate(M, pos);
  glUniformMatrix4fv(mLoc, 1, GL_FALSE, &(M[0][0]));
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindVertexArray(0);
  glUseProgram(0);
}
