RWByteAddressBuffer buffer0 : register(u0);
RWByteAddressBuffer buffer1 : register(u1);

cbuffer OverallCB : register(b0) {
  int num_triangles;
  int num_threads;
};

struct Triangle {
  float3 v0, v1, v2;
};

struct AABB {
  float3 min, max;
};

struct LeafNode {
  int parent;
  int num_tris;
  int tri_idx0;
  float3 v0, v1, v2;
  int tri_idx1;
  float3 v3, v4, v5;
  AABB aabb;
};

struct BVHMetaData {
  int num_leaf_nodes;
};
RWStructuredBuffer<BVHMetaData> buffer2 : register(u2);

float3 GetTriangleCenter(Triangle tri) {
  return (tri.v0 + tri.v1 + tri.v2) / 3.0;
}

Triangle LoadTriangle(RWByteAddressBuffer buf, int offset) {
  Triangle ret;
  ret.v0.x = buf.Load(offset);
  ret.v0.y = buf.Load(offset + 4);
  ret.v0.z = buf.Load(offset + 8);
  ret.v1.x = buf.Load(offset + 12);
  ret.v1.y = buf.Load(offset + 16);
  ret.v1.z = buf.Load(offset + 20);
  ret.v2.x = buf.Load(offset + 24);
  ret.v2.y = buf.Load(offset + 28);
  ret.v2.z = buf.Load(offset + 32);
  return ret;
}

void StoreLeafNodeData(RWByteAddressBuffer buf, int offset, LeafNode x) {
  buf.Store(offset, x.parent);
  buf.Store(offset + 4, x.num_tris);
  buf.Store(offset + 8, x.tri_idx0);
  buf.Store(offset + 12, x.v0.x);
  buf.Store(offset + 16, x.v0.y);
  buf.Store(offset + 20, x.v0.z);
  buf.Store(offset + 24, x.v1.x);
  buf.Store(offset + 28, x.v1.y);
  buf.Store(offset + 32, x.v1.z);
  buf.Store(offset + 36, x.v2.z);
  buf.Store(offset + 40, x.v2.z);
  buf.Store(offset + 44, x.v2.z);
  buf.Store(offset + 48, x.tri_idx1);
  buf.Store(offset + 52, x.v3.x);
  buf.Store(offset + 56, x.v3.y);
  buf.Store(offset + 60, x.v3.z);
  buf.Store(offset + 64, x.v4.x);
  buf.Store(offset + 68, x.v4.y);
  buf.Store(offset + 72, x.v4.z);
  buf.Store(offset + 76, x.v5.z);
  buf.Store(offset + 80, x.v5.z);
  buf.Store(offset + 84, x.v5.z);
  buf.Store(offset + 88, x.aabb.min.x);
  buf.Store(offset + 92, x.aabb.min.y);
  buf.Store(offset + 96, x.aabb.min.z);
  buf.Store(offset + 100, x.aabb.max.x);
  buf.Store(offset + 104, x.aabb.max.y);
  buf.Store(offset + 108, x.aabb.max.z);
}

[numthreads(32, 1, 1)]
void PopulatePrimitiveData(uint3 tid : SV_DispatchThreadID) {
  const int step = 2;
  for (int i = tid.x * step; i < num_triangles; i += num_threads * step) {
    Triangle t0 = LoadTriangle(buffer0, i * 36);
    Triangle t1 = LoadTriangle(buffer0, (i + 1) * 36);
    
    LeafNode node;
    node.parent = -1;
    node.num_tris = 2;
    node.tri_idx0 = i;
    node.v0 = t0.v0;
    node.v1 = t0.v1;
    node.v2 = t0.v2;
    node.tri_idx1 = i + 1;
    node.v3 = t1.v0;
    node.v4 = t1.v1;
    node.v5 = t1.v2;
    node.aabb.min = float3(0, 0, 0);
    node.aabb.max = float3(0, 0, 0);

    int leaf_idx;
    InterlockedAdd(buffer2[0].num_leaf_nodes, 1, leaf_idx);

    StoreLeafNodeData(buffer1, leaf_idx * 112, node);
  }
}