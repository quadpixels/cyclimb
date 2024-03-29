#include <stdio.h>
#include "chunk.hpp"
#include "util.hpp"
#include "camera.hpp"
#include "scene.hpp"
#include <string.h>

float    Chunk::l0 = 1.0f;
int      Chunk::size = 32;
unsigned Chunk::program = 0;

extern bool IsGL();
extern bool IsD3D11();
extern bool IsD3D12();
extern Camera* GetCurrentSceneCamera();

#ifdef WIN32
#include <wrl/client.h>
#include <d3dcompiler.h>
extern void UpdateGlobalPerObjectCB(const DirectX::XMMATRIX* M, const DirectX::XMMATRIX* V, const DirectX::XMMATRIX* P);
extern ID3D11Device* g_device11;
extern ID3D11DeviceContext* g_context11;
extern ID3D11Buffer* g_perobject_cb_default_palette;
extern DirectX::XMMATRIX g_projection_d3d11;

extern ID3D12Device* g_device12;

struct DefaultPalettePerObjectCB {
  DirectX::XMMATRIX M, V, P;
};
using Microsoft::WRL::ComPtr;
#endif

Chunk::Chunk() {
  vao = vbo = tri_count = 0;
#ifdef WIN32
  d3d11_vertex_buffer = nullptr;
#endif
  block = new unsigned char[size * size * size];
  light = new int[size * size * size];
  memset(block, 0x00, sizeof(char)*size*size*size);
  memset(light, 0x00, sizeof(int) *size*size*size);
  is_dirty = true;
}

