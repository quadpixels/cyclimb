#include <assert.h>
#include <stdio.h>

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <exception>
#include <random>
#include <vector>

void CE(HRESULT x) {
  if (FAILED(x)) {
    printf("ERROR: %X\n", x);
    throw std::exception();
  }
}

ID3D11Device* g_device11;
ID3D11DeviceContext* g_context11;

void CornellBoxTest();
void RadixSortTest();

void InitDevice11() {
  const D3D_FEATURE_LEVEL levels[] = {
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
  };

  UINT flags = 0;
  flags |= D3D11_CREATE_DEVICE_DEBUG;

  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, _countof(levels),
    D3D11_SDK_VERSION, &device, nullptr, &context);

  if (FAILED(hr)) {
    printf("Failed to create D3D11 device: %08X\n", hr);
    exit(1);
  }

  if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) {
    D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS hwopts = { 0 };
    device->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &hwopts, sizeof(hwopts));
    if (!hwopts.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x) {
      device->Release();
      printf("DirectCompute not supported via ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4\n");
      exit(1);
    }
  }

  printf("DX11 device and context created.\n");
  g_device11 = device;
  g_context11 = context;
}

ID3D11ComputeShader* BuildComputeShader(LPCWSTR src_file, LPCSTR entry_point) {
  UINT flags = 0;// D3DCOMPILE_ENABLE_STRICTNESS;
  LPCSTR profile = (g_device11->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0) ? "cs_5_0" : "cs_4_0";
  ID3DBlob* shader_blob = nullptr, * error_blob = nullptr;
  HRESULT hr = D3DCompileFromFile(src_file, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry_point, profile, flags, 0, &shader_blob, &error_blob);
  if (FAILED(hr)) {
    if (error_blob) {
      OutputDebugStringA((char*)error_blob->GetBufferPointer());
      error_blob->Release();
    }
    if (shader_blob) {
      shader_blob->Release();
    }
  }
  ID3D11ComputeShader* ret = nullptr;
  hr = g_device11->CreateComputeShader(shader_blob->GetBufferPointer(), shader_blob->GetBufferSize(), nullptr, &ret);
  if (FAILED(hr)) {
    printf("Failed to create compute shader\n");
    assert(0);
  }
  if (error_blob) error_blob->Release();
  shader_blob->Release();
  printf("Created CS in file %ls with entrypoint %s\n", src_file, entry_point);
  return ret;
}

ID3D11Buffer* CreateRawBuffer(int size) {
  D3D11_BUFFER_DESC desc{};
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  desc.ByteWidth = size;
  desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Buffer* ret;
  CE(g_device11->CreateBuffer(&desc, nullptr, &ret));
  printf("Created a raw buffer with size %d\n", size);
  return ret;
}

ID3D11Buffer* CreateRawBufferCPUWriteable(int size) {
  D3D11_BUFFER_DESC desc{};
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.ByteWidth = size;
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.MiscFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ID3D11Buffer* ret;
  CE(g_device11->CreateBuffer(&desc, nullptr, &ret));
  printf("Created a CPU-writable raw buffer with size %d\n", size);
  return ret;
}

ID3D11Buffer* CreateStructuredBuffer(UINT element_size, UINT count) {
  ID3D11Buffer* ret = nullptr;
  D3D11_BUFFER_DESC desc = {};
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
  desc.ByteWidth = element_size * count;
  desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  desc.StructureByteStride = element_size;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  CE(g_device11->CreateBuffer(&desc, nullptr, &ret));
  printf("Created a structured buffer with size %dx%d=%d\n", element_size, count, desc.ByteWidth);
  return ret;
}


ID3D11UnorderedAccessView* CreateBufferUAV(ID3D11Buffer* buffer) {
  D3D11_BUFFER_DESC buf_desc = { };
  buffer->GetDesc(&buf_desc);
  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.FirstElement = 0;
  if (buf_desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) {
    uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    uav_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    uav_desc.Buffer.NumElements = buf_desc.ByteWidth / 4;
  }
  else if (buf_desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) {
    uav_desc.Format = DXGI_FORMAT_UNKNOWN;
    uav_desc.Buffer.NumElements = buf_desc.ByteWidth / buf_desc.StructureByteStride;
  }
  ID3D11UnorderedAccessView* ret;
  CE(g_device11->CreateUnorderedAccessView(buffer, &uav_desc, &ret));
  printf("Created UAV for a buffer of size %d\n", buf_desc.ByteWidth);
  return ret;
}

int RoundUpToAlign(int x, int align) {
  if (x % align != 0) {
    x += align - (x % align);
  }
  return x;
}

ID3D11Buffer* CreateConstantBuffer(int size) {
  ID3D11Buffer* ret = nullptr;
  D3D11_BUFFER_DESC cb_desc{};
  cb_desc.ByteWidth = RoundUpToAlign(size, 16);
  cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cb_desc.Usage = D3D11_USAGE_DYNAMIC;
  cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  CE(g_device11->CreateBuffer(&cb_desc, nullptr, &ret));
  printf("Created a constant buffer of size %d\n", cb_desc.ByteWidth);
  return ret;
}

