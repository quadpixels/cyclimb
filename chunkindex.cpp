#include "chunkindex.hpp"
#include "chunk.hpp"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <set>

unsigned DivUp(unsigned a, unsigned b) {
  return (a-1) / b + 1;
}

ChunkIndex::ChunkIndex(unsigned _xlen, unsigned _ylen, unsigned _zlen) {
  x_len = _xlen; y_len = _ylen; z_len = _zlen;
}

ChunkGrid::ChunkGrid(unsigned _xlen, unsigned _ylen, unsigned _zlen)
  : ChunkIndex(_xlen, _ylen, _zlen) {
  Init(_xlen, _ylen, _zlen);
}

void ChunkGrid::Render(
    const glm::vec3& pos,         const glm::vec3& scale,
    const glm::mat3& orientation, const glm::vec3& anchor) {
  glm::mat4 M(orientation);

  M = glm::scale(M, scale);
  M = glm::translate(M, glm::inverse(orientation) * pos / scale);
  M = glm::translate(M, -anchor);

  for (int xx=0; xx < xdim; xx++) {
    for (int yy=0; yy < ydim; yy++) {
      for (int zz=0; zz < ydim; zz++) {
        glm::vec3 tr(float(xx * Chunk::size),
                     float(yy * Chunk::size),
                     float(zz * Chunk::size));
        glm::mat4 M_chunk = glm::translate(M, tr);
        int ix = IX(xx, yy, zz);
        Chunk* chk = chunks[ix];
        if (chk->is_dirty) {
          Chunk* neighs[26] = { NULL };
          GetNeighbors(chk, neighs);
          chk->BuildBuffers(neighs);
        }
        chunks[ix]->Render(M_chunk);
      }
    }
  }
}

#ifdef WIN32
extern void GlmMat4ToDirectXMatrix(DirectX::XMMATRIX* out, const glm::mat4& m);
void ChunkGrid::Render_D3D11(
  const glm::vec3& pos,  const glm::vec3& scale,
  const glm::mat3& orientation,  const glm::vec3& anchor) {

  glm::vec3 pos11 = pos;
  pos11.z *= -1;

  glm::mat3 orientation1 = orientation;
  //orientation1[2][0] *= -1;
  //orientation1[2][1] *= -1;
  //orientation1[2][2] *= -1;

  glm::mat4 M(orientation1);

  /*
  M = glm::scale(M, scale);
  
  M = glm::translate(M, t);
  glm::vec3 anchor1 = anchor; anchor1.z = -anchor1.z;
  M = glm::translate(M, anchor1);
  */

  const float eps = 0.01f;
  M = glm::scale(M, scale + glm::vec3(eps, eps, eps));
  glm::vec3 t = glm::inverse(orientation1) * pos11 / scale;

  M = glm::translate(M, t);
  glm::vec3 anchor1 = anchor; anchor1.z = -anchor1.z;
  M = glm::translate(M, -anchor1);

  for (int xx = 0; xx < xdim; xx++) {
    for (int yy = 0; yy < ydim; yy++) {
      for (int zz = 0; zz < zdim; zz++) {
        glm::vec3 tr(float(xx * Chunk::size),
          float(yy * Chunk::size),
          float(zz * Chunk::size) * -1);
        glm::mat4 M_chunk = glm::translate(M, tr);
        int ix = IX(xx, yy, zz);
        Chunk* chk = chunks[ix];
        if (chk->is_dirty) {
          Chunk* neighs[26] = { NULL };
          GetNeighbors(chk, neighs);
          chk->BuildBuffers(neighs);
        }

        DirectX::XMMATRIX M1;
        GlmMat4ToDirectXMatrix(&M1, M_chunk);
        chunks[ix]->Render_D3D11(M1);
      }
    }
  }
}

