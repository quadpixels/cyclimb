// buffer1's layout
//
// [ N int's ] for ping
// [ N int's ] for pong
// [ num_blocks * blockDim.x ] for local block sums
// [ num_blocks * ways ] for global block sums
// [ num_blocks * ways ] for cumsum'ed global block sums
RWByteAddressBuffer buffer1 : register(u1);

// 1. CountBitPatterns
//    calls SingleBlockBlellochScan
// 2. ComputeBlockSums
// 3. Shuffle

cbuffer RadixSortCB : register(b0) {
  int offset_ping;
  int offset_pong;
  int offset_local_block_sums;
  int offset_global_block_sums;
  int iter;
  int num_threads_total;
  int N;
  int way;
  int gridDim_x;
  int shift_right;
};

groupshared int shmem[8192];  // Need to be less than 2*num_blocks

static const int NT = 4;

void SingleBlockBlellochScan(int ping, int pong, int threadIdx_x, int N) {
  //int ping = 0, pong = NT;  // Offsets

  GroupMemoryBarrierWithGroupSync();
  bool need_swap = true;

#define THE_BLELLOCH_LOOP \
  { \
    need_swap = !need_swap; \
    for (int j = threadIdx_x; j < N; j += NT) { \
      if (j >= dist && j < N) { \
        shmem[pong + j] = shmem[ping + j] + shmem[ping + j - dist]; \
      } \
      else if (j < dist) { \
        shmem[pong + j] = shmem[ping + j]; \
      } \
    } \
    int tmp = ping; ping = pong; pong = tmp; \
    GroupMemoryBarrierWithGroupSync(); \
  }

#if 0
  for (int dist = 1, iter = 0; dist < N; dist *= 2, iter++) {
    THE_BLELLOCH_LOOP
  }
#else
  int dist = 1;
  dist = 1;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 2;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 4;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 8;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 16;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 32;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 64;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 128;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 256;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 512;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 1024;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 2048;
  if (dist < N) THE_BLELLOCH_LOOP;
  dist = 4096;
  if (dist < N) THE_BLELLOCH_LOOP;
#endif

  if (need_swap) {
    for (int i = threadIdx_x; i < N; i += NT) {
      shmem[pong + i] = shmem[ping + i];
    }
  }
  GroupMemoryBarrierWithGroupSync();
}

[numthreads(NT, 1, 1)]
void CountBitPatterns(uint3 dispatch_tid : SV_DispatchThreadID, uint3 threadIdx : SV_GroupThreadID, uint3 blockIdx : SV_GroupID) {
  const int tidx = dispatch_tid.x;
  const int nthds = num_threads_total;
  const int ping = 0, pong = NT;  // shmem[NT] is cumsum
  
  const int num_blocks = (N - 1) / NT + 1;
  
  for (int i = blockIdx.x * NT, bidx = blockIdx.x; i < N; i += nthds, bidx += gridDim_x) {
    for (int bmask = 0; bmask < way; bmask++) {
      shmem[threadIdx.x] = 0;
      shmem[NT + threadIdx.x] = 0;
      GroupMemoryBarrierWithGroupSync();

      if (i + threadIdx.x < N) {
        int orig = buffer1.Load((i + threadIdx.x) * 4);
        int elt = (orig >> shift_right) & (way - 1);
        if (elt == bmask) {
          shmem[threadIdx.x] = 1;
        }
      }
      GroupMemoryBarrierWithGroupSync();

      // Single block blelloch scan
      SingleBlockBlellochScan(ping, pong, threadIdx.x, NT);

      if (i + threadIdx.x < N) {
        int orig = buffer1.Load((i + threadIdx.x) * 4);
        int elt = (orig >> shift_right) & (way - 1);
        if (elt == bmask) {
          int local_block_sum = (threadIdx.x > 0) ?
            shmem[pong + threadIdx.x - 1] : 0;
          buffer1.Store(offset_local_block_sums + (NT * bidx + threadIdx.x) * 4, local_block_sum);
        }
      }

      GroupMemoryBarrierWithGroupSync();
      if (threadIdx.x == 0) {
        int idx = num_blocks * bmask + bidx;
        buffer1.Store(offset_global_block_sums + idx * 4, shmem[pong + NT - 1]);
      }
      GroupMemoryBarrierWithGroupSync();
    }
  }
}

[numthreads(NT, 1, 1)]
void ComputeBlockSums(uint3 dispatch_tid : SV_DispatchThreadID, uint3 threadIdx : SV_GroupThreadID, uint3 blockIdx : SV_GroupID) {
  const int num_blocks = (N - 1) / NT + 1;
  const int ping = offset_global_block_sums / 4 + blockIdx.x * num_blocks;
  const int pong = ping + num_blocks * way;
  const int shmem_ping = 0;
  const int shmem_pong = num_blocks;

  for (int i = threadIdx.x; i < num_blocks; i += NT) {
    shmem[i] = buffer1.Load((ping + i) * 4);
  }
  GroupMemoryBarrierWithGroupSync();

  //buffer1.Store(ping * 4, 100000 + ping);
  //buffer1.Store(pong * 4, 100000 + pong);
  SingleBlockBlellochScan(shmem_ping, shmem_pong, threadIdx.x, num_blocks);

  for (int i = threadIdx.x; i < num_blocks; i += NT) {
    buffer1.Store((pong + i) * 4, shmem[i + shmem_pong]);
  }
  GroupMemoryBarrierWithGroupSync();
}

[numthreads(NT, 1, 1)]
void Shuffle(uint3 dispatch_tid : SV_DispatchThreadID, uint3 threadIdx : SV_GroupThreadID, uint3 blockIdx : SV_GroupID) {
  const int tidx = dispatch_tid.x;
  const int nthds = num_threads_total;
  const int num_blocks = (N - 1) / NT + 1;

  int offsets123[16];
  offsets123[0] = 0;
  for (int i = 1; i < way; i++) {
    offsets123[i] = buffer1.Load(offset_global_block_sums + 4 * (num_blocks * way + num_blocks * i - 1)) + offsets123[i - 1];
  }
  for (int i = blockIdx.x * NT, bidx = blockIdx.x; i < N; i += nthds, bidx += gridDim_x) {
    for (int bmask = 0; bmask < way; bmask++) {
      if (i + threadIdx.x < N) {
        int elt = (buffer1.Load((i + threadIdx.x) * 4) >> shift_right);
        elt = elt & (way - 1);
        if (elt == bmask) {
          const int lbs = buffer1.Load(offset_local_block_sums + (NT * bidx + threadIdx.x) * 4);
          int goffset = (bidx > 0) ? buffer1.Load(offset_global_block_sums + (num_blocks * bmask + bidx - 1) * 4) : 0;
          goffset += offsets123[bmask];
          int x = buffer1.Load(offset_ping + (i + threadIdx.x) * 4);
          //buffer1.Store(offset_pong + (goffset + lbs) * 4, 233333);// threadIdx.x);
          //buffer1.Store(offset_pong + (goffset + lbs) * 4, x);
          buffer1.Store(offset_pong + (i + threadIdx.x) * 4, (goffset));
        }
      }
    }
  }
}