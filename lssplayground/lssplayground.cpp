// LSS PLAYGROUND.

#include <iostream>
#include <source_location>

#include <GLFW/glfw3.h>
#include <glfw/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include "../dxrquestions/MyFramework.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_dx12.h"

#ifdef NDEBUG
#include "x64/Release/lss_nvapi.hlsl.h"
#else
#include "x64/Debug/lss_nvapi.hlsl.h"
#endif

MyFramework* g_myframework{};
GLFWwindow* g_window{};
uint32_t WIN_W = 960, WIN_H = 480;
uint32_t VIEW_W = 320, VIEW_H = 320;

ID3D12RootSignature* g_raytracing_rootsig{};
MyRtPipeline g_myrtpipeline{};
ID3D12Resource* g_dxr_output_resource{};
ID3D12DescriptorHeap* g_srv_uav_cbv_heap{}, * g_srv_uav_cbv_heap_cpu{};
ImTextureID g_rt_output_imgui_texid{};

#ifndef CE
#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}
#endif

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  return 0;
};

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
  if (action == GLFW_PRESS)
  {
    switch (key)
    {
    case GLFW_KEY_ESCAPE:
      glfwTerminate();
      exit(0);
      break;
    }
  }
}

void CreateLSSPlaygroundWindow() {
  if (!glfwInit()) {
    printf("Oh! Could not initialize GLFW.\n");
    exit(1);
  }
  printf("GLFW inited.\n");
  GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* video_mode = glfwGetVideoMode(primary_monitor);
  printf("Video mode of primary monitor is %dx%d\n", video_mode->width, video_mode->height);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  g_window = glfwCreateWindow(WIN_W, WIN_H, "MyRraLoader", nullptr, nullptr);
  glfwSetKeyCallback(g_window, KeyCallback);
}

void RenderImGui(ID3D12GraphicsCommandList4* command_list) {
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGui::SetNextWindowSize(ImVec2(320, 360), ImGuiCond_Once);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Once);

  ImGui::Begin("RRA Playground.");
  ImGui::Text("Hey.");
  ImGui::End();

  ImGui::SetNextWindowSize(ImVec2(320, 360), ImGuiCond_Once);
  ImGui::SetNextWindowPos(ImVec2(320, 0), ImGuiCond_Once);

  ImGui::Begin("Output 1");
  ImGui::Image(g_rt_output_imgui_texid, ImVec2(300, 300));
  ImGui::End();
  
  ImGui::Render();
  command_list->SetDescriptorHeaps(1, &(g_myframework->imgui_heap));
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);
}

void Render() {
  D3D12_CPU_DESCRIPTOR_HANDLE handle_rtv = g_myframework->GetCurrRenderTargetCPUDescriptor();
  ID3D12CommandAllocator* command_allocator = g_myframework->GetCommandAllocator();
  ID3D12GraphicsCommandList4* command_list = g_myframework->GetGraphicsCommandList();
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));
  
  // Render target 1
  command_list->SetComputeRootSignature(g_raytracing_rootsig);
  command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)(&g_srv_uav_cbv_heap));
  D3D12_GPU_DESCRIPTOR_HANDLE handle_table = g_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart();
  command_list->SetComputeRootDescriptorTable(0, handle_table);
  command_list->SetPipelineState1(g_myrtpipeline.rt_state_object);
  D3D12_DISPATCH_RAYS_DESC* drd = &(g_myrtpipeline.dispatch_rays_desc);
  drd->Width = VIEW_W;
  drd->Height = VIEW_H;
  drd->Depth = 1;
  ResourceBarrierTransition(command_list, g_dxr_output_resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  command_list->DispatchRays(drd);
  ResourceBarrierTransition(command_list, g_dxr_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  // Render target 2

  // Overall render target
  float bg_color[] = { 0.15f, 0.15f, 0.15f, 1.0f };
  ID3D12Resource* rendertarget = g_myframework->GetCurrentRenderTarget();
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  command_list->OMSetRenderTargets(1, &handle_rtv, true, nullptr);

  RenderImGui(command_list);

  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

  CE(command_list->Close());
  ID3D12CommandQueue* command_queue = g_myframework->GetCommandQueue();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(g_myframework->Present());
  g_myframework->WaitForPreviousFrame();
}

void InitPipeline() {
  g_myframework->CreateRtGlobalRootSig(&g_raytracing_rootsig, 1, 1, 1, true, 999);

  MyFramework::MyRtShaderListInfo sli{};
  sli.dxil_lib_bytecode = static_cast<void*>(const_cast<uint8_t*>(g_LssNvapiShader));
  sli.dxil_lib_length = sizeof(g_LssNvapiShader);
  sli.raygen_shader = L"RayGen";
  sli.miss_shader = L"Miss";
  sli.closest_hit_shader = L"ClosestHit";
  sli.hitgroup_name = L"HitGroup";
  g_myframework->CreateMyRtPipeline(&g_myrtpipeline, g_raytracing_rootsig, sli);
}

void InitResources() {
  uint32_t srv_uav_cbv_descriptor_size = g_myframework->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  g_myframework->CreateRtOutputResource(&g_dxr_output_resource, VIEW_W, VIEW_H);
  // |For RT in RenderTarget 1                                | RenderTgt 1 in ImGui |
  //  [Rendertarget 1 UAV] [NVAPI UAV] [AS SRV] [PerScene CBV] [Rendertaget 1 SRV]    
  g_myframework->CreateCBVSRVUAVHeap(&g_srv_uav_cbv_heap, &g_srv_uav_cbv_heap_cpu, 5);
  g_myframework->CreateSRVTexture2D(g_dxr_output_resource, g_srv_uav_cbv_heap, 4);
  
  g_rt_output_imgui_texid = g_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart().ptr + 4 * srv_uav_cbv_descriptor_size;
  g_myframework->CreateUAVTexture2D(g_dxr_output_resource, g_srv_uav_cbv_heap, 0);
}

int main()
{
  CreateLSSPlaygroundWindow();

  g_myframework = new MyFramework();
  g_myframework->InitDeviceAndCommandQ();
  g_myframework->SetHwnd(glfwGetWin32Window(g_window));
  g_myframework->InitSwapchain(WIN_W, WIN_H);
  g_myframework->InitImGUIForGLFW(g_window);

  InitPipeline();
  InitResources();

  while (!glfwWindowShouldClose(g_window))
  {
    Render();
    glfwPollEvents();
  }
  delete g_myframework;
  return 0;
}