void ChunkGrid::RecordRenderCommand_D3D12(
  ChunkPass* chunk_pass,
  const glm::vec3& pos,
  const glm::vec3& scale,
  const glm::mat3& orientation,
  const glm::vec3& anchor,
  const DirectX::XMMATRIX& V,
  const DirectX::XMMATRIX& P) {

  glm::vec3 pos11 = pos;
  pos11.z *= -1;

  glm::mat3 orientation1 = orientation;
  glm::mat4 M(orientation1);

  const float eps = 0.01f;
  M = glm::scale(M, scale + glm::vec3(eps, eps, eps));
  glm::vec3 t = glm::inverse(orientation1) * pos11 / scale;

  M = glm::translate(M, t);
  glm::vec3 anchor1 = anchor; anchor1.z = -anchor1.z;
  M = glm::translate(M, -anchor1);

  for (int xx = 0; xx < xdim; xx++) {
    for (int yy = 0; yy < ydim; yy++) {
      for (int zz = 0; zz < zdim; zz++) {
        glm::vec3 tr(float(xx * Chunk::size),
          float(yy * Chunk::size),
          float(zz * Chunk::size) * -1);
        glm::mat4 M_chunk = glm::translate(M, tr);
        int ix = IX(xx, yy, zz);
        Chunk* chk = chunks[ix];
        if (chk->is_dirty) {
          Chunk* neighs[26] = { NULL };
          GetNeighbors(chk, neighs);
          chk->BuildBuffers(neighs);
        }

        DirectX::XMMATRIX M1;
        GlmMat4ToDirectXMatrix(&M1, M_chunk);
        chunks[ix]->RecordRenderCommand_D3D12(chunk_pass, M1, V, P);
      }
    }
  }
}
#endif

Chunk* ChunkGrid::GetChunk(int x, int y, int z, int* local_x, int* local_y, int* local_z) {
  int xx = x / Chunk::size,
      yy = y / Chunk::size,
      zz = z / Chunk::size;
  if (local_x)
    *local_x = x % Chunk::size;
  if (local_y)
    *local_y = y % Chunk::size;
  if (local_z)
    *local_z = z % Chunk::size;
  if (xx < xdim && yy < ydim && zz < zdim && xx >= 0 && yy >= 0 && zz >= 0) {
    Chunk* chk = chunks.at(IX(xx,yy,zz));
    return chk;
  } else return NULL;
}

void ChunkGrid::SetVoxel(unsigned x, unsigned y, unsigned z, int v) {
  int lx, ly, lz;
  Chunk* chk = GetChunk(x, y, z, &lx, &ly, &lz);
  if (chk)
    chk->SetVoxel(lx, ly, lz, v);
}

void ChunkGrid::SetVoxel(const glm::vec3& p, int vox) {
  int xx = int(p.x / Chunk::size),
      yy = int(p.y / Chunk::size),
      zz = int(p.z / Chunk::size),
      local_x = int(p.x) % Chunk::size,
      local_y = int(p.y) % Chunk::size,
      local_z = int(p.z) % Chunk::size;
  int ix = IX(xx, yy, zz);
  if (ix >= 0 && ix < chunks.size()) {
    Chunk* chk = chunks.at(ix);
    chk->SetVoxel(local_x, local_y, local_z, vox);
    Chunk* neighs[26] = { NULL };
    GetNeighbors(chk, neighs);
    chk->BuildBuffers(neighs);
  }
}

int ChunkGrid::GetVoxel(unsigned x, unsigned y, unsigned z) {
  unsigned xx = x / Chunk::size,
      yy = y / Chunk::size,
      zz = z / Chunk::size,
      local_x = x % Chunk::size,
      local_y = y % Chunk::size,
      local_z = z % Chunk::size;
  if (xx < xdim && yy < ydim && zz < zdim) {
    Chunk* chk = chunks.at(IX(xx,yy,zz));
    return chk->GetVoxel(local_x, local_y, local_z);
  } else return 0;
}