void Chunk::BuildBuffers(Chunk* neighbors[26]) {
  bool is_gl = IsGL();
  if (is_gl) {
    if (vbo != (unsigned)-999) {
      glDeleteBuffers(1, &vbo);
      glDeleteVertexArrays(1, &vao);
      vao = vbo = 0;
    }
  }
  else {
    #ifdef WIN32
    if (d3d11_vertex_buffer != nullptr) {
      //d3d11_vertex_buffer->Release(); // TODO: 为什么导致crash
    }
    #endif
  }

  // axis=0  x=u y=v z=w
  // axis=1  x=w y=u z=v
  // axis=2  x=v y=w z=u

  // x_idxs 表示 x 应该是 {u,v,w} 中的第几个；其它的依次类推
  const int x_idxs[] = { 0,2,1 }, y_idxs[] = { 1,0,2 }, z_idxs[] = { 2,1,0 };

  // u_idxs 表示 u 应该是 {x,y,z} 中的第几个，其它的依次类推
  const int u_axes[] = { 0,1,2 }, v_axes[] = { 1,2,0 }, w_axes[] = { 2,0,1 };

  const glm::vec3 units[] = { glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1) };
  const int SIZE = (size+1) * (size+1) * (size+1);
  float* tmp_vert = new float[SIZE * 36 * 3],
       * tmp_norm = new float[SIZE * 36 * 3];
  int  * tmp_data = new int[SIZE * 36];
  int  * tmp_ao   = new int[SIZE * 36];
  int idx_v = 0, idx_n = 0, idx_data = 0, idx_ao = 0;
  tri_count = 0;

  const float coord_min = 0;//(size-1) * l0 * 0.5f;
  for (int aidx = 0; aidx < 3; aidx++) {
    for (int w=0; w<size; w++) {
      for (int d=0; d<2; d++) {
        // 1. Generate Scratch
        std::vector<int> scratch(size*size, 0);
        glm::vec3 u0 = units[u_axes[aidx]], v0 = units[v_axes[aidx]], w0 = units[w_axes[aidx]];
        if (d==1) w0 = -w0;
        for (int u=0; u<size; u++) {
          for (int v=0; v<size; v++) {
            const int sidx = u*size + v;
            float uvw[] = { float(u), float(v), float(w) };
            glm::vec3 xyz = glm::vec3(uvw[x_idxs[aidx]], uvw[y_idxs[aidx]], uvw[z_idxs[aidx]]);
            glm::vec3 xyz_next = xyz;
            xyz_next[w_axes[aidx]] += (d==0 ? 1.0f : -1.0f);
            glm::vec3 origin(-coord_min + xyz.x*l0, -coord_min + xyz.y*l0, -coord_min + xyz.z*l0);

            const int ix_xyz = IX(int(xyz.x), int(xyz.y), int(xyz.z));
            int voxel = block[ix_xyz];
            if (voxel > 0) voxel |= (light[ix_xyz] << 8);
            if (voxel != 0) {
              if (d==0) {
                if (!(w==size-1 || block[IX(int(xyz_next.x), int(xyz_next.y), int(xyz_next.z))]==0)) continue;
              } else {
                if (!(w==0 || block[IX(int(xyz_next.x), int(xyz_next.y), int(xyz_next.z))]==0)) continue;
              }
            }
            scratch[sidx] = voxel;
          }
        }

        // 2. Mesh using Scratch
        for (int u=0; u<size; u++) {
          for (int v=0; v<size; v++) {
            float uvw[] = { float(u), float(v), float(w) };
            glm::vec3 xyz = glm::vec3(uvw[x_idxs[aidx]], uvw[y_idxs[aidx]], uvw[z_idxs[aidx]]);
            glm::vec3 origin(-coord_min + xyz.x*l0, -coord_min + xyz.y*l0, -coord_min + xyz.z*l0);
            const float l = 0.5f * l0;
            const int voxel = scratch[u*size+v];
            if (voxel != 0) {                          //     V
              int du = 1, dv = 1;                     // P1 ----------- P0
              //       determine value of du and dv   // |  +W 穿出屏幕   |
              glm::vec3 pos_w = origin + l*w0,        // P2 ----------- P3 ---> U
                        p2 = pos_w - l*u0 - l*v0, p3 = p2 + float(du)*l0*u0, p0 = p3 + float(dv)*l0*v0,
                        p1 = p2 + float(dv)*l0*v0;

              // +Z -Z +X -X +Y -Y
              const int ao_dirs[] = { 4,5,0,1,2,3 };
              const int ao_dir = ao_dirs[2*aidx+d];

              int ao_2 = GetOcclusionFactor(p2.x, p2.y, p2.z, ao_dir, neighbors);
              int ao_0 = 0, ao_1 = 0, ao_3 = 0;

              // 延伸 dv
              // Try possible dv values & make dv as large as possible
              bool may_extend_u = true;
              for (int ddv=1; ddv+v-1<size; ddv++) {
                int next_voxel = scratch[u*size+(v-1)+ddv];
                if (next_voxel != voxel) break;
                glm::vec3 p33 = p2 + float(du)*l0*u0, p00 = p33 + float(ddv)*l0*v0,
                          p11 = p2 + float(ddv)*l0*v0;
                int ao_33 = GetOcclusionFactor(p33.x, p33.y, p33.z, ao_dir, neighbors),
                    ao_00 = GetOcclusionFactor(p00.x, p00.y, p00.z, ao_dir, neighbors),
                    ao_11 = GetOcclusionFactor(p11.x, p11.y, p11.z, ao_dir, neighbors);

                if (! (ao_33 == ao_2 && ao_11 == ao_2 && ao_00 == ao_2)) {
                  may_extend_u = false;
                  if (! (du==1 && ddv==1)) break;
                }

                ao_0 = ao_00; ao_1 = ao_11; ao_3 = ao_33;
                p0   = p00;   p1   = p11;   p3   = p33;
                dv = ddv; // 到这里这个 dv 的试探值就可以被采用了。
                if (!may_extend_u) break;
              }

              // 延伸 du
              if (may_extend_u) { // 这个要加上的，不然间隔为2的竖条会造成bug
                for (int ddu = 2; ddu+u-1<size; ddu++) {
                  bool line_ok = true, checked = false;
                  glm::vec3 p11, p33, p00;
                  for (int vv=v; vv<v+dv; vv++) {
                    checked = true;
                    int the_voxel = scratch[(u-1+ddu)*size + vv];
                    if (the_voxel != voxel) {
                      line_ok = false; break;
                    }
                    p33 = p2 + float(ddu)*l0*u0, p00 = p33 + float(vv-v+1)*l0*v0,
                    p11 = p2 + float(vv-v+1)*l0*v0;
                    int ao_33 = GetOcclusionFactor(p33.x, p33.y, p33.z, ao_dir, neighbors),
                        ao_00 = GetOcclusionFactor(p00.x, p00.y, p00.z, ao_dir, neighbors),
                        ao_11 = GetOcclusionFactor(p11.x, p11.y, p11.z, ao_dir, neighbors);
                    if (! (ao_33 == ao_2 && ao_11 == ao_2 && ao_00 == ao_2)) {
                      line_ok = false; break;
                    }
                  }
                  if (line_ok && checked) {
                    ao_0 = ao_1 = ao_3 = ao_2;
                    p0   = p00;   p1   = p11;   p3   = p33;
                    du   = ddu; // 到这里、这个 dv 的试探值就可以被采用了。
                  } else break;
                }
              }

              const float verts_xyz[] = {
                p3.x, p3.y, p3.z, p0.x, p0.y, p0.z, p2.x, p2.y, p2.z,
                p1.x, p1.y, p1.z, p2.x, p2.y, p2.z, p0.x, p0.y, p0.z
              };
              int idxes[] = {
                0,1,2, 3,4,5, 6,7,8, 9,10,11, 12,13,14, 15,16,17, // 正
                0,1,2, 6,7,8, 3,4,5, 9,10,11, 15,16,17, 12,13,14  // 反
              };
              for (unsigned i=0; i<18; i++) {
                tmp_vert[idx_v++] = verts_xyz[idxes[i + d*18]];
              }
              for (unsigned i=0; i<6; i++) {
                tmp_norm[idx_n++] = aidx*2 + d;
              }

              int ao_factors[] = {
                ao_3, ao_0, ao_2, ao_1, ao_2, ao_0,
                ao_3, ao_2, ao_0, ao_1, ao_0, ao_2
              };
              for (int i=0; i<6; i++) {
                tmp_ao[idx_ao++]     = ao_factors[i + d*6];
                tmp_data[idx_data++] = voxel;
              }
              tri_count += 2;

              // Clear scratch
              for (int uu=u; uu<u+du; uu++)
                for (int vv=v; vv<v+dv; vv++)
                  scratch[uu*size+vv] = 0;
            }
          }
        }
      }
    }
  }

  float* tmp_packed = new float[tri_count * 3 * 6];
  for (int i=0; i<tri_count*3; i++) {
    const float x=tmp_vert[i*3], y=tmp_vert[i*3+1], z=tmp_vert[i*3+2],
                nidx = tmp_norm[i],
                data = tmp_data[i],
                ao   = tmp_ao[i];
    std::vector<float> entry;
    if (is_gl) {
      entry = std::vector<float>({ x,y,z,nidx,data,ao });
    }
    else {
      entry = std::vector<float>({ x,y,-z,nidx,data,ao });
    }

    for (unsigned j=0; j<6; j++) {
      int ii = i;
      if (!is_gl) { // Change winding direction for D3D11
        if (ii % 3 == 1) ii++;
        else if (ii % 3 == 2) ii--;
      }
      tmp_packed[ii*6 + j] = entry[j];
    }
  }

  if (is_gl) {
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*tri_count * 3 * 6,
      tmp_packed, GL_STATIC_DRAW);
    const size_t stride = sizeof(float) * 6;

    // XYZ pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);

    // Normal idx
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    // Data
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(4 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    // AO Index
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(5 * sizeof(GLfloat)));
    glEnableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    MyCheckGLError("Chunk::BuildBuffer");
  }
  else {
    #ifdef WIN32
    if (IsD3D11()) {
      if (tri_count > 0) {
        D3D11_BUFFER_DESC desc = { };
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.ByteWidth = sizeof(float) * tri_count * 3 * 6;
        desc.StructureByteStride = sizeof(float) * 6;
        desc.Usage = D3D11_USAGE_IMMUTABLE;

        D3D11_SUBRESOURCE_DATA srd = { };
        srd.pSysMem = tmp_packed;
        srd.SysMemPitch = sizeof(float) * tri_count * 3 * 6;

        assert(SUCCEEDED(g_device11->CreateBuffer(&desc, &srd, &d3d11_vertex_buffer)));
      }
    }
    else if (IsD3D12()) {
      if (tri_count > 0) {
        size_t byte_width = sizeof(float) * tri_count * 3 * 6;
        CE(g_device12->CreateCommittedResource(
          &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
          D3D12_HEAP_FLAG_NONE,
          &keep(CD3DX12_RESOURCE_DESC::Buffer(byte_width)),
          D3D12_RESOURCE_STATE_GENERIC_READ,
          nullptr,
          IID_PPV_ARGS(&d3d12_vertex_buffer)));
        char* pData;
        CD3DX12_RANGE readRange(0, 0);
        CE(d3d12_vertex_buffer->Map(0, &readRange, (void**)&pData));
        memcpy(pData, tmp_packed, byte_width);
        d3d12_vertex_buffer->Unmap(0, nullptr);
        d3d12_vertex_buffer_view.BufferLocation = d3d12_vertex_buffer->GetGPUVirtualAddress();
        d3d12_vertex_buffer_view.StrideInBytes = sizeof(float) * 6;
        d3d12_vertex_buffer_view.SizeInBytes = byte_width;
      }
    }
    #endif
  }

  delete[] tmp_vert; delete[] tmp_norm; delete[] tmp_data; delete[] tmp_ao;
  delete[] tmp_packed;
  is_dirty = false;
}

