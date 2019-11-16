#include "sprite.hpp"
#include <stdio.h>
#include <assert.h>
#include "util.hpp"
#define _USE_MATH_DEFINES
#include <math.h>

void Sprite::Update(float sec) {
  pos += vel * sec;
  float rad_sq = glm::dot(omega, omega);
  if (glm::dot(omega, omega) > 0) {
    RotateAroundGlobalAxis(glm::normalize(omega), sqrtf(rad_sq)*180/M_PI);
  }
}

void ChunkSprite::Init() {
  orientation = glm::mat3(1);
  scale = glm::vec3(1,1,1);
  // Default Anchor: centered
  anchor = chunk->Size() * 0.5f;
}

void Sprite::RotateAroundGlobalAxis(const glm::vec3& axis, const float deg) {
  glm::mat4 o4(orientation);
  o4 = glm::rotate(o4, deg*3.14159f/180.0f, glm::inverse(orientation)*axis);
  orientation = glm::mat3(o4);
}

void Sprite::RotateAroundLocalAxis(const glm::vec3& axis, const float deg) {
  glm::mat4 o4(orientation);
  o4 = glm::rotate(o4, deg*3.14159f/180.0f, axis);
  orientation = glm::mat3(o4);
}

void ChunkSprite::Render() {
  chunk->Render(pos, scale, orientation, anchor);
}

void ChunkSprite::Render_D3D11() {
  chunk->Render_D3D11(pos, scale, orientation, anchor);
}

glm::vec3 Sprite::GetVoxelCoord(const glm::vec3& p_world) {
  glm::vec3 p_local = glm::inverse(orientation) * (p_world - pos);
  glm::vec3 pc = (p_local / scale) + anchor; // pc = point_chunk
  return pc;
}

bool ChunkSprite::IntersectPoint(const glm::vec3& p_world) {
  glm::vec3 pc = GetVoxelCoord(p_world);
  return (chunk->GetVoxel(unsigned(pc.x), unsigned(pc.y), unsigned(pc.z)) > 0);
}

bool ChunkSprite::IntersectPoint(const glm::vec3& p_world, int tolerance) {
  glm::vec3 p_local = glm::inverse(orientation) * (p_world - pos);
  glm::vec3 pc = p_local / scale + anchor; // pc = point_chunk
  for (int dx=-tolerance; dx<=tolerance; dx++) {
    for (int dy=-tolerance; dy<=tolerance; dy++) {
      for (int dz=-tolerance; dz<=tolerance; dz++) {
        int xx = int(pc.x) + dx, yy = int(pc.y) + dy, zz = int(pc.z) + dz;
        if (xx >= 0 && yy >= 0 && zz >= 0) {
          if (chunk->GetVoxel(unsigned(xx), unsigned(yy), unsigned(zz)) > 0) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

glm::vec3 Sprite::GetWorldCoord(const glm::vec3& p_voxel) {
  return pos + orientation * (p_voxel * scale - anchor);
}

AABB ChunkSprite::GetAABBInWorld() {
  glm::vec3 p[8];
  p[0] = GetWorldCoord(glm::vec3(0,            0,            0));
  p[1] = GetWorldCoord(glm::vec3(chunk->x_len, 0,            0));
  p[2] = GetWorldCoord(glm::vec3(0,            chunk->y_len, 0));
  p[3] = GetWorldCoord(glm::vec3(chunk->x_len, chunk->y_len, 0));
  p[4] = GetWorldCoord(glm::vec3(0,            0,            chunk->z_len));
  p[5] = GetWorldCoord(glm::vec3(chunk->x_len, 0,            chunk->z_len));
  p[6] = GetWorldCoord(glm::vec3(0,            chunk->y_len, chunk->z_len));
  p[7] = GetWorldCoord(glm::vec3(chunk->x_len, chunk->y_len, chunk->z_len));
  glm::vec3 ub(-1e20, -1e20, -1e20), lb(1e20, 1e20, 1e20);
  for (int i=0; i<8; i++) {
    ub.x = std::max(ub.x, p[i].x); ub.y = std::max(ub.y, p[i].y); ub.z = std::max(ub.z, p[i].z);
    lb.x = std::min(lb.x, p[i].x); lb.y = std::min(lb.y, p[i].y); lb.z = std::min(lb.z, p[i].z);
  }
  return AABB(lb, ub);
}

//============CHUNK ANIM SPRITE===================
ChunkAnimSprite::ChunkAnimSprite(std::vector<ChunkIndex*>* _c, std::vector<float>* _ts) {
  orientation = glm::mat3(1);
  curr_secs = 0;
  scale = glm::vec3(1,1,1);
  assert (_c->size() == _ts->size());
  for (unsigned i=0; i<_ts->size(); i++) {
    frames[_ts->at(i)] = std::make_pair(
        _c->at(i), _c->at(i)->GetCentroid());
  }
}

ChunkIndex* ChunkAnimSprite::GetCurrChunk() {
  std::map<float, std::pair<ChunkIndex*, glm::vec3> >::iterator itr =
        frames.upper_bound(curr_secs);
  return itr->second.first;
}

void ChunkAnimSprite::Render() {
  std::map<float, std::pair<ChunkIndex*, glm::vec3> >::iterator itr =
      frames.upper_bound(curr_secs);
  ChunkIndex* c = itr->second.first;
  glm::vec3   a = itr->second.second;
  c->Render(pos, scale, orientation, a);
}

void ChunkAnimSprite::Update(const float secs) {
  curr_secs += secs;
  if (curr_secs >= frames.rbegin()->first) curr_secs = 0; // reset
  pos += vel * secs;
}

// DUMMY
bool ChunkAnimSprite::IntersectPoint(const glm::vec3& p_world) {
  return false;
}
bool ChunkAnimSprite::IntersectPoint(const glm::vec3& p_world, int tolerance) {
  return false;
}