bool ChunkGrid::GetNeighbors(Chunk* chunk, Chunk* neighs[26]) {
  int xx, yy, zz;
  FromIX(chunk->idx, xx, yy, zz);
  const int xyzs[] = {
    IX(xx + 1, yy, zz),      // [0]
    IX(xx - 1, yy, zz),      // [1]
    IX(xx, yy + 1, zz),      // [2]
    IX(xx, yy - 1, zz),      // [3]
    IX(xx, yy, zz + 1),      // [4]
    IX(xx, yy, zz - 1),      // [5]
    IX(xx + 1, yy + 1, zz + 1),  // [6]
    IX(xx + 1, yy + 1, zz),    // [7]
    IX(xx + 1, yy + 1, zz - 1),  // [8]
    IX(xx + 1, yy, zz + 1),    // [9]
    IX(xx + 1, yy, zz - 1),    // [10]
    IX(xx + 1, yy - 1, zz + 1),  // [11]
    IX(xx + 1, yy - 1, zz),    // [12]
    IX(xx + 1, yy - 1, zz - 1),  // [13]
    IX(xx, yy + 1, zz + 1),    // [14]
    IX(xx, yy + 1, zz - 1),    // [15]
    IX(xx, yy - 1, zz + 1),    // [16]
    IX(xx, yy - 1, zz - 1),    // [17]
    IX(xx - 1, yy + 1, zz + 1),  // [18]
    IX(xx - 1, yy + 1, zz),    // [19]
    IX(xx - 1, yy + 1, zz - 1),  // [20]
    IX(xx - 1, yy, zz + 1),    // [21]
    IX(xx - 1, yy, zz - 1),    // [22]
    IX(xx - 1, yy - 1, zz + 1),  // [23]
    IX(xx - 1, yy - 1, zz),    // [24]
    IX(xx - 1, yy - 1, zz - 1),  // [25]
  };
  const unsigned xyzdim = xdim * ydim * zdim;
  for (int i = 0; i < 26; i++) {
    const unsigned xyz = xyzs[i];
    if (xyz < xyzdim) { neighs[i] = chunks[xyz]; }
  }
  return true;
}

void ChunkGrid::Init(unsigned _xlen, unsigned _ylen, unsigned _zlen) {
  x_len = _xlen; y_len = _ylen; z_len = _zlen;
  for (Chunk* c : chunks) {
    delete c;
  }
  chunks.clear();

  xdim = DivUp(_xlen, Chunk::size);
  ydim = DivUp(_ylen, Chunk::size);
  zdim = DivUp(_zlen, Chunk::size);

  unsigned xyzdim = xdim * ydim * zdim;
  chunks.resize(xyzdim);
  for (unsigned i=0; i<xyzdim; i++) {
    chunks[i] = new Chunk();
    chunks[i]->idx = i;
  }
}

ChunkGrid::ChunkGrid(const char* vox_fn) {
  FILE* f;
  fopen_s(&f, vox_fn, "rb");

  long long file_size;
  fseek(f, 0, SEEK_END);
  file_size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char magic[4];
  assert (fread(magic, sizeof(char), 4, f) == 4);
  assert (magic[0] == 'V');
  assert (magic[1] == 'O');
  assert (magic[2] == 'X');
  assert (magic[3] == ' ');

  int ver;
  assert (fread(&ver, sizeof(int), 1, f) == 1);

  bool done = false; // For Ver >= 150 read voxels discard everything else

  int curr_size[3]; // X Y Z
  while (ftell(f) < file_size && done == false) {
    char chunk[5];
    assert (fread(chunk, sizeof(char), 4, f) == 4);
    chunk[4] = 0x00;
    int size_content, size_children;
    assert (fread(&size_content, sizeof(int), 1, f) == 1);
    assert (fread(&size_children, sizeof(int), 1, f) == 1);

    if (!strcmp(chunk, "MAIN")) {
      continue; // Read next chunk
    } else if (!strcmp(chunk, "PACK")) {
      int n;
      assert (fread(&n, sizeof(int), 1, f) == 1);
    } else if (!strcmp(chunk, "SIZE")) {
      assert (fread(curr_size, sizeof(int), 3, f) == 3);
    } else if (!strcmp(chunk, "XYZI")) {
      unsigned num_voxels;
      assert (fread(&num_voxels, sizeof(int), 1, f) == 1);
      printf("File: %s, version=%d, size=%ld B, %u voxels, %d x %d x %d\n",
        vox_fn, ver, file_size,
        num_voxels, curr_size[0], curr_size[1], curr_size[2]);

      // Y and Z are swapped
      Init(curr_size[0], curr_size[2], curr_size[1]);

      int* xyzi = new int[num_voxels];
      assert (fread(xyzi, sizeof(int), num_voxels, f) == num_voxels);
      for (unsigned i=0; i<num_voxels; i++) {
        int tmp = xyzi[i],
            val = ((tmp & 0xFF000000) >> 24),
            y = (tmp & 0x00FF0000) >> 16,
            z = curr_size[1] - ((tmp & 0x0000FF00) >> 8),
            x = tmp & 0xFF;
        SetVoxel(x, y, z, val);
      }
      delete xyzi;
      done = true;
    } else if (!strcmp(chunk, "RGBA")) {
      printf("RGBA Palette, skipped\n");
      assert (0 == fseek(f, 256 * sizeof(int), SEEK_CUR));
    } else if (!strcmp(chunk, "MATT")) {
      int mat_id, mat_type, property;
      float weight;
      assert (fread(&mat_id, sizeof(int), 1, f) == 1);
      assert (fread(&mat_type, sizeof(int), 1, f) == 1);
      assert (fread(&weight, sizeof(float), 1, f) == 1);
      assert (fread(&property, sizeof(int), 1, f) == 1);
      int p = property; unsigned nbits = 0;
      for (int i=0; i<8; i++) {
        if (p & 1) nbits ++;
        p >>= 1;
      }
      float property_value[8];
      assert (fread(property_value, sizeof(int), nbits, f) == nbits);
    }
  }
  fclose(f);
}

