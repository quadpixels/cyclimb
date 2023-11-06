#ifndef CHUNKINDEX_HPP
#define CHUNKINDEX_HPP

#define GLM_FORCE_RADIANS
#include "chunk.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <algorithm>
#include <stdio.h>

class AABB {
public:
  AABB() : ub(0), lb(0) { }
  AABB(glm::vec3 _lb, glm::vec3 _ub) : ub(_ub), lb(_lb) {}
  glm::vec3 lb, ub;
  void Print() {
    printf("(%g,%g,%g)-(%g,%g,%g)\n",
        lb.x, lb.y, lb.z,
        ub.x, ub.y, ub.z);
  }
  bool Intersect(AABB& other) {
    if (lb.x > other.ub.x) return false;
    else if (lb.y > other.ub.y) return false;
    else if (lb.z > other.ub.z) return false;
    else if (ub.x < other.lb.x) return false;
    else if (ub.y < other.lb.y) return false;
    else if (ub.z < other.lb.z) return false;
    else return true;
  }
  void ExpandToPoint(glm::vec3 p) {
    lb.x = std::min(lb.x, p.x);
    ub.x = std::max(ub.x, p.x);
    lb.y = std::min(lb.y, p.y);
    ub.y = std::max(ub.y, p.y);
    lb.z = std::min(lb.z, p.z);
    ub.z = std::max(ub.z, p.z);
  }
  static AABB Union(const AABB& a, const AABB& b) {
    glm::vec3 lb, ub;
    lb.x = std::min(a.lb.x, b.lb.x);
    lb.y = std::min(a.lb.y, b.lb.y);
    lb.z = std::min(a.lb.z, b.lb.z);
    ub.x = std::max(a.ub.x, b.ub.x);
    ub.y = std::max(a.ub.y, b.ub.y);
    ub.z = std::max(a.ub.z, b.ub.z);
    return AABB(lb, ub);
  }
  bool ContainsPoint(glm::vec3 p) {
    return ((lb.x <= p.x) && (ub.x >= p.x) &&
            (lb.y <= p.y) && (ub.y >= p.y) &&
            (lb.z <= p.z) && (ub.z >= p.z));
  }
  glm::vec3 GetClosestPoint(glm::vec3 p) {
    glm::vec3 ret = p;
    if (ContainsPoint(p)) return p;
    if      (ret.x < lb.x) ret.x = lb.x;
    else if (ret.x > ub.x) ret.x = ub.x;
    if      (ret.y < lb.y) ret.y = lb.y;
    else if (ret.y > ub.y) ret.y = ub.y;
    if      (ret.z < lb.z) ret.z = lb.z;
    else if (ret.z > ub.z) ret.z = ub.z;
    return ret;
  }
};

// Indices for multiple Chunk's

class Background;
class ChunkSprite;

class ChunkIndex {
  friend class Background;
  friend class ChunkSprite;
public:
  glm::vec3 Size() { return glm::vec3(float(x_len), float(y_len), float(z_len)); }
  ChunkIndex() : x_len(0), y_len(0), z_len(0) { }
  ChunkIndex(unsigned _xlen, unsigned _ylen, unsigned _zlen);
  virtual void SetVoxel(unsigned x, unsigned y, unsigned z, int vox) = 0;
  virtual void SetVoxel(const glm::vec3& p, int vox) = 0;
  virtual void SetVoxelSphere(const glm::vec3& p, float radius, int vox) = 0;
  virtual int  GetVoxel(unsigned x, unsigned y, unsigned z) = 0;
  virtual bool IntersectPoint(const glm::vec3& p) = 0;
  virtual void Render(
      const glm::vec3& pos,
      const glm::vec3& scale,
      const glm::mat3& orientation,
      const glm::vec3& anchor) = 0;
#ifdef WIN32
  virtual void Render_D3D11(
    const glm::vec3& pos,
    const glm::vec3& scale,
    const glm::mat3& orientation,
    const glm::vec3& anchor) = 0;
  virtual void RecordRenderCommand_D3D12(
    ChunkPass* chunk_pass,
    const glm::vec3& pos,
    const glm::vec3& scale,
    const glm::mat3& orientation,
    const glm::vec3& anchor,
    const DirectX::XMMATRIX& V,
    const DirectX::XMMATRIX& P) = 0;
#endif
  virtual ~ChunkIndex() {}
  virtual void Fill(int vox) = 0;
  glm::vec3 GetCentroid() { return glm::vec3(x_len*0.5f, z_len*0.5f, y_len*0.5f); }
protected:
  unsigned x_len, y_len, z_len;
  virtual bool GetNeighbors(Chunk* which, Chunk* neighs[26]) = 0;
};

class ChunkGrid : public ChunkIndex {
public:
  ChunkGrid() : xdim(0), ydim(0), zdim(0) { }
  ChunkGrid(unsigned _xlen, unsigned _ylen, unsigned _zlen);
  ChunkGrid(const char* vox_fn);
  ChunkGrid(const ChunkGrid& other);
  virtual void Render(
    const glm::vec3& pos,
    const glm::vec3& scale,
    const glm::mat3& orientation,
    const glm::vec3& anchor);
#ifdef WIN32
  virtual void Render_D3D11(
    const glm::vec3& pos,
    const glm::vec3& scale,
    const glm::mat3& orientation,
    const glm::vec3& anchor
  );
  virtual void RecordRenderCommand_D3D12(
    ChunkPass* chunk_pass,
    const glm::vec3& pos,
    const glm::vec3& scale,
    const glm::mat3& orientation,
    const glm::vec3& anchor,
    const DirectX::XMMATRIX& V,
    const DirectX::XMMATRIX& P);
#endif
  virtual void SetVoxel(unsigned x, unsigned y, unsigned z, int v);
  virtual void SetVoxel(const glm::vec3& p, int vox);
  virtual void SetVoxelSphere(const glm::vec3& p, float radius, int vox);
  virtual int  GetVoxel(unsigned x, unsigned y, unsigned z);
  virtual bool IntersectPoint(const glm::vec3& p);
  virtual void Fill(int vox);
protected:
  Chunk* GetChunk(int x, int y, int z, int* local_x, int* local_y, int* local_z);
  void Init(unsigned _xlen, unsigned _ylen, unsigned _zlen);
  unsigned xdim, ydim, zdim;
  std::vector<Chunk*> chunks;
  virtual bool GetNeighbors(Chunk* which, Chunk* neighs[26]);
  int IX(int x, int y, int z) {
    return x*ydim*zdim + y*zdim + z;
  }
  void FromIX(int ix, int& x, int& y, int& z);
};

#endif