int Chunk::GetOcclusionFactor(const float x0, const float y0, const float z0, const int dir,
  Chunk* neighs[26]) {
  const float coord_min = l0 * 0.5f;//(size) * l0 * 0.5f;
  const int xx = (x0 + coord_min) / l0, yy = (y0 + coord_min) / l0, zz = (z0 + coord_min) / l0;
  int x_next[4], y_next[4], z_next[4];
  switch (dir) {
    case 0: // +X
      x_next[0] = x_next[1] = x_next[2] = x_next[3] = xx;
      y_next[0] = y_next[1] = yy; y_next[2] = y_next[3] = yy-1;
      z_next[0] = z_next[2] = zz; z_next[1] = z_next[3] = zz-1;
      break;
    case 1: // -X
      x_next[0] = x_next[1] = x_next[2] = x_next[3] = xx-1;
      y_next[0] = y_next[1] = yy; y_next[2] = y_next[3] = yy-1;
      z_next[0] = z_next[2] = zz; z_next[1] = z_next[3] = zz-1;
      break;
    case 2: // +Y
      y_next[0] = y_next[1] = y_next[2] = y_next[3] = yy;
      x_next[0] = x_next[1] = xx; x_next[2] = x_next[3] = xx-1;
      z_next[0] = z_next[2] = zz; z_next[1] = z_next[3] = zz-1;
      break;
    case 3: // -Y
      y_next[0] = y_next[1] = y_next[2] = y_next[3] = yy-1;
      x_next[0] = x_next[1] = xx; x_next[2] = x_next[3] = xx-1;
      z_next[0] = z_next[2] = zz; z_next[1] = z_next[3] = zz-1;
      break;
    case 4:
      z_next[0] = z_next[1] = z_next[2] = z_next[3] = zz;
      x_next[0] = x_next[1] = xx; x_next[2] = x_next[3] = xx-1;
      y_next[0] = y_next[2] = yy; y_next[1] = y_next[3] = yy-1;
      break;
    case 5:
      z_next[0] = z_next[1] = z_next[2] = z_next[3] = zz-1;
      x_next[0] = x_next[1] = xx; x_next[2] = x_next[3] = xx-1;
      y_next[0] = y_next[2] = yy; y_next[1] = y_next[3] = yy-1;
      break;
  }

  int occ = 0;
  for (int i=0; i<4; i++) {
    const int x1 = x_next[i], y1 = y_next[i], z1 = z_next[i];
    if (x1 < 0 || y1 < 0 || z1 < 0 ||
      x1 >= size || y1 >= size || z1 >= size) {
    if (x1 >= size) { // Cases [0] and [6:13]
      if (y1 < 0) {
        if (z1 < 0) {
          if (neighs[13] && neighs[13]->block[IX(0, size-1, size-1)]) occ++;
        } else if (z1 < size) {
          if (neighs[12] && neighs[12]->block[IX(0, size-1, z1)]) occ++;
        } else {
          if (neighs[11] && neighs[11]->block[IX(0, size-1, 0)]) occ++;
        }
      } else if (y1 < size) {
        if (z1 < 0) {
          if (neighs[10] && neighs[10]->block[IX(0, y1, size-1)]) occ++;
        } else if (z1 < size) {
          if (neighs[0] && neighs[0]->block[IX(0, y1, z1)]) occ++;
        } else {
          if (neighs[9] && neighs[9]->block[IX(0, y1, 0)]) occ++;
        }
      } else {
        if (z1 < 0) {
          if (neighs[8] && neighs[8]->block[IX(x1, 0, size - 1)]) occ++;
        }
        else if (z1 < size) {
          if (neighs[7] && neighs[7]->block[IX(x1, 0, z1)]) occ++;
        }
        else {
          if (neighs[6] && neighs[6]->block[IX(x1, 0, 0)]) occ++;
        }
      }
    } else if (x1 >= 0) { // Cases [2:5], [14:17]
      if (y1 < 0) {
        if (z1 < 0) {
          if (neighs[17] && neighs[17]->block[IX(x1, size-1, size-1)]) occ++;
        } else if (z1 < size) {
          if (neighs[3] && neighs[3]->block[IX(x1, size-1, z1)]) occ++;
        } else {
          if (neighs[16] && neighs[16]->block[IX(x1, size-1, 0)]) occ++;
        }
      } else if (y1 < size) {
        if (z1 < 0) {
          if (neighs[5] && neighs[5]->block[IX(x1, y1, size-1)]) occ++;
        } else if (z1 < size) {
          printf("ERROR: x1=%d, y1=%d, z1=%d\n", x1, y1, z1);
        } else {
          if (neighs[4] && neighs[4]->block[IX(x1, y1, 0)]) occ++;
        }
      } else {
        if (z1 < 0) {
          if (neighs[15] && neighs[15]->block[IX(x1, 0, size-1)]) occ++;
        } else if (z1 < size) {
          if (neighs[2] && neighs[2]->block[IX(x1, 0, z1)]) occ++;
        } else {
          if (neighs[14] && neighs[14]->block[IX(x1, 0, 0)]) occ++;
        }
      }
    } else { // Cases [1], [18:25]
      if (y1 < 0) {
        if (z1 < 0) {
          if (neighs[25] && neighs[25]->block[IX(size-1, size-1, size-1)]) occ++;
        } else if (z1 < size) {
          if (neighs[24] && neighs[24]->block[IX(size-1, size-1, z1)]) occ++;
        } else {
          if (neighs[23] && neighs[23]->block[IX(size-1, size-1, 0)]) occ++;
        }
      } else if (y1 < size) {
        if (z1 < 0) {
          if (neighs[22] && neighs[22]->block[IX(size-1, y1, size-1)]) occ++;
        } else if (z1 < size) {
          if (neighs[1] && neighs[1]->block[IX(size-1, y1, z1)]) occ++;
        } else {
          if (neighs[21] && neighs[21]->block[IX(size-1, y1, 0)]) occ++;
        }
      } else {
        if (z1 < 0) {
          if (neighs[20] && neighs[20]->block[IX(size-1, 0, size-1)]) occ++;
        } else if (z1 < size) {
          if (neighs[19] && neighs[19]->block[IX(size-1, 0, z1)]) occ++;
        } else {
          if (neighs[18] && neighs[18]->block[IX(size-1, 0, 0)]) occ++;
        }
      }
    }
  } else {
      if (block[IX(x1, y1, z1)] != 0) occ++;
    }
  }
  return occ;
}

