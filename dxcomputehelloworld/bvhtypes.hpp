#pragma once

#include <stdio.h>

const float INFINITY = 1e20;

struct Vector3 {
  float x, y, z;
  Vector3() {}
  Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

  static Vector3 vmin(const Vector3& left, const Vector3& right) {
    return Vector3(
      left.x < right.x ? left.x : right.x,
      left.y < right.y ? left.y : right.y,
      left.z < right.z ? left.z : right.z
    );
  }

  static Vector3 vmax(const Vector3& left, const Vector3& right) {
    return Vector3(
      left.x > right.x ? left.x : right.x,
      left.y > right.y ? left.y : right.y,
      left.z > right.z ? left.z : right.z
    );
  }
  float& operator[](int idx) {
    if (idx == 0) { return x; }
    else if (idx == 1) { return y; }
    else return z;
  }
};
Vector3 operator-(const Vector3& left, const Vector3& right) { return Vector3(left.x - right.x, left.y - right.y, left.z - right.z); }
Vector3 operator+(const Vector3& left, const Vector3& right) { return Vector3(left.x + right.x, left.y + right.y, left.z + right.z); }
Vector3 operator/(const Vector3& left, const float& right) { return Vector3(left.x / right, left.y / right, left.z / right); }

struct AABB {
  Vector3 min;
  Vector3 max;
  AABB(const Vector3& mmin, const Vector3& mmax) : min(mmin), max(mmax) {}
  AABB() : min(Vector3(INFINITY, INFINITY, INFINITY)), max(Vector3(-INFINITY, -INFINITY, -INFINITY)) {}
   void expand(const AABB& aabb) {
    min = Vector3::vmin(min, aabb.min);
    max = Vector3::vmax(max, aabb.max);
  }
  float surface_area() const {
    Vector3 diff = max - min;
    return 2.0f * (diff.x * diff.y + diff.y * diff.z + diff.z * diff.x);
  }
  static AABB create_empty() {
    AABB aabb;
    aabb.min = Vector3(INFINITY, INFINITY, INFINITY);
    aabb.max = Vector3(-INFINITY, -INFINITY, -INFINITY);
    return aabb;
  }
  static AABB from_points(const Vector3* points, int point_count) {
    AABB aabb = create_empty();
    for (int i = 0; i < point_count; i++) {
      aabb.expand(points[i]);
    }
    aabb.fix_if_needed();
    return aabb;
  }
  void expand(const Vector3& point) {
    min = Vector3::vmin(min, point);
    max = Vector3::vmax(max, point);
  }
  void fix_if_needed(float epsilon = 0.001f) {
    if (is_empty()) return;
    for (int dimension = 0; dimension < 3; dimension++) {
      float eps = epsilon;
      while (max[dimension] - min[dimension] < eps) {
        min[dimension] -= eps;
        max[dimension] += eps;
        eps *= 2.0f;
      }
    }
  }
  bool is_empty() const {
    return
      min.x == INFINITY && min.y == INFINITY && min.z == INFINITY &&
      max.x == -INFINITY && max.y == -INFINITY && max.z == -INFINITY;
  }
};

struct Triangle {
  Vector3 v0, v1, v2;
  Vector3 get_center() {
    return (v0 + v1 + v2) / 3.0f;
  }
  AABB get_aabb() {
    Vector3 vertices[3] = { v0,v1,v2 };
    return AABB::from_points(vertices, 3);
  }
  Triangle(float v0x, float v0y, float v0z, float v1x, float v1y, float v1z, float v2x, float v2y, float v2z)
    : v0(v0x, v0y, v0z), v1(v1x, v1y, v1z), v2(v2x, v2y, v2z)
  {
  }
};

struct BvhCB {
  int num_triangles;
  int num_threads;
};

struct LeafNode {
  int parent;
  int num_tris;
  int tri_idx0;
  Vector3 v0, v1, v2;
  int tri_idx1;
  Vector3 v3, v4, v5;
  AABB aabb;

  void Print() {
    printf("[LeafNode] %d tris: %d, %d\n", num_tris, tri_idx0, tri_idx1);
  }
};

struct BVHMetaData {
  int num_leaf_nodes;
  void Print() {
    printf("[BVHMetaData] %d leaf nodes\n", num_leaf_nodes);
  }
};