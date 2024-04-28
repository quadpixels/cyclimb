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

int main() {
  InitDevice11();
  ID3D11ComputeShader* x = BuildComputeShader(L"shaders/shaders.hlsl", "kernel1");
  return 0;
}