void Chunk::LoadDefault() {
  for (int x=0; x<size; x++) {
    for (int y=0; y<size; y++) {
      for (int z=0; z<size; z++) {
        int dx = size/2-x, dy=size/2-y, dz=size/2-z;
        if (dx*dx + dy*dy + dz*dz <= 10*10)
          block[IX(x,y,z)] = (x*101+y*47+z*119) % 255;
      }
    }
  }
}

void Chunk::Render(const glm::mat4& M) {
  if (tri_count < 1) return;
  glUseProgram(program);
  GLuint mLoc = glGetUniformLocation(program, "M");
  glUniformMatrix4fv(mLoc, 1, GL_FALSE, &(M[0][0]));
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, tri_count * 3);
  glBindVertexArray(0);
  glUseProgram(0);
}

void Chunk::Render() {
  glm::mat4 M(1);
  M = glm::translate(M, pos);
  Render(M);
}

#ifdef WIN32
void Chunk::Render_D3D11(const DirectX::XMMATRIX& M) {
  if (tri_count < 1) return;
  UpdateGlobalPerObjectCB(&M, nullptr, nullptr);
  unsigned stride = sizeof(float) * 6, offset = 0;
  g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_context11->IASetVertexBuffers(0, 1, &d3d11_vertex_buffer, &stride, &offset);
  g_context11->Draw(3 * tri_count, 0);
}