void CounterTest() {
  ID3D11ComputeShader* x = BuildComputeShader(L"shaders/shaders.hlsl", "kernel1");

  ID3D11Buffer* buf0 = CreateRawBuffer(256);
  ID3D11UnorderedAccessView* uav0 = CreateBufferUAV(buf0);
  ID3D11UnorderedAccessView* null_uav = nullptr;
  unsigned zero4[] = { 0,0,0,0 };
  g_context11->ClearUnorderedAccessViewUint(uav0, zero4);
  g_context11->CSSetShader(x, nullptr, 0);
  g_context11->CSSetUnorderedAccessViews(0, 1, &uav0, nullptr);
  g_context11->Dispatch(1, 1, 1);
  g_context11->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  g_context11->Map(buf0, 0, D3D11_MAP_READ, 0, &mapped);
  int val = *((int*)mapped.pData);
  g_context11->Unmap(buf0, 0);
  printf("counter=%d\n", val);

  x->Release();
  buf0->Release();
  uav0->Release();
}

struct RadixSortCB {
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
void RadixSortTest() {
  const int N = 20;
  const int way = 4;
  const int gridDim_x = 1;
  const int blockDim_x = 4;
  int num_blocks = (N - 1) / blockDim_x + 1;
  int tot_sz = N * 2 + num_blocks * blockDim_x + num_blocks * way;

  ID3D11Buffer* buf1 = CreateRawBuffer(tot_sz * 4);
  ID3D11Buffer* buf1_cpu = CreateRawBufferCPUWriteable(tot_sz * 4);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  g_context11->Map(buf1_cpu, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  std::vector<int> d_h = { 1,2,0,3, 0,1,1,0, 3,3,3,2, 1,2,2,0, 2,0,0,2 };
  /*
  for (int i = 0; i < N; i++) {
    d_h.push_back(i);
  }
  */
  std::random_device rd;
  std::mt19937 g(rd());
  //std::shuffle(d_h.begin(), d_h.end(), g);
  printf("Input:");
  for (int i = 0; i < N; i++) {
    printf(" %d", d_h[i]);
  }
  printf("\n");
  memcpy(mapped.pData, d_h.data(), sizeof(int) * N);
  g_context11->Unmap(buf1_cpu, 0);
  g_context11->CopyResource(buf1, buf1_cpu);

  ID3D11UnorderedAccessView* uav1 = CreateBufferUAV(buf1);

  ID3D11ComputeShader* s = BuildComputeShader(L"shaders/radix4sort.hlsl", "CountBitPatterns");

  RadixSortCB cb{};
  cb.offset_ping = 0;  // N elements
  cb.offset_pong = 4 * N;  // N elements
  cb.iter = 0;
  cb.num_threads_total = gridDim_x * blockDim_x;
  cb.offset_local_block_sums = 2 * (4 * N);  // blockDim.x * gridDim.x elements
  cb.offset_global_block_sums = cb.offset_local_block_sums + 4 * (num_blocks * blockDim_x);  // num_blocks
  cb.N = N;
  cb.way = 4;
  cb.gridDim_x = gridDim_x;
  cb.shift_right = 0;

  ID3D11Buffer* radixsort_cb = CreateConstantBuffer(sizeof(RadixSortCB));
  g_context11->Map(radixsort_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  memcpy(mapped.pData, &cb, sizeof(cb));
  g_context11->Unmap(radixsort_cb, 0);

  g_context11->CSSetShader(s, nullptr, 0);
  g_context11->CSSetUnorderedAccessViews(1, 1, &uav1, nullptr);
  g_context11->CSSetConstantBuffers(0, 1, &radixsort_cb);
  g_context11->Dispatch(gridDim_x, 1, 1);

  {
    int* tmp = new int[tot_sz];
    g_context11->Map(buf1, 0, D3D11_MAP_READ, 0, &mapped);
    memcpy(tmp, mapped.pData, sizeof(int) * tot_sz);
    printf("tmp:");
    printf("\nN:");
    for (int i = 0; i < N; i++) {
      if (i % 16 == 0) {
        printf("\n[%d]:", i);
      }
      printf(" %d", tmp[i]);
    }
    printf("\n");
    printf("N #2:");
    for (int i = 0; i < N; i++) {
      if (i % 16 == 0) {
        printf("\n[%d]:", i);
      }
      printf(" %d", tmp[i+N]);
    }
    printf("\n");
    printf("Local block sums:");
    for (int i = 0; i < num_blocks * blockDim_x; i++) {
      printf(" %d", tmp[2 * N + i]);
    }
    printf("\nGlobal block sums:");
    for (int i = 0; i < num_blocks * way; i++) {
      if (i % num_blocks == 0) {
        printf("\n%d:", i / num_blocks);
      }
      printf(" %d", tmp[2 * N + num_blocks * blockDim_x + i]);
    }
    printf("\n");
    g_context11->Unmap(buf1, 0);
    delete[] tmp;
  }
}

int main(int argc, char** argv) {
  InitDevice11();

  if (argc > 1) {
    if (!strcmp(argv[1], "cornellbox")) {
      CornellBoxTest();
    }
    else if (!strcmp(argv[1], "radixsort")) {
      RadixSortTest();
    }
    else {
      CounterTest();
    }
  }

  return 0;
}