bool ChunkGrid::IntersectPoint(const glm::vec3& p) {
  return false;
}

ChunkGrid::ChunkGrid(const ChunkGrid& other) {
  chunks.resize(other.chunks.size());
  for (unsigned i=0; i<chunks.size(); i++) {
    chunks[i] = new Chunk(*(other.chunks.at(i)));
  }
  xdim = other.xdim; ydim = other.ydim; zdim = other.zdim;
  x_len = other.x_len; y_len = other.y_len; z_len = other.z_len;
}

void ChunkGrid::Fill(int vox) {
  std::set<Chunk*> dirty;
  for (unsigned x=0; x<x_len; x++) {
    int xx = int(x / Chunk::size);
    for (unsigned y=0; y<y_len; y++) {
      int yy = int(y / Chunk::size);
      for (unsigned z=0; z<z_len; z++) {
        int zz = int(z / Chunk::size);
        int local_x = x % Chunk::size,
            local_y = y % Chunk::size,
            local_z = z % Chunk::size;
        int ix = IX(xx, yy, zz);
        if (ix >= 0 && ix < chunks.size()) {
          Chunk* chk = chunks.at(ix);
          chk->SetVoxel(local_x, local_y, local_z, vox);
          dirty.insert(chk);
        }
      }
    }
  }
  for (Chunk* c : dirty) {
    Chunk* dummy[26] = { NULL };
    c->BuildBuffers(dummy);
  }
}

void ChunkGrid::SetVoxelSphere(const glm::vec3& p, float radius, int v) {
  std::set<Chunk*> touched;
  int r = int(radius) + 1;
  for (int dx = -r; dx <= r; dx ++) {
    for (int dy = -r; dy <= r; dy ++) {
      for (int dz = -r; dz <= r; dz ++) {
        const float dsq = dx*dx + dy*dy + dz*dz;
        if (dsq <= radius * radius) {
          int lx, ly, lz;
          Chunk* chk = GetChunk(int(p.x+dx), int(p.y+dy), int(p.z+dz),
              &lx, &ly, &lz);
          touched.insert(chk);
          chk->SetVoxel(lx, ly, lz, v);
        }
  } } }
  for (Chunk* c : touched) {
    Chunk* dummy[26] = { NULL };
    c->BuildBuffers(dummy);
  }
}

void ChunkGrid::FromIX(int ix, int& x, int& y, int& z) {
  x = ix / (ydim * zdim);
  ix -= x * ydim * zdim;
  y = ix / zdim;
  z = ix % zdim;
}