void Chunk::Render_D3D11() {
  DirectX::XMMATRIX M = DirectX::XMMatrixIdentity();
  M *= DirectX::XMMatrixTranslation(pos.x, pos.y, -pos.z);
  Render_D3D11(M);
}

void Chunk::RecordRenderCommand_D3D12(ChunkPass* pass, const DirectX::XMMATRIX& V, const DirectX::XMMATRIX& P) {
  pass->chunk_instances.push_back(this);
  DirectX::XMMATRIX M = DirectX::XMMatrixIdentity();
  M *= DirectX::XMMatrixTranslation(pos.x, pos.y, -pos.z);
  PerObjectCB cb;
  cb.M = M;
  cb.V = V;
  cb.P = P;
  pass->chunk_per_object_cbs.push_back(cb);
}

void Chunk::RecordRenderCommand_D3D12(ChunkPass* pass, const DirectX::XMMATRIX& M, const DirectX::XMMATRIX& V, const DirectX::XMMATRIX& P) {
  pass->chunk_instances.push_back(this);
  PerObjectCB cb;
  cb.M = M;
  cb.V = V;
  cb.P = P;
  pass->chunk_per_object_cbs.push_back(cb);
}

#endif

void Chunk::SetVoxel(unsigned x, unsigned y, unsigned z, int v) {
  block[IX(x,y,z)] = v;
  is_dirty = true;
}

