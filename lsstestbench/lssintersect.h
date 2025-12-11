#pragma once

#include <vector>
#include <memory>

#include <glm/glm.hpp>

class Ray {
public:
  Ray() {}
  Ray(const glm::vec3& _o, const glm::vec3& _d) : origin(_o), direction(_d) {
    direction = glm::normalize(direction);
  }
  glm::vec3 at(const float t) const { return origin + direction * t; }
  glm::vec3 origin{}, direction{};
};

struct HitRecord {
  glm::vec3 p, normal;
  float t;
  bool front_face;
  uint8_t flag{};
  inline void SetFaceNormal(const Ray& r, const glm::vec3& outward_normal) {
    float ddn = glm::dot(r.direction, outward_normal);
    front_face = ddn < 0;
    normal = front_face ? outward_normal : -outward_normal;
  }
};

class Hittable {
public:
  virtual bool Hit(const Ray& r, float t_min, float t_max,
    HitRecord& rec) const = 0;
};

class Sphere : public Hittable {
public:
  Sphere() {}
  Sphere(glm::vec3 cen, double r)
    : center(cen), radius(r) {
  };

  bool Hit(const Ray& r, float t_min, float t_max,
    HitRecord& rec) const override;

  bool use_far_hitpoint{ false };  // for LSS
public:
  glm::vec3 center{};
  float radius{};
};

class LinearSweptSphere : public Hittable {
public:
  LinearSweptSphere() {}
  LinearSweptSphere(glm::vec3 _c0, float _r0, glm::vec3 _c1, float _r1)
    : c0(_c0), c1(_c1), r0(_r0), r1(_r1) {
  };
  bool Hit(const Ray& r, float t_min, float t_max,
    HitRecord& rec) const override;
  bool HitSpheres(const Ray& r, float t_min, float t_max, HitRecord& rec, bool is_inside) const;
  bool do_hit_old(const Ray& r, float t_min, float t_max, HitRecord& rec) const;
  bool do_hit_new(const Ray& r, float t_min, float t_max, HitRecord& rec) const;
public:
  glm::vec3 c0{}, c1{};
  float r0{}, r1{};
};