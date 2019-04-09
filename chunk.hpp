#ifndef _CHUNK_HPP
#define _CHUNK_HPP

#include <GL/glew.h>
#include <GL/freeglut.h>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

// Vertex format: 12 floats per vertex
// X X X Y Y Y Z Z Z NormalIDX Data AO

class Chunk {
public:
  glm::vec3 pos;
  static int size;
  Chunk();
  Chunk(Chunk& other);
  void LoadDefault();
  static unsigned program;
  void BuildBuffers(Chunk* neighbors[26]);
  void Render();
  void Render(const glm::mat4& M);
  void SetVoxel(unsigned x, unsigned y, unsigned z, int v);
  int  GetVoxel(unsigned x, unsigned y, unsigned z);
  void Fill(int vox);
  bool is_dirty;
  unsigned char* block;
private:
  unsigned vao, vbo, tri_count;
  static float l0;
  int* light;
  inline int IX(int x, int y, int z) {
    return size*size*x + size*y + z;
  }

  int GetOcclusionFactor(const float x0, const float y0, const float z0,
      const int dir, Chunk* neighs[26]);
};

#endif