int Chunk::GetVoxel(unsigned x, unsigned y, unsigned z) {
  return block[IX(x,y,z)];
}

Chunk::Chunk(Chunk& other) {
  is_dirty = true;
  pos = other.pos;
  tri_count = vao = vbo = 0;
  block = new unsigned char[size*size*size];
  light = new int[size*size*size];
  memcpy(block, other.block, sizeof(char)*size*size*size);
  memcpy(light, other.light, sizeof(int)*size*size*size);
}

void Chunk::Fill(int vox) {
  for (int i=0; i<size*size*size; i++) block[i] = vox;
  is_dirty = true;
}

void ChunkPass::AllocateConstantBuffers(int n) {
  num_max_chunks = n;
  CE(g_device12->CreateCommittedResource(
    &keep(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
    D3D12_HEAP_FLAG_NONE,
    &keep(CD3DX12_RESOURCE_DESC::Buffer(num_max_chunks * 256)),
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&d_per_object_cbs)));
}

void ChunkPass::EndPass() {
  CD3DX12_RANGE read_range(0, 256 * chunk_per_object_cbs.size());
  char* ptr;
  CE(d_per_object_cbs->Map(0, &read_range, (void**)&ptr));
  for (int i = 0; i<int(chunk_per_object_cbs.size()); i++) {
    memcpy(ptr + 256 * i, &(chunk_per_object_cbs[i]), sizeof(PerObjectCB));
  }
  d_per_object_cbs->Unmap(0, nullptr);
}

