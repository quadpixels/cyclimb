#ifndef SPRITE_H
#define SPRITE_H

#define GLM_FORCE_RADIANS
#include <gl/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <map>

#include "chunkindex.hpp"
#include "chunk.hpp"

enum SpriteFaction {
  SPRITE_FRIENDLY,
  SPRITE_ENEMY,
};

// 对于一个精灵，有三个常用的坐标系
// 1. 全局坐标系；每个单位就是世界坐标中的单位长
// 2. 体素坐标系；以(0, 0, 0)为原点，每个单位为一个scale对应的方向上的体素单位长
// 3. 局部坐标系；以anchor为原点，每个单位为一个scale对应的方向上的体素单位长
class Sprite {
public:
  enum DrawMode {
    NORMAL,
    WIREFRAME,
  };
  glm::vec3 pos, vel, omega;
  glm::mat3 orientation;
  SpriteFaction faction;
  glm::vec3 scale, anchor;
  DrawMode draw_mode;
  virtual void Render() = 0;
#ifdef WIN32
  virtual void Render_D3D11() = 0;
  virtual void RecordRenderCommand_D3D12(
    ChunkPass* chunk_pass,
    const DirectX::XMMATRIX& V,
    const DirectX::XMMATRIX& P) = 0;
#endif
  virtual void Update(float);
  virtual bool IntersectPoint(const glm::vec3& p_world) = 0;
  virtual bool IntersectPoint(const glm::vec3& p_world, int tolerance) = 0;
  void RotateAroundLocalAxis(const glm::vec3& axis, const float deg);
  void RotateAroundGlobalAxis(const glm::vec3& axis, const float deg);
  glm::vec3 GetVoxelCoord(const glm::vec3& p_world);
  glm::vec3 GetWorldCoord(const glm::vec3& p_local);
  virtual ~Sprite() { }
  Sprite() {
    draw_mode = DrawMode::NORMAL;
  }
  bool marked_for_removal = false;
};

class ChunkSprite : public Sprite {
public:
  ChunkSprite(ChunkIndex* _c) : chunk(_c) { Init(); }
  void Init();
  virtual bool IntersectPoint(const glm::vec3& p_world);
  virtual bool IntersectPoint(const glm::vec3& p_world, int tolerance);
  ChunkIndex* chunk;
  virtual void Render();
#ifdef WIN32
  virtual void Render_D3D11();
  virtual void RecordRenderCommand_D3D12(
    ChunkPass* chunk_pass,
    const DirectX::XMMATRIX& V,
    const DirectX::XMMATRIX& P);
#endif
  AABB GetAABBInWorld();
};

class ChunkAnimSprite : public Sprite {
public:
  ChunkAnimSprite(std::vector<ChunkIndex*>* _c, std::vector<float>* _ts);
  virtual void Render();
  virtual void Update(float);
  virtual bool IntersectPoint(const glm::vec3&);
  virtual bool IntersectPoint(const glm::vec3&, int tolerance);
  glm::vec3 scale;
private:
  ChunkIndex* GetCurrChunk();
  std::map<float, std::pair<ChunkIndex*, glm::vec3> > frames;
  float curr_secs;
  void ComputeAnchors();
};

#endif
