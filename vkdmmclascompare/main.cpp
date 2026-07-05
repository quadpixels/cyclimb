#include <stdio.h>

#include "../vkptlastest/myframework_vk.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#undef max
#undef min
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_EXTERNAL_IMAGE
#include "tiny_gltf.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef USE_IMGUI
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#endif

#include "baryutils/baryutils.h"

uint32_t WIDTH = 800, HEIGHT = 600;

MyFrameworkVk* g_framework{};
bool g_should_exit{ false };
static GLFWwindow* g_window;
static int g_viz_mode{};

// Base mesh
static VkBuffer vertex_buffer;
static VkDeviceMemory vertex_buffer_memory;
static VkBuffer index_buffer;
static VkDeviceMemory index_buffer_memory;
static VkBuffer normals_buffer;
static VkDeviceMemory normals_memory;

// Naively populated micro-triangles
static VkBuffer naive_utris_vertex_buffer;
static VkDeviceMemory naive_utris_vertex_buffer_memory;
static VkBuffer naive_utris_index_buffer;
static VkDeviceMemory naive_utris_index_buffer_memory;
static VkBuffer naive_utris_normals_buffer;
static VkDeviceMemory naive_utris_normals_buffer_memory;

// Naive rast pipeline
static VkBuffer perscene_cb_buffer;
static VkDeviceMemory perscene_cb_buffer_memory;
static VkDescriptorPool rast_descriptor_pool;
static VkDescriptorSetLayout rast_descriptor_set_layout;
static VkDescriptorSet rast_descriptor_set;
static VkPipelineLayout rast_pipeline_layout;
static VkPipeline rast_pipeline;
size_t g_num_indices{}, g_num_vertices{};
size_t g_num_naive_utris_indices{}, g_num_naive_utris_vertices{};
// RT pipeline
static VkDescriptorSetLayout rt_descriptor_set_layout;
static VkDescriptorPool rt_descriptor_pool;
static VkDescriptorSet rt_descriptor_set;
static VkPipelineLayout rt_pipeline_layout;
static MyFrameworkVk::MyRtPipeline g_my_rt_pipeline;
static VkImage rt_output_image;
static VkDeviceMemory rt_output_image_memory;
static VkImageView rt_output_image_view;
static VkBuffer rt_perscene_cb_buffer;
static VkDeviceMemory rt_perscene_cb_buffer_memory;
// Mesh shading pipeline
static VkDescriptorSetLayout mesh_descriptor_set_layout;
static VkDescriptorPool mesh_descriptor_pool;
static VkDescriptorSet mesh_descriptor_set;
static VkPipelineLayout mesh_pipeline_layout;
static VkPipeline mesh_pipeline;

// Blas, TLAS
static VkAccelerationStructureKHR blas;
static VkBuffer blas_result_buffer;
static VkDeviceMemory blas_result_memory;
MyFrameworkVk::MyASBuildInfo blas_build_info;
static VkAccelerationStructureKHR tlas;
static VkBuffer tlas_result_buffer;
static VkDeviceMemory tlas_result_memory;
static VkAccelerationStructureKHR naive_utris_blas;
static VkBuffer naive_utris_blas_result_buffer;
static VkDeviceMemory naive_utris_blas_result_memory;
MyFrameworkVk::MyASBuildInfo naive_utris_blas_build_info;
static VkAccelerationStructureKHR naive_utris_tlas;
static VkBuffer naive_utris_tlas_result_buffer;
static VkDeviceMemory naive_utris_tlas_result_memory;

static baryutils::BaryLevelsMap g_maps(bary::ValueLayout::eTriangleBirdCurve, 5);
static std::vector<std::vector<float>> g_dmm_displacements;
static std::vector<uint8_t> g_dmm_levels;

struct PerSceneUniformBuffer {
  glm::mat4 M, V, P;
};

struct RtPersceneUniformBuffer {
  glm::mat4 inv_view, inv_proj;
};

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  if (action == GLFW_PRESS) {
    switch (key) {
    case GLFW_KEY_ESCAPE: {
      g_should_exit = true;
      break;
    }
    default:
      break;
    }
  }
}

struct MyModelStuff {
  std::vector<glm::vec3> positions;
  std::vector<uint32_t>  indices;
  std::vector<glm::vec3> normals; // per-face
  std::vector<glm::vec3> per_vertex_normals;  // per-vertex
  void Print(uint32_t count) {
    printf("First %u elts\n", count);
    printf("pos:");
    for (uint32_t i = 0; i < std::min(count, uint32_t(positions.size())); i++) {
      auto p = positions[i];
      printf(" (%g,%g,%g)", p.x, p.y, p.z);
    }
    printf("\n");
    printf("idx:");
    for (uint32_t i = 0; i < std::min(count, uint32_t(indices.size())); i++) {
      auto idx = indices[i];
      printf(" %u", idx);
    }
    printf("\n");
  }

  void CalculateNormals() {
    normals.clear();
    for (uint32_t i = 0; i < indices.size(); i += 3) {
      glm::vec3 v0 = positions[indices[i]];
      glm::vec3 v1 = positions[indices[i + 1]];
      glm::vec3 v2 = positions[indices[i + 2]];
      glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v2 - v0));
      normals.push_back(n);
    }
  }
};

static bool is_directive(const std::string& line, const char* key)
{
  size_t i = 0;
  while (i < line.size() && std::isspace((unsigned char)line[i]))
    ++i;

  size_t k = 0;
  while (key[k] && i + k < line.size() && line[i + k] == key[k])
    ++k;

  if (key[k] != '\0')
    return false;

  size_t j = i + k;
  return j == line.size() || std::isspace((unsigned char)line[j]);
}

static int parse_obj_position_index(const std::string& tok, int currentVertexCount)
{
  // tok can be:
  //   "12"
  //   "12/3"
  //   "12//7"
  //   "12/3/7"
  std::string s = tok.substr(0, tok.find('/'));
  int idx = std::stoi(s);

  // OBJ index is 1-based if positive.
  // Negative index is relative to current vertex count.
  if (idx > 0)
    return idx - 1;
  else
    return currentVertexCount + idx;
}