void ChunkPass::InitD3D12DefaultPalette() {
  // 本Pass所用的Root Signature与Pipeline State
  CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
  ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

  CD3DX12_ROOT_PARAMETER1 rootParameters[3];
  rootParameters[0].InitAsConstantBufferView(0, 0,  // per-object CB
    D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
  rootParameters[1].InitAsConstantBufferView(1, 0,  // per-scene CB
    D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);
  rootParameters[2].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MipLODBias = 0;
  sampler.MaxAnisotropy = 4;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  sampler.MinLOD = 0.0f;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderRegister = 0;
  sampler.RegisterSpace = 0;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc;
  root_sig_desc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler,
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
  ComPtr<ID3DBlob> signature, error;

  HRESULT hr = D3DX12SerializeVersionedRootSignature(&root_sig_desc,
    D3D_ROOT_SIGNATURE_VERSION_1_1,
    &signature, &error);
  if (signature == nullptr) {
    printf("Could not serialize root signature: %s\n",
      (char*)(error->GetBufferPointer()));
  }

  CE(g_device12->CreateRootSignature(0, signature->GetBufferPointer(),
    signature->GetBufferSize(), IID_PPV_ARGS(&root_signature_default_palette)));
  root_signature_default_palette->SetName(L"Chunk Pass Root Signature");

  ID3DBlob* default_palette_VS = nullptr;
  ID3DBlob* default_palette_PS = nullptr;
  {
    ID3DBlob* error = nullptr;
    unsigned compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

    const wchar_t* paths[] = {
      L"../shaders_hlsl/default_palette.hlsl",
      L"shaders_hlsl/default_palette.hlsl"
    };

    for (int i = 0; i < 2; i++) {
      const wchar_t* path = paths[i];
      D3DCompileFromFile(path, nullptr, nullptr,
        "VSMain", "vs_5_0", compile_flags, 0, &default_palette_VS, &error);
      if (error) {
        printf("Error compiling VS: %s\n", (char*)(error->GetBufferPointer()));
        continue;
      }

      D3DCompileFromFile(path, nullptr, nullptr,
        "PSMainWithShadow", "ps_5_0", compile_flags, 0, &default_palette_PS, &error);
      if (error) {
        printf("Error compiling PS: %s\n", (char*)(error->GetBufferPointer()));
        continue;
      }
    }
  }

  D3D12_INPUT_ELEMENT_DESC input_element_desc[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 1, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR"   , 2, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = root_signature_default_palette;
  pso_desc.VS = CD3DX12_SHADER_BYTECODE(default_palette_VS);
  pso_desc.PS = CD3DX12_SHADER_BYTECODE(default_palette_PS);
  pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  pso_desc.InputLayout.pInputElementDescs = input_element_desc;
  pso_desc.InputLayout.NumElements = 4;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 2;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
  pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pso_desc.SampleDesc.Count = 1;

  CE(g_device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state_default_palette)));

  pso_desc.NumRenderTargets = 0;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
  pso_desc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
  CE(g_device12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state_depth_only)));

  CE(default_palette_VS->Release());
  CE(default_palette_PS->Release());
}
