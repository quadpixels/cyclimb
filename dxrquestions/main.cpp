// 2025-07-11
// DXR Questions

#include <stdexcept>
#include <source_location>
#include <stdio.h>
#include <Windows.h>

#include <d3dcompiler.h>
#include <dxcapi.h>
#include "../dx12helloworld/dxcapi.use.h"

#ifndef NDEBUG
#include "x64\Debug\g_PixelShader.h"
#include "x64\Debug\g_VertexShader.h"
#else
#include "x64\Release\g_PixelShader.h"
#include "x64\Release\g_VertexShader.h"
#endif

#include <d3d12.h>
#include <dxgi1_4.h>
#include <nvapi.h>

#include "MyFramework.h"

#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}

static dxc::DxcDllSupport g_dxc_support;
namespace dxc {
  char const* kDxCompilerLib = "dxcompiler";
}

MyFramework* g_myframework{};

void InitNVAPI() {
  NvAPI_Status status = NvAPI_Initialize();
  if (status == NVAPI_OK) {
    printf("NVAPI inited.\n");
  }
  else {
    printf("NVAPI init failed = %d\n", (int)status);
    return;
  }
  size_t lss_data_size = sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC);
  printf("sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC) = %zu\n", lss_data_size);
  NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAPS lssCaps = NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAP_NONE;
  NvAPI_D3D12_GetRaytracingCaps(g_myframework->GetDevice(), NVAPI_D3D12_RAYTRACING_CAPS_TYPE_LINEAR_SWEPT_SPHERES, &lssCaps, sizeof(NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAPS));
  printf("lssCaps = %u, %s\n",
    (uint32_t)(lssCaps),
    lssCaps == NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAP_NONE ? "LSS not supported" : "LSS supported");
}

void InitDX12Stuff() {
  g_dxc_support.Initialize();
  IDxcCompiler* dxc_compiler;
  IDxcOperationResult* dxc_opr_result;
  CE(g_dxc_support.CreateInstance(CLSID_DxcCompiler, &dxc_compiler));

  g_myframework = new MyFramework();
  g_myframework->InitWindow();
  g_myframework->InitDeviceAndCommandQ();
}

void CreateMyWindow() {
  AllocConsole();
  freopen_s((FILE**)stdin, "CONIN$", "r", stderr);
  freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
  freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
}

int main() {
  CreateMyWindow();

  InitDX12Stuff();
  InitNVAPI();

  MSG msg = { 0 };
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  return 0;
}