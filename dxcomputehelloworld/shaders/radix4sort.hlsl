// buffer1's layout
//
// [ N int's ] for ping
// [ N int's ] for pong
// [ num_blocks * blockDim.x ] for local block sums
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

groupshared int shmem[128];

static const int NT = 4;

void SingleBlockBlellochScan(int threadIdx_x, int N) {
  int ping = 0, pong = NT;  // Offsets

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
      int tmp = ping; ping = pong; pong = tmp; \
    } \
    GroupMemoryBarrierWithGroupSync(); \
  }

#if 1
  for (int dist = 1, iter = 0; dist < N; dist *= 2, iter++) {
    THE_BLELLOCH_LOOP
  }
#else
  int dist = 1;
  dist = 1;
  THE_BLELLOCH_LOOP;
  dist = 2;
  THE_BLELLOCH_LOOP;
  dist = 4;
  THE_BLELLOCH_LOOP;
  dist = 8;
  THE_BLELLOCH_LOOP;
  dist = 16;
  THE_BLELLOCH_LOOP;
  dist = 32;
  THE_BLELLOCH_LOOP;
  dist = 64;
  THE_BLELLOCH_LOOP;
#endif

  if (need_swap) {
    for (int i = threadIdx_x; i < N; i += NT) {
      shmem[pong + i] = shmem[ping + i];
    }
  }
  GroupMemoryBarrierWithGroupSync();
}

[numthreads(4, 1, 1)]
void CountBitPatterns(uint3 dispatch_tid : SV_DispatchThreadID, uint3 threadIdx : SV_GroupThreadID, uint3 blockIdx : SV_GroupID) {
  const int tidx = dispatch_tid.x;
  const int nthds = num_threads_total;
  const int pong = NT;  // shmem[NT] is cumsum
  
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
      SingleBlockBlellochScan(threadIdx.x, NT);

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
