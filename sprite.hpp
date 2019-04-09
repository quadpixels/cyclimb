#ifndef SPRITE_H
#define SPRITE_H

#include <GL/glew.h>
#include <GL/freeglut.h>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "chunkindex.hpp"
#include <map>

enum SpriteFaction {
  SPRITE_FRIENDLY,
  SPRITE_ENEMY,
};

class Sprite {
public:
  glm::vec3 pos, vel;
  glm::mat3 orientation;
  SpriteFaction faction;
  virtual void Render() = 0;
  virtual void Update(float);
  virtual bool IntersectPoint(const glm::vec3& p_world) = 0;
  virtual bool IntersectPoint(const glm::vec3& p_world, int tolerance) = 0;
  virtual ~Sprite() { }
};

class ChunkSprite : public Sprite {
public:
  ChunkSprite(ChunkIndex* _c) : chunk(_c) { Init(); }
  void Init();
  glm::vec3 GetLocalCoord(const glm::vec3& p_world);
  glm::vec3 GetWorldCoord(const glm::vec3& p_local);
  virtual bool IntersectPoint(const glm::vec3& p_world);
  virtual bool IntersectPoint(const glm::vec3& p_world, int tolerance);
  ChunkIndex* chunk;
  glm::vec3 scale, anchor;
  void RotateAroundLocalAxis(const glm::vec3& axis, const float deg);
  void RotateAroundGlobalAxis(const glm::vec3& axis, const float deg);
  virtual void Render();
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