MyModelStuff LoadModel(const char* filename) {
  std::string s(filename);
  MyModelStuff ret;
  if (s.ends_with(".gltf")) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool load_ok = loader.LoadASCIIFromFile(&model, &err, &warn, filename);

    if (!warn.empty()) {
      printf("Warning: %s\n", warn.c_str());
    }
    if (!err.empty()) {
      printf("Err:     %s\n", err.c_str());
    }
    if (!load_ok) {
      printf("Failed to load.\n");
    }
    else {
      printf("Load %s OK.\n", filename);
      uint32_t num_tris{ 0 };
      for (const auto& m : model.meshes) {
        for (const auto& prim : m.primitives) {
          auto posIt = prim.attributes.find("POSITION");
          if (posIt != prim.attributes.end()) {
            int accessorIdx = posIt->second;
            const tinygltf::Accessor& accessor = model.accessors[accessorIdx];
            const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
            int stride = accessor.ByteStride(bufferView);
            printf("position bytestride: %d\n", stride);
            const char* dataPtr = reinterpret_cast<const char*>(
              buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
            for (uint32_t pidx = 0; pidx < accessor.count; pidx++) {
              glm::vec3 p = *(reinterpret_cast<const glm::vec3*>(dataPtr + stride * pidx));
              ret.positions.push_back(p);
            }
          }

          if (prim.indices > -1) {
            const tinygltf::Accessor& indexAccessor = model.accessors[prim.indices];
            const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
            const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];
            int stride = indexAccessor.ByteStride(indexBufferView);
            printf("index bytestride: %d\n", stride);
            num_tris += indexAccessor.count / 3;
            const char* dataPtr = reinterpret_cast<const char*>(
              indexBuffer.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset);
            for (uint32_t iidx = 0; iidx < indexAccessor.count; iidx++) {
              uint32_t idx = *(reinterpret_cast<const uint32_t*>(dataPtr + stride * iidx));
              ret.indices.push_back(idx);
            }
          }
        }
      }

      printf("%zu meshes. %zu total vec3s. %zu total indices\n",
        model.meshes.size(), ret.positions.size(), ret.indices.size());
    }
  }
  else if (s.ends_with(".obj")) {
    std::ifstream in(filename);
    if (!in)
      throw std::runtime_error("Failed to open OBJ: " + std::string(filename));

    std::string line;

    while (std::getline(in, line)) {
      if (is_directive(line, "v")) {
        std::istringstream ss(line);
        std::string tag;
        glm::vec3 p;

        ss >> tag >> p.x >> p.y >> p.z;
        if (!ss)
          throw std::runtime_error("Bad vertex line: " + line);

        ret.positions.push_back(p);
      }
      else if (is_directive(line, "vn")) {
        std::istringstream ss(line);
        std::string tag;
        glm::vec3 p;

        ss >> tag >> p.x >> p.y >> p.z;
        if (!ss)
          throw std::runtime_error("Bad vn line: " + line);
        ret.per_vertex_normals.push_back(p);
      }
      else if (is_directive(line, "f")) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;

        std::vector<uint32_t> poly;
        std::string tok;

        while (ss >> tok) {
          int vi = parse_obj_position_index(tok, (int)ret.positions.size());
          if (vi < 0 || vi >= (int)ret.positions.size())
            throw std::runtime_error("OBJ vertex index out of range: " + line);

          poly.push_back((uint32_t)vi);
        }

        if (poly.size() < 3)
          continue;

        // fan triangulation: f a b c d -> abc, acd
        for (size_t i = 1; i + 1 < poly.size(); ++i) {
          ret.indices.push_back(poly[0]);
          ret.indices.push_back(poly[i]);
          ret.indices.push_back(poly[i+1]);
        }
      }
    }
  }
  else {
    throw std::exception("Not sure how to deal with this format.");
  }

  ret.CalculateNormals();
  return ret;
}

void StartImGuiForFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void RenderImGuiAndEndImGuiForFrame(VkCommandBuffer commandBuffer) {
  ImGui::SetNextWindowSize(ImVec2(360, 320), ImGuiCond_Once);
  ImGui::SetNextWindowPos(ImVec2(32, 32), ImGuiCond_Once);
  ImGui::Begin("DMM vs regular vs Cluster Test.");
  ImGui::Text("Viz mode");

  char buf[100];
  snprintf(buf, sizeof(buf), "Rast base mesh, %zu tris", g_num_indices / 3);
  ImGui::RadioButton(buf, &g_viz_mode, 0);
  snprintf(buf, sizeof(buf), "Rast Utris, %zu tris", g_num_naive_utris_indices / 3);
  ImGui::RadioButton(buf, &g_viz_mode, 1);
  snprintf(buf, sizeof(buf), "RT base mesh, AS size %zu", blas_build_info.as_size);
  ImGui::RadioButton(buf, &g_viz_mode, 2);
  snprintf(buf, sizeof(buf), "RT Utris, AS size %zu", naive_utris_blas_build_info.as_size);
  ImGui::RadioButton(buf, &g_viz_mode, 3);
  snprintf(buf, sizeof(buf), "Mesh shading");
  ImGui::RadioButton(buf, &g_viz_mode, 4);

  ImGui::End();
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = 0;
  beginInfo.pInheritanceInfo = nullptr;
  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("Failed to begin command buffer");
  }

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = g_framework->renderPass;
  renderPassInfo.framebuffer = g_framework->swapChainFramebuffers[imageIndex];
  renderPassInfo.renderArea.offset = { 0, 0 };
  renderPassInfo.renderArea.extent = g_framework->swapChainExtent;
  VkClearValue clearColor = { {{0.3f, 0.3f, 0.3f, 1.0f }} };
  VkClearValue clearDepth{};
  renderPassInfo.clearValueCount = 2;
  clearDepth.depthStencil = { 1.0f, 0 };
  VkClearValue clearValues[] = { clearColor, clearDepth };
  renderPassInfo.pClearValues = clearValues;
  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rast_pipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rast_pipeline_layout, 0, 1, &rast_descriptor_set, 0, nullptr);

  VkViewport viewport{};
  viewport.x = viewport.y = 0;
  viewport.width = (float)(g_framework->swapChainExtent.width);
  viewport.height = (float)(g_framework->swapChainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = { 0, 0 };
  scissor.extent = g_framework->swapChainExtent;
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

  VkDeviceSize zero{ 0 };
  if (g_viz_mode == 0) {
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertex_buffer, &zero);
    vkCmdBindIndexBuffer(commandBuffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, g_num_indices, 1, 0, 0, 0);
  }
  else if (g_viz_mode == 1) {
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &naive_utris_vertex_buffer, &zero);
    vkCmdBindIndexBuffer(commandBuffer, naive_utris_index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, g_num_naive_utris_indices, 1, 0, 0, 0);
  }

  vkCmdEndRenderPass(commandBuffer);

  g_framework->CmdTransitionImageLayout(commandBuffer, g_framework->swapChainImages[imageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = g_framework->imguiRenderPass;
  renderPassInfo.framebuffer = g_framework->imguiFramebuffers[imageIndex];
  renderPassInfo.renderArea.offset = { 0, 0 };
  renderPassInfo.renderArea.extent = g_framework->swapChainExtent;
  renderPassInfo.clearValueCount = 0;
  renderPassInfo.pClearValues = nullptr;
  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  RenderImGuiAndEndImGuiForFrame(commandBuffer);
  vkCmdEndRenderPass(commandBuffer);

  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("Could not end command buffer");
  }
}

void recordRtCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  // Set descriptors
  if (g_viz_mode == 2) {
    g_framework->CreateSRVAccelerationStructure(tlas, rt_descriptor_set, 0);
    g_framework->CreateUAVBuffer(vertex_buffer, 0, sizeof(glm::vec3) * g_num_vertices, rt_descriptor_set, 3);
    g_framework->CreateUAVBuffer(index_buffer, 0, sizeof(uint32_t) * g_num_indices, rt_descriptor_set, 4);
  }
  else if (g_viz_mode == 3) {
    g_framework->CreateSRVAccelerationStructure(naive_utris_tlas, rt_descriptor_set, 0);
    g_framework->CreateUAVBuffer(naive_utris_vertex_buffer, 0, sizeof(glm::vec3) * g_num_naive_utris_vertices, rt_descriptor_set, 3);
    g_framework->CreateUAVBuffer(naive_utris_index_buffer, 0, sizeof(uint32_t) * g_num_naive_utris_indices, rt_descriptor_set, 4);
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = 0;
  beginInfo.pInheritanceInfo = nullptr;
  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("Failed to begin command buffer");
  }

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_my_rt_pipeline.rtPipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rt_pipeline_layout, 0, 1, &rt_descriptor_set, 0, nullptr);
  PFN_vkCmdTraceRaysKHR funcCmdTraceRaysKHR =
    (PFN_vkCmdTraceRaysKHR)vkGetInstanceProcAddr(
      g_framework->instance, "vkCmdTraceRaysKHR");
  funcCmdTraceRaysKHR(commandBuffer,
    &g_my_rt_pipeline.rtRgenRegion,
    &g_my_rt_pipeline.rtMissRegion,
    &g_my_rt_pipeline.rtHitRegion,
    &g_my_rt_pipeline.rtCallRegion, WIDTH, HEIGHT, 1);

  //g_framework->CmdTransitionImageLayout(commandBuffer, rt_output_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = rt_output_image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

  vkCmdPipelineBarrier(
    commandBuffer,
    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    0,
    0, nullptr,
    0, nullptr,
    1, &barrier);
  g_framework->CmdTransitionImageLayout(commandBuffer, g_framework->swapChainImages[imageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  VkImageBlit blit{};
  blit.srcOffsets[1] = { static_cast<int>(WIDTH), static_cast<int>(HEIGHT), 1 };
  blit.dstOffsets[1] = { static_cast<int>(WIDTH), static_cast<int>(HEIGHT), 1 };
  blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.srcSubresource.layerCount = 1;
  blit.dstSubresource.layerCount = 1;

  vkCmdBlitImage(commandBuffer,
    rt_output_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    g_framework->swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    1, &blit, VK_FILTER_LINEAR);

  g_framework->CmdTransitionImageLayout(commandBuffer, g_framework->swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  g_framework->CmdTransitionImageLayout(commandBuffer, rt_output_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = g_framework->imguiRenderPass;
  renderPassInfo.framebuffer = g_framework->imguiFramebuffers[imageIndex];
  renderPassInfo.renderArea.offset = { 0, 0 };
  renderPassInfo.renderArea.extent = g_framework->swapChainExtent;
  renderPassInfo.clearValueCount = 0;
  renderPassInfo.pClearValues = nullptr;
  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  RenderImGuiAndEndImGuiForFrame(commandBuffer);
  vkCmdEndRenderPass(commandBuffer);

  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("Could not end command buffer");
  }
}

void recordMeshShadingCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  g_framework->CreateCBVBuffer(perscene_cb_buffer, 0, sizeof(PerSceneUniformBuffer), mesh_descriptor_set, 0);
  g_framework->CreateUAVBuffer(vertex_buffer, 0, sizeof(glm::vec3) * g_num_vertices, mesh_descriptor_set, 1);
  g_framework->CreateUAVBuffer(index_buffer, 0, sizeof(uint32_t) * g_num_indices, mesh_descriptor_set, 2);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = 0;
  beginInfo.pInheritanceInfo = nullptr;
  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("Failed to begin command buffer");
  }

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = g_framework->renderPass;
  renderPassInfo.framebuffer = g_framework->swapChainFramebuffers[imageIndex];
  renderPassInfo.renderArea.offset = { 0, 0 };
  renderPassInfo.renderArea.extent = g_framework->swapChainExtent;
  VkClearValue clearColor = { {{0.3f, 0.3f, 0.3f, 1.0f }} };
  VkClearValue clearDepth{};
  renderPassInfo.clearValueCount = 2;
  clearDepth.depthStencil = { 1.0f, 0 };
  VkClearValue clearValues[] = { clearColor, clearDepth };
  renderPassInfo.pClearValues = clearValues;
  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline_layout, 0, 1, &mesh_descriptor_set, 0, nullptr);

  VkViewport viewport{};
  viewport.x = viewport.y = 0;
  viewport.width = (float)(g_framework->swapChainExtent.width);
  viewport.height = (float)(g_framework->swapChainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = { 0, 0 };
  scissor.extent = g_framework->swapChainExtent;
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  
  PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(g_framework->device, "vkCmdDrawMeshTasksEXT");
  vkCmdDrawMeshTasksEXT(commandBuffer, 1, 1, 1);

  vkCmdEndRenderPass(commandBuffer);

  g_framework->CmdTransitionImageLayout(commandBuffer, g_framework->swapChainImages[imageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = g_framework->imguiRenderPass;
  renderPassInfo.framebuffer = g_framework->imguiFramebuffers[imageIndex];
  renderPassInfo.renderArea.offset = { 0, 0 };
  renderPassInfo.renderArea.extent = g_framework->swapChainExtent;
  renderPassInfo.clearValueCount = 0;
  renderPassInfo.pClearValues = nullptr;
  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  RenderImGuiAndEndImGuiForFrame(commandBuffer);
  vkCmdEndRenderPass(commandBuffer);

  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("Could not end command buffer");
  }
}


void DrawFrame() {
  // Update UAV
  static int last_viz_mode{ -1 };
  if (last_viz_mode != g_viz_mode) {
    switch (g_viz_mode) {
    case 0: {
      g_framework->CreateUAVBuffer(normals_buffer, 0, sizeof(glm::vec3) * g_num_indices / 3, rast_descriptor_set, 1);
      break;
    }
    case 1: {
      g_framework->CreateUAVBuffer(naive_utris_normals_buffer, 0, sizeof(glm::vec3) * g_num_naive_utris_indices / 3, rast_descriptor_set, 1);
      break;
    }
    }
    last_viz_mode = g_viz_mode;
  }

  uint32_t imageIndex;
  vkWaitForFences(g_framework->device, 1, &(g_framework->inFlightFence), VK_TRUE, UINT64_MAX);
  vkResetFences(g_framework->device, 1, &(g_framework->inFlightFence));
  vkResetCommandBuffer(g_framework->commandBuffer, 0);

  StartImGuiForFrame();
  vkAcquireNextImageKHR(g_framework->device, g_framework->swapChain, UINT64_MAX, g_framework->imageAvailableSemaphore[0], VK_NULL_HANDLE, &imageIndex);

  switch (g_viz_mode) {
    case 0: case 1: {
      recordCommandBuffer(g_framework->commandBuffer, imageIndex);
      break;
    }
    case 2: case 3: {
      recordRtCommandBuffer(g_framework->commandBuffer, imageIndex);
      break;
    }
    case 4: {
      recordMeshShadingCommandBuffer(g_framework->commandBuffer, imageIndex);
      break;
    }
  }

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  VkSemaphore waitSemaphores[] = { g_framework->imageAvailableSemaphore[0]};
  submitInfo.pWaitSemaphores = waitSemaphores;
  VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &(g_framework->commandBuffer);
  VkSemaphore signalSemaphores[] = { g_framework->renderFinishedSemaphore };
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;
  if (vkQueueSubmit(g_framework->graphicsQueue, 1, &submitInfo, g_framework->inFlightFence) != VK_SUCCESS) {
    throw std::runtime_error("Failed to submit draw command buffer");
  }

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &g_framework->swapChain;
  presentInfo.pImageIndices = &imageIndex;
  presentInfo.pResults = nullptr;

  if (vkQueuePresentKHR(g_framework->presentQueue, &presentInfo) != VK_SUCCESS) {
    throw std::runtime_error("Failed to present");
  }
}

uint32_t RoundUp(uint32_t val, uint32_t boundary) {
  return ((val - 1) / boundary + 1) * boundary;
}

float getBaryMinMaxValue(bary::Format fmt, const void* data, size_t idx)
{
  switch (fmt)
  {
  case bary::Format::eR8_unorm:
    return float(reinterpret_cast<const uint8_t*>(data)[idx]) / float(0xFF);
  case bary::Format::eR16_unorm:
    return float(reinterpret_cast<const uint16_t*>(data)[idx]) / float(0xFFFF);
  case bary::Format::eR11_unorm_pack16:
  case bary::Format::eR11_unorm_packed_align32:
    return float(reinterpret_cast<const uint16_t*>(data)[idx]) / float(0x7FF);
  case bary::Format::eR32_sfloat:
    return float(reinterpret_cast<const float*>(data)[idx]);
  default:
    return 0.0f;
  }
}

const uint32_t VERTICES_COUNT_PER_LEVEL[] = {
  3, 6, 15, 45, 153, 561
};

const uint32_t UTRIS_COUNT_PER_LEVEL[] = {
  1, 4, 16, 64, 256, 1024
};

int LoadBary(const std::string& fn) {
  baryutils::BaryFileOpenOptions openOptions{};
  baryutils::BaryFile            baryFile{};
  bary::Result                   baryResult = baryFile.open(fn.c_str(), &openOptions);
  if (baryResult != bary::Result::eSuccess)
  {
    printf("Oh! file open failed.\n");
    return 1;
  }
  printf("bary file open success.\n");
  baryResult = baryFile.validate(bary::ValueSemanticType::eDisplacement);
  if (baryResult != bary::Result::eSuccess)
  {
    printf("Oh! validation failed.\n");
    return 1;
  }

  printf("Triangle count: %u\n", (uint32_t)(baryFile.getBasic().trianglesCount));
  baryutils::BaryBasicData uncompressed{};
  baryutils::BaryBasicData compressed{};
  baryutils::BaryMiscData  compressedMisc{};

  bary::Format valueFormat = baryFile.getBasic().valuesInfo->valueFormat;
  bary::ValueFrequency valueFrequency = baryFile.getBasic().valuesInfo->valueFrequency;

  switch (valueFormat)
  {
  case bary::Format::eR11_unorm_packed_align32:
    uncompressed.setData(baryFile.getBasic());
    printf("group uncompressed mips count: %d, triangle uncompressed mips count: %d\n",
      baryFile.getMisc().groupUncompressedMipsCount,
      baryFile.getMisc().triangleUncompressedMipsCount);
    if (baryFile.getMisc().groupUncompressedMipsCount && baryFile.getMisc().triangleUncompressedMipsCount
      && baryFile.getMisc().uncompressedMipsInfo)
    {
      compressedMisc.setData(baryFile.getMisc());
    }
    break;
  default:
    printf("Not suer how to deal with format %d\n", int(valueFormat));
    return 1;
  }

  bary::BasicView basic = uncompressed.getView();

  printf("basic.groupsCount=%u\n", (uint32_t)(basic.groupsCount));
  printf("value align=%u, bytesize=%u, count=%u, format=%u, freq=%u, layout=%u\n",
    (uint32_t)(basic.valuesInfo->valueByteAlignment),
    (uint32_t)(basic.valuesInfo->valueByteSize),
    (uint32_t)(basic.valuesInfo->valueCount),
    (uint32_t)(basic.valuesInfo->valueFormat),    // 1000397002 = eR11_unorm_packed_align32
    (uint32_t)(basic.valuesInfo->valueFrequency), // 1 = ePerVertex
    (uint32_t)(basic.valuesInfo->valueLayout));   // 2 = eTriangleBirdCurve
  printf("group[0], tri first=%u, tri count=%u, ", (uint32_t)(basic.groups[0].triangleFirst),
    (uint32_t)(basic.groups[0].triangleCount));
  printf("value first=%u, value count=%u\n", (uint32_t)(basic.groups[0].valueFirst), (uint32_t)(basic.groups[0].valueCount));
  assert(basic.groupsCount == 1);
  bary::Group baryGroup = basic.groups[0];
  const uint8_t* pValues = basic.values;

  const float valueBias = baryGroup.floatBias.r;
  const float valueScale = baryGroup.floatScale.r;

  uint32_t utIdx = 0;
  uint32_t utValueOffsetTotal = 0;

  std::vector<glm::vec3> micro_tris;

  for (uint32_t triIdx = 0; triIdx < basic.groups[0].triangleCount; triIdx++)
  {
    const bary::Triangle& baryTri = basic.triangles[triIdx];
    float minDisp = getBaryMinMaxValue(basic.triangleMinMaxsInfo->elementFormat, basic.triangleMinMaxs, triIdx * 2 + 0) * valueScale + valueBias;
    float maxDisp = getBaryMinMaxValue(basic.triangleMinMaxsInfo->elementFormat, basic.triangleMinMaxs, triIdx * 2 + 1) * valueScale + valueBias;
    printf("tri[%u], level=%u, valuesOffset=%u, minDisp=%g, maxDisp=%g\n",
      triIdx, baryTri.subdivLevel, baryTri.valuesOffset, minDisp, maxDisp);

    const uint32_t vc = VERTICES_COUNT_PER_LEVEL[baryTri.subdivLevel];
    assert(basic.valuesInfo->valueFormat == bary::Format::eR11_unorm_packed_align32);

    std::vector<float> values;


    uint32_t valueBitsOffset = 8 * baryTri.valuesOffset;

    for (uint32_t vidx = 0; vidx < vc; vidx++)
    {
      uint32_t bitOffset = valueBitsOffset + 11 * vidx;
      uint32_t word0Idx = bitOffset / 32;
      uint32_t word0BitOffset = bitOffset % 32;
      int      nBitsInWord0 = std::min(11, 32 - (int)word0BitOffset);
      int      nBitsInWord1 = std::max(0, 11 - nBitsInWord0);

      int val{ 0 };
      int      bidx = 0;
      uint32_t word0 = ((uint32_t*)basic.values)[word0Idx];
      uint32_t word1 = ((uint32_t*)basic.values)[word0Idx + 1];

      for (uint32_t offset = word0BitOffset; offset < 32 && offset < word0BitOffset + nBitsInWord0; offset++)
      {
        bool b = word0 & (1 << (offset));
        if (b)
        {
          val |= (1 << bidx);
        }
        bidx++;
      }
      for (uint32_t offset = 0; offset < nBitsInWord1; offset++)
      {
        bool b = word1 & (1 << (offset));
        if (b)
        {
          val |= (1 << bidx);
        }
        bidx++;
      }

      float fVal = float(val) / 0x7FF;
      fVal = fVal * valueScale + valueBias;

      //      float disp = getBaryMinMaxValue(basic.valuesInfo->valueFormat, basic.values, utIdx) * valueScale + valueBias;
      utIdx++;
      //printf("    uVertex[%u], word0=%p, w0bitoffset=%d, value=%u=%g\n",
      //  vidx, ((uint32_t*)basic.values + word0Idx), word0BitOffset, val, fVal);
      values.push_back(fVal);
    }

    const uint32_t tc = UTRIS_COUNT_PER_LEVEL[baryTri.subdivLevel];
    // glm::vec3      p0 = g_verts[triIdx * 3];
    // glm::vec3      p1 = g_verts[triIdx * 3 + 1];
    // glm::vec3      p2 = g_verts[triIdx * 3 + 2];
    // glm::vec3      n0 = g_directions[triIdx * 3];
    // glm::vec3      n1 = g_directions[triIdx * 3 + 1];
    // glm::vec3      n2 = g_directions[triIdx * 3 + 2];

    baryutils::BaryLevelsMap::Level level = g_maps.getLevel(baryTri.subdivLevel);

    // for(uint32_t tidx = 0; tidx < level.triangles.size(); tidx++)
    // {
    //   baryutils::BaryLevelsMap::Triangle tri = level.triangles[tidx];
    //   glm::vec3                          wuv_a, wuv_b, wuv_c;
    //   level.getFloatCoord(tri.a, &wuv_a.x);
    //   level.getFloatCoord(tri.b, &wuv_b.x);
    //   level.getFloatCoord(tri.c, &wuv_c.x);

    //   float     mag_a = values[tri.a];  // * (maxDisp - minDisp) + minDisp;
    //   float     mag_b = values[tri.b];  // * (maxDisp - minDisp) + minDisp;
    //   float     mag_c = values[tri.c];  // * (maxDisp - minDisp) + minDisp;

    //   glm::vec3 un0 = n0 * wuv_a.x + n1 * wuv_a.y + n2 * wuv_a.z;
    //   glm::vec3 un1 = n0 * wuv_b.x + n1 * wuv_b.y + n2 * wuv_b.z;
    //   glm::vec3 un2 = n0 * wuv_c.x + n1 * wuv_c.y + n2 * wuv_c.z;

    //   glm::vec3 up0 = p0 * wuv_a.x + p1 * wuv_a.y + p2 * wuv_a.z + un0 * mag_a;
    //   glm::vec3 up1 = p0 * wuv_b.x + p1 * wuv_b.y + p2 * wuv_b.z + un1 * mag_b;
    //   glm::vec3 up2 = p0 * wuv_c.x + p1 * wuv_c.y + p2 * wuv_c.z + un2 * mag_c;

    //   micro_tris.push_back(up0);
    //   micro_tris.push_back(up1);
    //   micro_tris.push_back(up2);
    // }

    valueBitsOffset += (11 * vc);
    valueBitsOffset = RoundUp(valueBitsOffset, 32);
    utValueOffsetTotal += baryTri.valuesOffset;
    g_dmm_displacements.push_back(values);
    g_dmm_levels.push_back((uint8_t)(baryTri.subdivLevel));
  }

  printf("Loaded displacements for %zu triangles.\n", g_dmm_displacements.size());

  baryFile.close();
  return 0;
}

MyModelStuff PopulateDisplacedMicroTriangles(
  const MyModelStuff& base_mesh,
  const std::vector<std::vector<float>> dmm_displacements,
  const std::vector<uint8_t>& dmm_levels
) {
  MyModelStuff ret;
  for (uint32_t i = 0; i < base_mesh.indices.size(); i += 3) {
    uint32_t i0 = base_mesh.indices[i];
    uint32_t i1 = base_mesh.indices[i + 1];
    uint32_t i2 = base_mesh.indices[i + 2];

    glm::vec3 v0 = base_mesh.positions[i0];
    glm::vec3 v1 = base_mesh.positions[i1];
    glm::vec3 v2 = base_mesh.positions[i2];

    glm::vec3 n0 = base_mesh.per_vertex_normals[i0];
    glm::vec3 n1 = base_mesh.per_vertex_normals[i1];
    glm::vec3 n2 = base_mesh.per_vertex_normals[i2];

    uint8_t subdivision_level = dmm_levels[i / 3];
    baryutils::BaryLevelsMap::Level level = g_maps.getLevel(subdivision_level);
    const auto& displacements = dmm_displacements[i / 3];

    auto get_displaced_triangle = [&](int a, int b, int c, bool is_max) {
      glm::vec3 wuv_a, wuv_b, wuv_c;
      level.getFloatCoord(a, &wuv_a.x);
      level.getFloatCoord(b, &wuv_b.x);
      level.getFloatCoord(c, &wuv_c.x);

      glm::vec3 utri_low_0 = v0 * wuv_a.x + v1 * wuv_a.y + v2 * wuv_a.z;
      glm::vec3 utri_low_1 = v0 * wuv_b.x + v1 * wuv_b.y + v2 * wuv_b.z;
      glm::vec3 utri_low_2 = v0 * wuv_c.x + v1 * wuv_c.y + v2 * wuv_c.z;

      glm::vec3 utri_d0 = n0 * wuv_a.x + n1 * wuv_a.y + n2 * wuv_a.z;
      glm::vec3 utri_d1 = n0 * wuv_b.x + n1 * wuv_b.y + n2 * wuv_b.z;
      glm::vec3 utri_d2 = n0 * wuv_c.x + n1 * wuv_c.y + n2 * wuv_c.z;

      float mag0 = 1.0f, mag1 = 1.0f, mag2 = 1.0f;
      if (!is_max) {
        mag0 = displacements.at(a);
        mag1 = displacements.at(b);
        mag2 = displacements.at(c);
      }

      glm::vec3 utri_high_0 = utri_low_0 + utri_d0 * mag0;
      glm::vec3 utri_high_1 = utri_low_1 + utri_d1 * mag1;
      glm::vec3 utri_high_2 = utri_low_2 + utri_d2 * mag2;

      return std::make_tuple(utri_high_0, utri_high_1, utri_high_2);
    };

    for (uint32_t tidx = 0; tidx < level.triangles.size(); tidx++) {
      baryutils::BaryLevelsMap::Triangle tri = level.triangles[tidx];
      auto [p0, p1, p2] = get_displaced_triangle(tri.a, tri.b, tri.c, false);
      ret.indices.push_back(ret.indices.size());
      ret.positions.push_back(p0);
      ret.indices.push_back(ret.indices.size());
      ret.positions.push_back(p1);
      ret.indices.push_back(ret.indices.size());
      ret.positions.push_back(p2);
    }
  }

  printf("When naively populating the geometry, there are %u triangles\n",
    ret.indices.size() / 3);
  g_num_naive_utris_indices = ret.indices.size();
  g_num_naive_utris_vertices = ret.positions.size();
  ret.CalculateNormals();
  return ret;
}

void CreateRastPipeline() {
  // Pipeline
  std::vector<char> vs_code = g_framework->ReadFile("shaders/vert.spv");
  std::vector<char> fs_code = g_framework->ReadFile("shaders/frag.spv");
  VkShaderModule vs_module = g_framework->CreateShaderModule(vs_code);
  VkShaderModule fs_module = g_framework->CreateShaderModule(fs_code);
  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = vs_module;
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = fs_module;
  fragShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

  std::vector<VkDynamicState> dynamicStates = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR
  };
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = (uint32_t)(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  // 1 vertex buffer servicing location=0 and location=1
  VkVertexInputBindingDescription vertexBindingDesc{};
  vertexBindingDesc.binding = 0;
  vertexBindingDesc.stride = sizeof(glm::vec3);
  vertexBindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription vertexAttrDesc[1]{};
  vertexAttrDesc[0].binding = 0;
  vertexAttrDesc[0].location = 0;
  vertexAttrDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  vertexAttrDesc[0].offset = 0;

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &vertexBindingDesc;
  vertexInputInfo.vertexAttributeDescriptionCount = _countof(vertexAttrDesc);
  vertexInputInfo.pVertexAttributeDescriptions = vertexAttrDesc;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = false;

  VkViewport viewport{};
  viewport.x = 0;
  viewport.y = 0;
  viewport.width = float(g_framework->swapChainExtent.width);
  viewport.height = float(g_framework->swapChainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = { 0, 0 };
  scissor.extent = g_framework->swapChainExtent;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.depthBiasConstantFactor = 0.0f;
  rasterizer.depthBiasClamp = 0.0f;
  rasterizer.depthBiasSlopeFactor = 0.0f;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.minSampleShading = 1.0f;
  multisampling.pSampleMask = nullptr;
  multisampling.alphaToCoverageEnable = VK_FALSE;
  multisampling.alphaToOneEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask =
    VK_COLOR_COMPONENT_A_BIT |
    VK_COLOR_COMPONENT_R_BIT |
    VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;
  colorBlending.blendConstants[0] = 0;
  colorBlending.blendConstants[1] = 0;
  colorBlending.blendConstants[2] = 0;
  colorBlending.blendConstants[3] = 0;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &rast_descriptor_set_layout;
  pipelineLayoutInfo.pushConstantRangeCount = 0;
  pipelineLayoutInfo.pPushConstantRanges = nullptr;
  if (vkCreatePipelineLayout(g_framework->device, &pipelineLayoutInfo, nullptr, &rast_pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("Could not create pipeline layout");
  }

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.minDepthBounds = 0.0f; // Optional
  depthStencil.maxDepthBounds = 1.0f; // Optional
  depthStencil.stencilTestEnable = VK_FALSE;
  depthStencil.front = {}; // Optional
  depthStencil.back = {}; // Optional

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = rast_pipeline_layout;
  pipelineInfo.renderPass = g_framework->renderPass;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.pDepthStencilState = &depthStencil;

  if (vkCreateGraphicsPipelines(g_framework->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rast_pipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create graphics pipeline");
  }
  vkDestroyShaderModule(g_framework->device, vs_module, nullptr);
  vkDestroyShaderModule(g_framework->device, fs_module, nullptr);
}

void CreateMeshDescriptorSetStuff() {
  // Mesh Descriptor pool
  mesh_descriptor_pool = g_framework->CreateCBVSRVUAVPool(
    {
      { VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 },
      { VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
    },
    1
  );

  VkDescriptorSetLayoutBinding mesh_dslb[3]{};
  mesh_dslb[0].binding = 0;
  mesh_dslb[0].descriptorCount = 1;
  mesh_dslb[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  mesh_dslb[0].pImmutableSamplers = nullptr;
  mesh_dslb[0].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;

  mesh_dslb[1].binding = 1;
  mesh_dslb[1].descriptorCount = 1;
  mesh_dslb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  mesh_dslb[1].pImmutableSamplers = nullptr;
  mesh_dslb[1].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;

  mesh_dslb[2].binding = 2;
  mesh_dslb[2].descriptorCount = 1;
  mesh_dslb[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  mesh_dslb[2].pImmutableSamplers = nullptr;
  mesh_dslb[2].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;

  VkDescriptorSetLayoutCreateInfo mesh_dslci{};
  mesh_dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  mesh_dslci.bindingCount = _countof(mesh_dslb);
  mesh_dslci.pBindings = mesh_dslb;
  if (vkCreateDescriptorSetLayout(g_framework->device, &mesh_dslci, nullptr, &mesh_descriptor_set_layout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create rast descriptor set layout");
  }
  mesh_descriptor_set = g_framework->CreateDescriptorSet(mesh_descriptor_set_layout, mesh_descriptor_pool);
}

void CreateMeshPipeline() {
  std::vector<char> as_code = g_framework->ReadFile("shaders/meshshading_task.spv");
  std::vector<char> ms_code = g_framework->ReadFile("shaders/meshshading_mesh.spv");
  std::vector<char> fs_code = g_framework->ReadFile("shaders/meshshading_frag.spv");

  VkShaderModule as_module = g_framework->CreateShaderModule(as_code);
  VkShaderModule ms_module = g_framework->CreateShaderModule(ms_code);
  VkShaderModule fs_module = g_framework->CreateShaderModule(fs_code);

  VkPipelineShaderStageCreateInfo pssci[3]{};
  pssci[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pssci[0].stage = VK_SHADER_STAGE_TASK_BIT_EXT;
  pssci[0].module = as_module;
  pssci[0].pName = "main";

  pssci[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pssci[1].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
  pssci[1].module = ms_module;
  pssci[1].pName = "main";

  pssci[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pssci[2].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  pssci[2].module = fs_module;
  pssci[2].pName = "main";

  std::vector<VkDynamicState> dynamicStates = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR
  };
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = (uint32_t)(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  VkViewport viewport{};
  viewport.x = 0;
  viewport.y = 0;
  viewport.width = float(g_framework->swapChainExtent.width);
  viewport.height = float(g_framework->swapChainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = { 0, 0 };
  scissor.extent = g_framework->swapChainExtent;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.depthBiasConstantFactor = 0.0f;
  rasterizer.depthBiasClamp = 0.0f;
  rasterizer.depthBiasSlopeFactor = 0.0f;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.minSampleShading = 1.0f;
  multisampling.pSampleMask = nullptr;
  multisampling.alphaToCoverageEnable = VK_FALSE;
  multisampling.alphaToOneEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask =
    VK_COLOR_COMPONENT_A_BIT |
    VK_COLOR_COMPONENT_R_BIT |
    VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;
  colorBlending.blendConstants[0] = 0;
  colorBlending.blendConstants[1] = 0;
  colorBlending.blendConstants[2] = 0;
  colorBlending.blendConstants[3] = 0;

  VkPipelineLayoutCreateInfo plci{};
  plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &mesh_descriptor_set_layout;
  plci.pushConstantRangeCount = 0;
  plci.pPushConstantRanges = nullptr;
  vkCreatePipelineLayout(g_framework->device, &plci, nullptr, &mesh_pipeline_layout);

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.minDepthBounds = 0.0f; // Optional
  depthStencil.maxDepthBounds = 1.0f; // Optional
  depthStencil.stencilTestEnable = VK_FALSE;
  depthStencil.front = {}; // Optional
  depthStencil.back = {}; // Optional

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = _countof(pssci);
  pipelineInfo.pStages = pssci;
  pipelineInfo.pVertexInputState = nullptr;
  pipelineInfo.pInputAssemblyState = nullptr;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = mesh_pipeline_layout;
  pipelineInfo.renderPass = g_framework->renderPass;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.pDepthStencilState = &depthStencil;

  if (vkCreateGraphicsPipelines(g_framework->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mesh_pipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create graphics pipeline");
  }

  vkDestroyShaderModule(g_framework->device, as_module, nullptr);
  vkDestroyShaderModule(g_framework->device, ms_module, nullptr);
  vkDestroyShaderModule(g_framework->device, fs_module, nullptr);
}

int main(int argv, char** argc) {
  printf("Hey.\n");
  MyModelStuff model = LoadModel("murex_romosus.obj");
  LoadBary("murex_romosus.bary");
  MyModelStuff naive_utris = PopulateDisplacedMicroTriangles(model, g_dmm_displacements, g_dmm_levels);
  g_framework = new MyFrameworkVk();
  g_framework->InitWindow("VK DMM CLAS BLAS Comparison", WIDTH, HEIGHT, KeyCallback);
  g_window = g_framework->window;
  g_num_indices = model.indices.size();
  g_num_vertices = model.positions.size();

  // InitVulkan
  g_framework->InitDeviceAndCommandQ();
  g_framework->InitSwapchain();
  g_framework->InitRenderPassAndFramebuffers(true);
  g_framework->CreateSyncObjects();
  g_framework->InitImGui();
  g_framework->InitImGuiRenderPass();
  g_framework->CreateImGuiFramebuffers();

  // Create vertex buffer and index buffer
  VkBufferUsageFlags usage =
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
    | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  size_t vb_size = sizeof(glm::vec3) * model.positions.size();
  g_framework->CreateBuffer(vb_size, usage, props,
    vertex_buffer, vertex_buffer_memory
  );
  size_t ib_size = sizeof(uint32_t) * model.indices.size();
  g_framework->CreateBuffer(ib_size, usage | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, props,
    index_buffer, index_buffer_memory
  );
  size_t normals_size = sizeof(glm::vec3) * g_num_indices / 3;
  g_framework->CreateBuffer(normals_size, usage, props,
    normals_buffer, normals_memory
  );

  size_t naive_utris_vb_size = sizeof(glm::vec3) * naive_utris.positions.size();
  g_framework->CreateBuffer(naive_utris_vb_size, usage, props,
    naive_utris_vertex_buffer, naive_utris_vertex_buffer_memory
  );
  size_t naive_utris_ib_size = sizeof(uint32_t) * naive_utris.indices.size();
  g_framework->CreateBuffer(naive_utris_ib_size, usage | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, props,
    naive_utris_index_buffer, naive_utris_index_buffer_memory
  );
  size_t naive_utris_normals_size = sizeof(glm::vec3) * g_num_naive_utris_indices / 3;
  g_framework->CreateBuffer(naive_utris_normals_size, usage, props,
    naive_utris_normals_buffer, naive_utris_normals_buffer_memory
  );

  void* data;
  vkMapMemory(g_framework->device, vertex_buffer_memory, 0, vb_size, 0, &data);
  memcpy(data, model.positions.data(), vb_size);
  vkUnmapMemory(g_framework->device, vertex_buffer_memory);
  vkMapMemory(g_framework->device, index_buffer_memory, 0, ib_size, 0, &data);
  memcpy(data, model.indices.data(), ib_size);
  vkUnmapMemory(g_framework->device, index_buffer_memory);
  vkMapMemory(g_framework->device, normals_memory, 0, normals_size, 0, &data);
  memcpy(data, model.normals.data(), normals_size);
  vkUnmapMemory(g_framework->device, normals_memory);

  vkMapMemory(g_framework->device, naive_utris_vertex_buffer_memory, 0, naive_utris_vb_size, 0, &data);
  memcpy(data, naive_utris.positions.data(), naive_utris_vb_size);
  vkUnmapMemory(g_framework->device, naive_utris_vertex_buffer_memory);
  vkMapMemory(g_framework->device, naive_utris_index_buffer_memory, 0, naive_utris_ib_size, 0, &data);
  memcpy(data, naive_utris.indices.data(), naive_utris_ib_size);
  vkUnmapMemory(g_framework->device, naive_utris_index_buffer_memory);
  vkMapMemory(g_framework->device, naive_utris_normals_buffer_memory, 0, naive_utris_normals_size, 0, &data);
  memcpy(data, naive_utris.normals.data(), naive_utris_normals_size);
  vkUnmapMemory(g_framework->device, naive_utris_normals_buffer_memory);

  // Create descriptor pool
  rast_descriptor_pool = g_framework->CreateCBVSRVUAVPool(
    {
      { VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 },
      { VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 }
    },
    1
  );
  // Create descriptor layout
  VkDescriptorSetLayoutBinding rast_dslb[2]{};
  rast_dslb[0].binding = 0;
  rast_dslb[0].descriptorCount = 1;
  rast_dslb[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  rast_dslb[0].pImmutableSamplers = nullptr;
  rast_dslb[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  rast_dslb[1].binding = 1;
  rast_dslb[1].descriptorCount = 1;
  rast_dslb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  rast_dslb[1].pImmutableSamplers = nullptr;
  rast_dslb[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo rast_dslci{};
  rast_dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  rast_dslci.bindingCount = _countof(rast_dslb);
  rast_dslci.pBindings = rast_dslb;
  if (vkCreateDescriptorSetLayout(g_framework->device, &rast_dslci, nullptr, &rast_descriptor_set_layout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create rast descriptor set layout");
  }
  rast_descriptor_set = g_framework->CreateDescriptorSet(rast_descriptor_set_layout, rast_descriptor_pool);

  size_t cb_size = sizeof(PerSceneUniformBuffer);
  g_framework->CreateBuffer(cb_size, usage, props, perscene_cb_buffer, perscene_cb_buffer_memory);
  g_framework->CreateCBVBuffer(perscene_cb_buffer, 0, cb_size, rast_descriptor_set, 0);
  g_framework->CreateUAVBuffer(normals_buffer, 0, normals_size, rast_descriptor_set, 1);

  CreateRastPipeline();
  CreateMeshDescriptorSetStuff();
  CreateMeshPipeline();

  // RT Descriptor layout, Descriptor pool, Descriptor set
  VkDescriptorSetLayoutBinding rt_dslb[5]{};
  rt_dslb[0].binding = 0;
  rt_dslb[0].descriptorCount = 1;
  rt_dslb[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  rt_dslb[0].pImmutableSamplers = nullptr;
  rt_dslb[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

  rt_dslb[1].binding = 1;
  rt_dslb[1].descriptorCount = 1;
  rt_dslb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  rt_dslb[1].pImmutableSamplers = nullptr;
  rt_dslb[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

  rt_dslb[2].binding = 2;
  rt_dslb[2].descriptorCount = 1;
  rt_dslb[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  rt_dslb[2].pImmutableSamplers = nullptr;
  rt_dslb[2].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

  rt_dslb[3].binding = 3;  // Vertices data
  rt_dslb[3].descriptorCount = 1;
  rt_dslb[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  rt_dslb[3].pImmutableSamplers = nullptr;
  rt_dslb[3].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

  rt_dslb[4].binding = 4;  // Indices data
  rt_dslb[4].descriptorCount = 1;
  rt_dslb[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  rt_dslb[4].pImmutableSamplers = nullptr;
  rt_dslb[4].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

  VkDescriptorSetLayoutCreateInfo rslci{};
  rslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  rslci.bindingCount = _countof(rt_dslb);
  rslci.pBindings = rt_dslb;
  if (vkCreateDescriptorSetLayout(g_framework->device, &rslci, nullptr, &rt_descriptor_set_layout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create RT descriptor set layout");
  }

  rt_descriptor_pool = g_framework->CreateCBVSRVUAVPool({
      { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
      { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 },
    },
    1);
  rt_descriptor_set = g_framework->CreateDescriptorSet(rt_descriptor_set_layout, rt_descriptor_pool);

  // RT Output Image
  g_framework->CreateRtOutputResource(WIDTH, HEIGHT, rt_output_image, rt_output_image_memory, rt_output_image_view);
  g_framework->CreateUAVTexture2D(rt_output_image_view, rt_descriptor_set, 1);

  // RT Pipeline Layout
  VkPipelineLayoutCreateInfo plci{};
  plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &rt_descriptor_set_layout;
  plci.pushConstantRangeCount = 0;
  plci.pPushConstantRanges = nullptr;
  if (vkCreatePipelineLayout(g_framework->device, &plci, nullptr, &rt_pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("Could not create rt pipeline layout");
  }

  // RT Pipeline
  MyFrameworkVk::MyRtShaderListInfo rsli{};
  rsli.closest_hit_shader = "shaders/rchit.spv";
  rsli.raygen_shader = "shaders/rgen.spv";
  rsli.miss_shader = "shaders/rmiss.spv";
  rsli.in_pipeline_layout = rt_pipeline_layout;
  g_framework->CreateMyRtPipeline(&g_my_rt_pipeline, rsli);

  // Blas
  g_framework->BuildBLAS(blas, blas_result_buffer, blas_result_memory, vertex_buffer, index_buffer, model.positions.size(), g_num_indices, sizeof(glm::vec3), nullptr, &blas_build_info);
  g_framework->BuildTLAS(tlas, { blas }, { blas_result_buffer }, tlas_result_buffer, tlas_result_memory);
  g_framework->BuildBLAS(naive_utris_blas, naive_utris_blas_result_buffer, naive_utris_blas_result_memory,
    naive_utris_vertex_buffer, naive_utris_index_buffer,
    naive_utris.positions.size(), g_num_naive_utris_indices, sizeof(glm::vec3), nullptr, &naive_utris_blas_build_info);
  g_framework->BuildTLAS(naive_utris_tlas, { naive_utris_blas }, { naive_utris_blas_result_buffer }, naive_utris_tlas_result_buffer, naive_utris_tlas_result_memory);

  // CBV
  PerSceneUniformBuffer psub{};
  psub.M = glm::mat4(1.0f);
  psub.V = glm::lookAt(
    glm::vec3(0, 0, 200),
    glm::vec3(0, 0, 0),
    glm::vec3(0, -1, 0));
  psub.P = glm::perspectiveFovRH_ZO(3.1415926f / 4,
    WIDTH * 1.0f, HEIGHT * 1.0f, 0.1f, 10000.0f);
  vkMapMemory(g_framework->device, perscene_cb_buffer_memory, 0, sizeof(PerSceneUniformBuffer), 0, (void**)&data);
  memcpy(data, &psub, sizeof(psub));
  vkUnmapMemory(g_framework->device, perscene_cb_buffer_memory);

  // RT CB and CBV
  cb_size = sizeof(RtPersceneUniformBuffer);
  g_framework->CreateBuffer(cb_size, usage, props, rt_perscene_cb_buffer, rt_perscene_cb_buffer_memory);
  vkMapMemory(g_framework->device, rt_perscene_cb_buffer_memory, 0, cb_size, 0, &data);
  RtPersceneUniformBuffer rtpsub{};
  rtpsub.inv_proj = glm::inverse(psub.P);
  rtpsub.inv_view = glm::inverse(psub.V);
  memcpy(data, &rtpsub, cb_size);
  vkUnmapMemory(g_framework->device, rt_perscene_cb_buffer_memory);
  g_framework->CreateCBVBuffer(rt_perscene_cb_buffer, 0, cb_size, rt_descriptor_set, 2);

  while (!glfwWindowShouldClose(g_window) && !g_should_exit) {
    glfwPollEvents();
    DrawFrame();
    vkDeviceWaitIdle(g_framework->device);
  }
  vkDeviceWaitIdle(g_framework->device);
  return 0;
}