#include <assert.h>
#include <stdio.h>

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <exception>

void CE(HRESULT x) {
  if (FAILED(x)) {
    printf("ERROR: %X\n", x);
    throw std::exception();
  }
}

ID3D11Device* g_device11;
ID3D11DeviceContext* g_context11;

void CornellBoxTest();

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
  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
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

int main(int argc, char** argv) {
  InitDevice11();

  if (argc > 1) {
    if (!strcmp(argv[1], "cornellbox")) {
      CornellBoxTest();
    }
    else {
      CounterTest();
    }
  }

  return 0;
}