#include "camera.hpp"

Camera::Camera() {
  pos = glm::vec3(0.0f,128.0f,90.0f);
  lookdir = glm::normalize(-pos);
  up  = glm::normalize(glm::vec3(0,10,-50));
}

void Camera::InitForHelpInfo() {
  pos = glm::vec3(0.0f, 0.0f, 20.0f);
  lookdir = glm::normalize(-pos);
  up = glm::vec3(0, 1, 0);
}

glm::mat4 Camera::GetViewMatrix() {
  return glm::lookAt(pos, pos + lookdir, up);
}

// Will convert world coordinates accordingly
#ifdef WIN32
DirectX::XMMATRIX Camera::GetViewMatrix_D3D11() {
  DirectX::XMVECTOR eye_d3d, lookdir_d3d, target_d3d, up_d3d;
  
  eye_d3d.m128_f32[0] = pos.x;
  eye_d3d.m128_f32[1] = pos.y;
  eye_d3d.m128_f32[2] = -pos.z;

  lookdir_d3d.m128_f32[0] = lookdir.x;
  lookdir_d3d.m128_f32[1] = lookdir.y;
  lookdir_d3d.m128_f32[2] = -lookdir.z;

  up_d3d.m128_f32[0] = up.x;
  up_d3d.m128_f32[1] = up.y;
  up_d3d.m128_f32[2] = -up.z;

  return DirectX::XMMatrixLookAtLH(eye_d3d, DirectX::XMVectorAdd(eye_d3d, lookdir_d3d), up_d3d);
}
#endif

void Camera::Update(float sec) {
  pos += vel * sec;
}

void Camera::MoveUp(float dist)    { do_MoveInLocalCoords(glm::vec3(0, 1, 0), dist); }
void Camera::MoveDown(float dist)  { do_MoveInLocalCoords(glm::vec3(0,-1, 0), dist); }
void Camera::MoveLeft(float dist)  { do_MoveInLocalCoords(glm::vec3(1, 0, 0), dist); }
void Camera::MoveRight(float dist) { do_MoveInLocalCoords(glm::vec3(-1,0, 0), dist); }
void Camera::MoveFront(float dist) { do_MoveInLocalCoords(glm::vec3(0, 0,-1), dist); }
void Camera::MoveBack(float dist)  { do_MoveInLocalCoords(glm::vec3(0, 0, 1), dist); }

void Camera::do_MoveInLocalCoords(glm::vec3 local_dir, float dist) {
  glm::vec3 world_x = glm::cross(up, lookdir),
            world_z = -lookdir,
            world_y = up,
            delta = world_x * local_dir.x * dist +
                    world_y * local_dir.y * dist +
                    world_z * local_dir.z * dist;
  pos += delta;
}

void Camera::LookUp(float rad)               { do_RotateInLocalCoords(glm::vec3(1, 0, 0), rad); }
void Camera::LookDown(float rad)             { do_RotateInLocalCoords(glm::vec3(1, 0, 0),-rad); }
void Camera::LookLeft(float rad)             { do_RotateInLocalCoords(glm::vec3(0, 1, 0), rad); }
void Camera::LookRight(float rad)            { do_RotateInLocalCoords(glm::vec3(0, 1, 0),-rad); }
void Camera::RollClockwise(float rad)        { do_RotateInLocalCoords(glm::vec3(0, 0, 1), rad); }
void Camera::RollCounterClockwise(float rad) { do_RotateInLocalCoords(glm::vec3(0, 0, 1),-rad); }
void Camera::do_RotateInLocalCoords(glm::vec3 local_axis, float rad) {
  glm::vec3 world_x = -glm::cross(up, lookdir),
            world_z = -lookdir,
            world_y =  up;
  glm::mat3 basis(world_x, world_y, world_z);

  glm::mat3 rot = glm::mat3(glm::rotate(rad, local_axis));
  basis = basis * rot;

  lookdir = -basis[2];
  up      = basis[1];
}


void Camera::RotateAlongPoint(glm::vec3 p, glm::vec3 local_axis, float rad) {
  float dist = sqrt(glm::dot(p - pos, p - pos));
  do_RotateInLocalCoords(glm::vec3(local_axis), rad);
  pos = p - (glm::normalize(lookdir) * dist);
}

#ifdef WIN32
DirectX::XMVECTOR Camera::GetPos_D3D11() {
  DirectX::XMVECTOR ret = { };
  ret.m128_f32[0] = pos.x;
  ret.m128_f32[1] = pos.y;
  ret.m128_f32[2] = pos.z * -1;
  ret.m128_f32[3] = 1.0f;
  return ret;
}
#endif