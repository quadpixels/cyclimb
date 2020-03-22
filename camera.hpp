#ifndef CAMERA_H
#define CAMERA_H

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>
#include <DirectXMath.h>

class Camera {
public:
  Camera();
  glm::mat4 GetViewMatrix();
  DirectX::XMMATRIX GetViewMatrix_D3D11();
  DirectX::XMVECTOR GetPos_D3D11();
  void Update(float);
  glm::vec3 pos, lookdir, up, vel; // in world coordinates

  void MoveUp(float dist);
  void MoveDown(float dist);
  void MoveLeft(float dist);
  void MoveRight(float right);
  void MoveFront(float dist);
  void MoveBack(float dist);

  void RollClockwise(float rad);
  void RollCounterClockwise(float rad);
  void LookLeft(float rad);
  void LookRight(float rad);
  void LookUp(float rad);
  void LookDown(float rad);

  void RotateAlongPoint(glm::vec3 p, glm::vec3 local_axis, float rad);

  void InitForHelpInfo();

private:
  void do_MoveInLocalCoords(glm::vec3 local_dir, float dist);
  void do_RotateInLocalCoords(glm::vec3 local_axis, float rad);
};

#endif
