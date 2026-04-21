#include "MyScene.h"

#include <source_location>

#ifndef CE
#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}
#endif

MyRotatingTriangleScene::MyRotatingTriangleScene(MyFramework* f) : MyScene(f) {
  tri_pos_rots = {
    { 0.0, 0.0, 0.0 }
  };
}

void MyRotatingTriangleScene::BuildOrUpdateBLAS(bool is_update) {
  // Populate vertex buffer
  glm::vec2 tri0[] = {
    { -0.1, 0   },
    {  0,   0.1 },
    {  0.1, 0   }
  };
  std::vector<glm::vec3> vertices;
  for (uint32_t i = 0; i < tri_pos_rots.size(); i++) {
    for (const auto& v : tri0) {
      glm::vec3 vert;
      glm::vec3 tp = tri_pos_rots[i];
      float tx = tp.x, ty = tp.y, theta = tp.z;
      vert.x = cos(theta) * v.x - sin(theta) * v.y;
      vert.y = sin(theta) * v.x + cos(theta) * v.y;
      vert.z = 0;
      vert.x += tx;
      vert.y += ty;
      vertices.push_back(vert);
    }
  }

  ID3D12Resource* vert_buf;
  framework->CreateBufferForCPUSideData(vertices.data(), sizeof(vertices[0]) * vertices.size(), &vert_buf);
  framework->BuildBLAS(&blas_result, vert_buf, sizeof(glm::vec3), vertices.size());
  framework->BuildTLAS(&tlas_result, blas_result);
}

void MyRotatingTriangleScene::Render() {
  D3D12_CPU_DESCRIPTOR_HANDLE handle_rtv = framework->GetCurrRenderTargetCPUDescriptor();
  float bg_color[] = { 1.0f, 0.9f, 0.8f, 1.0f };
  ID3D12CommandAllocator* command_allocator = framework->GetCommandAllocator();
  ID3D12GraphicsCommandList4* command_list = framework->GetGraphicsCommandList();
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));
  ID3D12Resource* rendertarget = framework->GetCurrentRenderTarget();
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
  //command_list->CopyResource(rendertarget, rt_output_resource);
  //ResourceBarrierTransition(command_list, rt_output_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);

  CE(command_list->Close());
  ID3D12CommandQueue* command_queue = framework->GetCommandQueue();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(framework->Present());
  framework->WaitForPreviousFrame();
}

void MyRotatingTriangleScene::Update(float secs) {
  static bool is_first_frame{ true };
  BuildOrUpdateBLAS(!is_first_frame);

}

void MyRotatingTriangleScene::OnKeyDown(uint32_t k) {

}

void MyRotatingTriangleScene::OnKeyUp(uint32_t k) {

}
