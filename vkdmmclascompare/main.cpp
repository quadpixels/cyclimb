// DMM RT Can work on:
// RTX3060 and driver 532.112.0
// 

#include <stdio.h>

#include <memory>

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
static VkBuffer mesh_perscene_cb_buffer;
static VkDeviceMemory mesh_perscene_cb_buffer_memory;
static VkBuffer mesh_dmm_displacements_buffer;
static VkDeviceMemory mesh_dmm_displacements_buffer_memory;
static VkBuffer mesh_dmm_displacements_offsets_buffer;
static VkDeviceMemory mesh_dmm_displacements_offsets_buffer_memory;
static VkBuffer mesh_dmm_levels_buffer;
static VkDeviceMemory mesh_dmm_levels_buffer_memory;
static VkBuffer mesh_bary_levels_map_triangles_buffer;
static VkDeviceMemory mesh_bary_levels_map_triangles_buffer_memory;
static VkBuffer mesh_bary_levels_map_triangles_offsets_buffer;
static VkDeviceMemory mesh_bary_levels_map_triangles_offsets_buffer_memory;
static VkBuffer mesh_bary_wuvs_buffer;
static VkDeviceMemory mesh_bary_wuvs_buffer_memory;
static VkBuffer mesh_bary_wuvs_offsets_buffer;
static VkDeviceMemory mesh_bary_wuvs_offsets_buffer_memory;
static VkBuffer mesh_per_vertex_normals_buffer;
static VkDeviceMemory mesh_per_vertex_normals_buffer_memory;

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
static VkAccelerationStructureKHR dmm_blas;
static VkBuffer dmm_blas_result_buffer;
static VkDeviceMemory dmm_blas_result_memory;
MyFrameworkVk::MyASBuildInfo dmm_blas_build_info;
static VkAccelerationStructureKHR dmm_tlas;
static VkBuffer dmm_tlas_result_buffer;
static VkDeviceMemory dmm_tlas_result_memory;
MyFrameworkVk::MyDmmAttachmentInfo dmm_attachment_info;

static baryutils::BaryLevelsMap g_maps(bary::ValueLayout::eTriangleBirdCurve, 5);
static std::vector<std::vector<float>> g_dmm_displacements;
static std::vector<uint8_t> g_dmm_levels;

// DMM Data for RT
// g_micromap is in dmm_attachment_info
VkBuffer g_micromap_buf;
VkDeviceMemory g_micromap_memory;
// f16vec4 needed for this thing
VkBuffer g_dmm_displacement_vector_buffer;
VkDeviceMemory g_dmm_displacement_vector_memory;
VkBuffer g_dmm_displacement_bias_and_scale_buffer;
VkDeviceMemory g_dmm_displacement_bias_and_scale_memory;
VkBuffer g_dmm_tri_idx_buffer;
VkDeviceMemory g_dmm_tri_idx_memory;
static MyFrameworkVk::MyRtPipeline g_my_rt_dmm_pipeline;

#include "glm/detail/type_half.hpp"
#include <stddef.h>
class float16_t
{
private:
  glm::detail::hdata h = 0;

public:
  float16_t() {}
  float16_t(float f) { h = glm::detail::toFloat16(f); }
  operator float() const { return glm::detail::toFloat32(h); }
};

struct f16vec4
{
  float16_t x;
  float16_t y;
  float16_t z;
  float16_t w;
};

// Cluster
VkAccelerationStructureKHR clustered_blas;
VkBuffer clustered_blas_result_buffer;
VkDeviceMemory clustered_blas_result_memory;
VkAccelerationStructureKHR clustered_tlas;
VkBuffer clustered_tlas_result_buffer;
VkDeviceMemory clustered_tlas_result_memory;
static MyFrameworkVk::MyRtPipeline g_my_rt_clas_pipeline;
static MyFrameworkVk::MyASBuildInfo clustered_blas_build_info;

struct PerSceneUniformBuffer {
  glm::mat4 M, V, P;
};

struct MeshSceneUniformBuffer {
  glm::mat4 M, V, P;
  uint32_t is_base_mesh;
};

struct RtPersceneUniformBuffer {
  glm::mat4 inv_view, inv_proj;
};

struct BaryDisplacementAttribute
{
  // displacement
  // The BARY representation of uncompressed data
  std::unique_ptr<baryutils::BaryBasicData> uncompressed = nullptr;

  // The BARY representation of compressed data
  std::unique_ptr<baryutils::BaryBasicData> compressed = nullptr;
  std::unique_ptr<baryutils::BaryMiscData>  compressedMisc = nullptr;
};

static BaryDisplacementAttribute g_bary_displacement_attrs;

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

MyModelStuff naive_utris;
MyModelStuff base_model;

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
  ImGui::Text("Device: %s", g_framework->deviceName.c_str());
  ImGui::Text("Driver: %u.%u.%u",
    g_framework->driverVersion[0], g_framework->driverVersion[1], g_framework->driverVersion[2]);
  ImGui::Separator();
  ImGui::Text("Viz mode");

  char buf[100];
  snprintf(buf, sizeof(buf), "Rast base mesh, %zu tris", g_num_indices / 3);
  ImGui::RadioButton(buf, &g_viz_mode, 0);
  snprintf(buf, sizeof(buf), "Rast Utris, %zu tris", g_num_naive_utris_indices / 3);
  ImGui::RadioButton(buf, &g_viz_mode, 1);
  snprintf(buf, sizeof(buf), "RT base mesh, AS size %zu", blas_build_info.as_size);
  ImGui::RadioButton(buf, &g_viz_mode, 2);

  if (g_framework->hasDMM) {
    snprintf(buf, sizeof(buf), "RT DMM base mesh, AS size %zu", dmm_blas_build_info.as_size);
    ImGui::RadioButton(buf, &g_viz_mode, 6);
  }

  if (g_framework->hasCLAS) {
    snprintf(buf, sizeof(buf), "RT CLAS, AS size %zu, Clusters %zu",
      clustered_blas_build_info.as_size,
      clustered_blas_build_info.clusters_size);
    ImGui::RadioButton(buf, &g_viz_mode, 7);
  }

  snprintf(buf, sizeof(buf), "RT Utris, AS size %zu", naive_utris_blas_build_info.as_size);
  ImGui::RadioButton(buf, &g_viz_mode, 3);
  snprintf(buf, sizeof(buf), "Mesh shading base mesh");
  ImGui::RadioButton(buf, &g_viz_mode, 4);
  snprintf(buf, sizeof(buf), "Mesh shading Utris");
  ImGui::RadioButton(buf, &g_viz_mode, 5);

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
  else if (g_viz_mode == 6) {
    g_framework->CreateSRVAccelerationStructure(dmm_tlas, rt_descriptor_set, 0);
    g_framework->CreateUAVBuffer(vertex_buffer, 0, sizeof(glm::vec3) * g_num_vertices, rt_descriptor_set, 3);
    g_framework->CreateUAVBuffer(index_buffer, 0, sizeof(uint32_t) * g_num_indices, rt_descriptor_set, 4);
  }
  else if (g_viz_mode == 7) {
    g_framework->CreateSRVAccelerationStructure(clustered_tlas, rt_descriptor_set, 0);
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = 0;
  beginInfo.pInheritanceInfo = nullptr;
  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("Failed to begin command buffer");
  }

  MyFrameworkVk::MyRtPipeline* rtpipeline{};
  switch (g_viz_mode) {
    case 7: {
      rtpipeline = &g_my_rt_clas_pipeline;
      break;
    }
    case 6: {
      rtpipeline = &g_my_rt_dmm_pipeline;
      break;
    }
    default: {
      rtpipeline = &g_my_rt_pipeline;
      break;
    }
  }
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtpipeline->rtPipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rt_pipeline_layout, 0, 1, &rt_descriptor_set, 0, nullptr);
  PFN_vkCmdTraceRaysKHR funcCmdTraceRaysKHR =
    (PFN_vkCmdTraceRaysKHR)vkGetInstanceProcAddr(
      g_framework->instance, "vkCmdTraceRaysKHR");
  funcCmdTraceRaysKHR(commandBuffer,
    &(rtpipeline->rtRgenRegion),
    &(rtpipeline->rtMissRegion),
    &(rtpipeline->rtHitRegion),
    &(rtpipeline->rtCallRegion), WIDTH, HEIGHT, 1);

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
  MeshSceneUniformBuffer msub{};
  msub.M = glm::mat4(1.0f);
  msub.V = glm::lookAt(
    glm::vec3(0, 0, 200),
    glm::vec3(0, 0, 0),
    glm::vec3(0, -1, 0));
  msub.P = glm::perspectiveFovRH_ZO(3.1415926f / 4,
    WIDTH * 1.0f, HEIGHT * 1.0f, 0.1f, 10000.0f);
  msub.is_base_mesh = (g_viz_mode == 4) ? 1 : 0;

  void* data;
  vkMapMemory(g_framework->device, mesh_perscene_cb_buffer_memory, 0, sizeof(MeshSceneUniformBuffer), 0, (void**)&data);
  memcpy(data, &msub, sizeof(msub));
  vkUnmapMemory(g_framework->device, mesh_perscene_cb_buffer_memory);

  g_framework->CreateCBVBuffer(mesh_perscene_cb_buffer, 0, sizeof(MeshSceneUniformBuffer), mesh_descriptor_set, 0);
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
    case 2: case 3: case 6: case 7: {
      recordRtCommandBuffer(g_framework->commandBuffer, imageIndex);
      break;
    }
    case 4: case 5: {
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
    //printf("tri[%u], level=%u, valuesOffset=%u, minDisp=%g, maxDisp=%g\n", triIdx, baryTri.subdivLevel, baryTri.valuesOffset, minDisp, maxDisp);

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

// Pack tris into clusters, and find uniq verts in the process
std::vector<MyFrameworkVk::VertexAndIndex> PackTrianglesIntoClusters(const MyModelStuff& model, uint32_t ntri_per_cluster) {
  std::vector<MyFrameworkVk::VertexAndIndex> ret;

  MyFrameworkVk::VertexAndIndex curr_vi;
  uint32_t nidxes = model.indices.size();
  uint32_t iidx = 0;
  while (iidx <= nidxes) {
    if (curr_vi.indices.size() >= ntri_per_cluster * 3 || iidx == nidxes) {
      ret.push_back(curr_vi);
      curr_vi = MyFrameworkVk::VertexAndIndex();
    }
    if (iidx == nidxes) {
      break;
    }
    uint32_t i0 = model.indices[iidx];
    uint32_t i1 = model.indices[iidx + 1];
    uint32_t i2 = model.indices[iidx + 2];
    glm::vec3 v0 = model.positions[i0];
    glm::vec3 v1 = model.positions[i1];
    glm::vec3 v2 = model.positions[i2];

    curr_vi.vertices.push_back(v0);
    curr_vi.vertices.push_back(v1);
    curr_vi.vertices.push_back(v2);
    curr_vi.indices.push_back(curr_vi.indices.size());
    curr_vi.indices.push_back(curr_vi.indices.size());
    curr_vi.indices.push_back(curr_vi.indices.size());
    iidx += 3;
  }
  printf("[PackTrianglesIntoClusters] %zu clusters.\n", ret.size());
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
      { VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
      { VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
    },
    1
  );

  VkDescriptorSetLayoutBinding mesh_dslb[11]{};
  mesh_dslb[0].binding = 0;
  mesh_dslb[0].descriptorCount = 1;
  mesh_dslb[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  mesh_dslb[0].pImmutableSamplers = nullptr;
  mesh_dslb[0].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;

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

  mesh_dslb[3] = mesh_dslb[2];
  mesh_dslb[3].binding = 3;

  mesh_dslb[4] = mesh_dslb[3];
  mesh_dslb[4].binding = 4;

  mesh_dslb[5] = mesh_dslb[4];
  mesh_dslb[5].binding = 5;

  mesh_dslb[6] = mesh_dslb[5];
  mesh_dslb[6].binding = 6;

  mesh_dslb[7] = mesh_dslb[6];
  mesh_dslb[7].binding = 7;

  mesh_dslb[8] = mesh_dslb[7];
  mesh_dslb[8].binding = 8;

  mesh_dslb[9] = mesh_dslb[8];
  mesh_dslb[9].binding = 9;

  mesh_dslb[10] = mesh_dslb[9];
  mesh_dslb[10].binding = 10;

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

  // Copy stuff
  std::vector<float> dmm_displacements_all;
  std::vector<uint32_t> dmm_displacements_offsets;
  for (const auto& d : g_dmm_displacements) {
    dmm_displacements_offsets.push_back(dmm_displacements_all.size());
    for (const auto f : d) {
      dmm_displacements_all.push_back(f);
    }
  }

  g_framework->CreateBufferForCPUSideData(dmm_displacements_all.data(), sizeof(float) * dmm_displacements_all.size(), mesh_dmm_displacements_buffer, mesh_dmm_displacements_buffer_memory);
  g_framework->CreateBufferForCPUSideData(dmm_displacements_offsets.data(), sizeof(uint32_t) * dmm_displacements_offsets.size(), mesh_dmm_displacements_offsets_buffer, mesh_dmm_displacements_offsets_buffer_memory);

  std::vector<uint32_t> dmm_levels_u32;
  for (uint32_t l : g_dmm_levels) {
    dmm_levels_u32.push_back(l);
  }
  g_framework->CreateBufferForCPUSideData(dmm_levels_u32.data(), sizeof(uint32_t) * dmm_levels_u32.size(), mesh_dmm_levels_buffer, mesh_dmm_levels_buffer_memory);

  std::vector<glm::uvec3> bary_levels_map_tris;
  std::vector<uint32_t> bary_levels_map_tris_offsets;
  for (uint32_t l = 0; l <= 5; l++) {
    baryutils::BaryLevelsMap::Level level = g_maps.getLevel(l);
    bary_levels_map_tris_offsets.push_back(bary_levels_map_tris.size());
    for (const auto& tri : level.triangles) {
      glm::uvec3 tri1(tri.a, tri.b, tri.c);
      bary_levels_map_tris.push_back(tri1);
    }
  }
  g_framework->CreateBufferForCPUSideData(bary_levels_map_tris.data(), sizeof(glm::uvec3) * bary_levels_map_tris.size(), mesh_bary_levels_map_triangles_buffer, mesh_bary_levels_map_triangles_buffer_memory);
  g_framework->CreateBufferForCPUSideData(bary_levels_map_tris_offsets.data(), sizeof(uint32_t) * bary_levels_map_tris_offsets.size(), mesh_bary_levels_map_triangles_offsets_buffer, mesh_bary_levels_map_triangles_offsets_buffer_memory);

  std::vector<glm::vec3> bary_wuvs;
  std::vector<uint32_t> bary_wuvs_offsets;
  for (uint32_t l = 0; l <= 5; l++) {
    baryutils::BaryLevelsMap::Level level = g_maps.getLevel(l);
    bary_wuvs_offsets.push_back(bary_wuvs.size());
    for (baryutils::BaryWUV_uint16 coord : level.coordinates) {
      float          mul = 1.0f / float(1 << l);
      glm::vec3 wuv;
      wuv.x = float(coord.w) * mul;
      wuv.y = float(coord.u) * mul;
      wuv.z = float(coord.v) * mul;
      bary_wuvs.push_back(wuv);
    }
  }
  g_framework->CreateBufferForCPUSideData(bary_wuvs.data(), sizeof(glm::vec3) * bary_wuvs.size(), mesh_bary_wuvs_buffer, mesh_bary_wuvs_buffer_memory);
  g_framework->CreateBufferForCPUSideData(bary_wuvs_offsets.data(), sizeof(uint32_t) * bary_wuvs_offsets.size(), mesh_bary_wuvs_offsets_buffer, mesh_bary_wuvs_offsets_buffer_memory);

  g_framework->CreateBufferForCPUSideData(base_model.per_vertex_normals.data(), sizeof(glm::vec3) * base_model.per_vertex_normals.size(), mesh_per_vertex_normals_buffer, mesh_per_vertex_normals_buffer_memory);

  // Set UAVs.
  g_framework->CreateUAVBuffer(mesh_dmm_displacements_buffer, 0, sizeof(float) * dmm_displacements_all.size(), mesh_descriptor_set, 3);
  g_framework->CreateUAVBuffer(mesh_dmm_displacements_offsets_buffer, 0, sizeof(uint32_t) * dmm_displacements_offsets.size(), mesh_descriptor_set, 5);
  g_framework->CreateUAVBuffer(mesh_dmm_levels_buffer, 0, sizeof(uint32_t) * dmm_levels_u32.size(), mesh_descriptor_set, 4);
  g_framework->CreateUAVBuffer(mesh_bary_levels_map_triangles_buffer, 0, sizeof(glm::uvec3) * bary_levels_map_tris.size(), mesh_descriptor_set, 6);
  g_framework->CreateUAVBuffer(mesh_bary_levels_map_triangles_offsets_buffer, 0, sizeof(uint32_t) * bary_levels_map_tris_offsets.size(), mesh_descriptor_set, 7);
  g_framework->CreateUAVBuffer(mesh_bary_wuvs_buffer, 0, sizeof(glm::vec3) * bary_wuvs.size(), mesh_descriptor_set, 8);
  g_framework->CreateUAVBuffer(mesh_bary_wuvs_offsets_buffer, 0, sizeof(uint32_t) * bary_wuvs_offsets.size(), mesh_descriptor_set, 9);
  g_framework->CreateUAVBuffer(mesh_per_vertex_normals_buffer, 0, sizeof(glm::vec3) * base_model.per_vertex_normals.size(), mesh_descriptor_set, 10);
}

static void fillDummyTriangleMinMaxs(baryutils::BaryBasicData& baryData)
{
  baryData.triangleMinMaxsInfo.elementCount = uint32_t(2 * baryData.triangles.size());
  baryData.triangleMinMaxsInfo.elementByteAlignment = 4;

  switch (baryData.valuesInfo.valueFormat)
  {
  case bary::Format::eDispC1_r11_unorm_block:
  case bary::Format::eR11_unorm_pack16:
  case bary::Format::eR11_unorm_packed_align32:
    baryData.triangleMinMaxsInfo.elementByteSize = uint32_t(sizeof(uint16_t));
    baryData.triangleMinMaxsInfo.elementFormat = bary::Format::eR11_unorm_pack16;
    baryData.triangleMinMaxs.resize(baryData.triangleMinMaxsInfo.elementByteSize * baryData.triangleMinMaxsInfo.elementCount);
    {
      uint16_t* minMaxs = reinterpret_cast<uint16_t*>(baryData.triangleMinMaxs.data());
      for (size_t i = 0; i < baryData.triangles.size(); i++)
      {
        minMaxs[i * 2 + 0] = 0;
        minMaxs[i * 2 + 1] = 0x7FF;
      }
    }
    break;
  case bary::Format::eR16_unorm:
    baryData.triangleMinMaxsInfo.elementByteSize = uint32_t(sizeof(uint16_t));
    baryData.triangleMinMaxsInfo.elementFormat = bary::Format::eR16_unorm;
    baryData.triangleMinMaxs.resize(baryData.triangleMinMaxsInfo.elementByteSize * baryData.triangleMinMaxsInfo.elementCount);
    {
      uint16_t* minMaxs = reinterpret_cast<uint16_t*>(baryData.triangleMinMaxs.data());
      for (size_t i = 0; i < baryData.triangles.size(); i++)
      {
        minMaxs[i * 2 + 0] = 0;
        minMaxs[i * 2 + 1] = 0xFFFF;
      }
    }
    break;
  case bary::Format::eR8_unorm:
    baryData.triangleMinMaxsInfo.elementByteSize = uint32_t(sizeof(uint8_t));
    baryData.triangleMinMaxsInfo.elementFormat = bary::Format::eR8_unorm;
    baryData.triangleMinMaxs.resize(baryData.triangleMinMaxsInfo.elementByteSize * baryData.triangleMinMaxsInfo.elementCount);
    {
      uint8_t* minMaxs = reinterpret_cast<uint8_t*>(baryData.triangleMinMaxs.data());
      for (size_t i = 0; i < baryData.triangles.size(); i++)
      {
        minMaxs[i * 2 + 0] = 0;
        minMaxs[i * 2 + 1] = 0xFF;
      }
    }
    break;
  case bary::Format::eR32_sfloat:
    baryData.triangleMinMaxsInfo.elementByteSize = uint32_t(sizeof(float));
    baryData.triangleMinMaxsInfo.elementFormat = bary::Format::eR32_sfloat;
    baryData.triangleMinMaxs.resize(baryData.triangleMinMaxsInfo.elementByteSize * baryData.triangleMinMaxsInfo.elementCount);
    {
      float* minMaxs = reinterpret_cast<float*>(baryData.triangleMinMaxs.data());
      for (size_t i = 0; i < baryData.triangles.size(); i++)
      {
        minMaxs[i * 2 + 0] = 0.0f;
        minMaxs[i * 2 + 1] = 1.0f;
      }
    }
    break;
  }
}

void LoadCompressedBaryForRT(const char* fn) {
  baryutils::BaryFileOpenOptions openOptions{};
  baryutils::BaryFile            baryFile{};
  bary::Result res = baryFile.open(fn, &openOptions);
  if (res != bary::Result::eSuccess) {
    printf("[LoadCompressedBaryForRT] Load fail. %d\n", static_cast<int>(res));
    exit(0);
  }

  res = baryFile.validate(bary::ValueSemanticType::eDisplacement);
  if (res != bary::Result::eSuccess) {
    printf("[LoadCompressedBaryForRT] Verify fail. %d\n", static_cast<int>(res));
  }

  bary::Format format = baryFile.m_content.basic.valuesInfo->valueFormat;
  printf("value format is %d\n", format);
  switch (format) {
    case bary::Format::eDispC1_r11_unorm_block: {
      g_bary_displacement_attrs.compressed = std::make_unique<baryutils::BaryBasicData>();
      g_bary_displacement_attrs.compressed->setData(baryFile.m_content.basic);
      // check if mips in file, if so load
      if (baryFile.getMisc().groupUncompressedMipsCount &&
          baryFile.getMisc().triangleUncompressedMipsCount &&
          baryFile.getMisc().uncompressedMipsInfo)
      {
        g_bary_displacement_attrs.compressedMisc = std::make_unique<baryutils::BaryMiscData>();
        g_bary_displacement_attrs.compressedMisc->setData(baryFile.m_content.misc);
      }

      if (g_bary_displacement_attrs.compressed->triangleMinMaxs.empty())
      {
        fillDummyTriangleMinMaxs(*g_bary_displacement_attrs.compressed);
      }
      break;
    }
    default: {
      printf("Don't know how to deal with this format ya. %d\n", format);
      break;
    }
  }
}

static uint32_t AlignUp(uint32_t x, uint32_t step) {
  return step * ((x - 1) / step + 1);
}

void BuildCompressedBaryForRT() {
  VkResult result = VK_ERROR_UNKNOWN;
  
  // Assume only 1 displacement set
  if (!g_bary_displacement_attrs.compressed) {
    printf("[BuildCompressedBaryForRT] Oh! either bary is not compressed or bary is empty.\n");
    return;
  }

  // Just one group
  std::vector<VkMicromapUsageEXT> *usages = &(dmm_attachment_info.usageCounts);

  bary::BasicView baryDescr = g_bary_displacement_attrs.compressed->getView();
  assert(baryDescr.groupHistogramRangesCount == 1 && "Oh! Don't know how to deal with >1 groups.");
  usages->resize(baryDescr.groupHistogramRanges[0].entryCount);
  const bary::HistogramEntry* histoEntries = 
    baryDescr.histogramEntries + baryDescr.groupHistogramRanges[0].entryFirst;
  const bary::Group& baryGroup = baryDescr.groups[0];
  for (uint32_t i = 0; i < usages->size(); i++) {
    usages->at(i).count = histoEntries[i].count;
    usages->at(i).format = histoEntries[i].blockFormat;
    usages->at(i).subdivisionLevel = histoEntries[i].subdivLevel;
  }

  // Just one build group
  uint32_t displacementID = 0;
  uint32_t displacementGroupID = 0;
  uint32_t inputAlignment{ 256 };
  uint32_t prim_offset = AlignUp(
    baryDescr.valuesInfo->valueByteSize * baryGroup.valueCount,
    inputAlignment
  );
  VkDeviceSize inputSize = prim_offset + (sizeof(VkMicromapTriangleEXT) * baryGroup.triangleCount);

  VkMicromapBuildInfoEXT buildInfo{};
  buildInfo.sType = VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT;
  buildInfo.type = VK_MICROMAP_TYPE_DISPLACEMENT_MICROMAP_NV;
  buildInfo.flags = 0;
  buildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
  buildInfo.dstMicromap = VK_NULL_HANDLE;
  buildInfo.usageCountsCount = usages->size();
  buildInfo.pUsageCounts = usages->data();
  buildInfo.data.deviceAddress = 0;
  buildInfo.triangleArray.deviceAddress = 0;
  buildInfo.triangleArrayStride = 0;

  VkMicromapBuildSizesInfoEXT sizeInfo{};
  sizeInfo.sType = VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT;
  if (g_framework->hasDMM) {
    PFN_vkGetMicromapBuildSizesEXT funcGetMicromapBuildSizes =
      (PFN_vkGetMicromapBuildSizesEXT)vkGetInstanceProcAddr(
        g_framework->instance, "vkGetMicromapBuildSizesEXT");
    funcGetMicromapBuildSizes(g_framework->device,
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &sizeInfo);
    printf("[BuildCompressedBaryForRT] MicroMap size: %llu\n", sizeInfo.micromapSize);

    g_framework->CreateBuffer(sizeInfo.micromapSize,
      VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT
      | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      g_micromap_buf, g_micromap_memory);

    VkMicromapCreateInfoEXT mci{};
    mci.sType = VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT;
    mci.createFlags = 0;
    mci.buffer = g_micromap_buf;
    mci.offset = 0;
    mci.size = sizeInfo.micromapSize;
    mci.type = VK_MICROMAP_TYPE_DISPLACEMENT_MICROMAP_NV;
    mci.deviceAddress = 0;

    auto func_vkCreateMicromap = (PFN_vkCreateMicromapEXT)vkGetInstanceProcAddr(
      g_framework->instance, "vkCreateMicromapEXT");
    if (func_vkCreateMicromap(g_framework->device, &mci, nullptr, &(dmm_attachment_info.dmmMicromap)) != VK_SUCCESS) {
      printf("[BuildCompressedBaryForRT] Failed to create VkMicromap\n");
      exit(0);
    }

    VkBuffer dmmScratchBuffer{};
    VkDeviceMemory dmmScratchMemory{};
    g_framework->CreateBuffer(sizeInfo.buildScratchSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
      | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT,
      0,
      dmmScratchBuffer, dmmScratchMemory);

    // Organize input buffer
    VkBuffer dmmInputBuffer{};
    VkDeviceMemory dmmInputMemory{};
    g_framework->CreateBuffer(inputSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
      | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      dmmInputBuffer, dmmInputMemory);
    char* mapped;
    vkMapMemory(g_framework->device, dmmInputMemory, 0, inputSize, 0, (void**)&mapped);
    memcpy(mapped,
      baryDescr.values + baryGroup.valueFirst * baryDescr.valuesInfo->valueByteSize,
      baryDescr.valuesInfo->valueByteSize * baryGroup.valueCount);
    memcpy(mapped + prim_offset,
      baryDescr.triangles + baryGroup.triangleFirst,
      sizeof(VkMicromapTriangleEXT) * baryGroup.triangleCount);
    vkUnmapMemory(g_framework->device, dmmInputMemory);
    VkDeviceAddress dmm_input_addr = g_framework->GetBufferDeviceAddress(dmmInputBuffer);

    // Build it
    VkMicromapBuildInfoEXT buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT;
    buildInfo.type = VK_MICROMAP_TYPE_DISPLACEMENT_MICROMAP_NV;
    buildInfo.flags = 0;
    buildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    buildInfo.dstMicromap = dmm_attachment_info.dmmMicromap;
    buildInfo.usageCountsCount = usages->size();
    buildInfo.pUsageCounts = usages->data();
    buildInfo.scratchData.deviceAddress = g_framework->GetBufferDeviceAddress(dmmScratchBuffer);
    buildInfo.data.deviceAddress = dmm_input_addr;
    buildInfo.triangleArray.deviceAddress = dmm_input_addr + prim_offset;
    buildInfo.triangleArrayStride = sizeof(VkMicromapTriangleEXT);

    VkCommandBuffer commandBuffer = g_framework->BeginSingleTimeCommands();
    PFN_vkCmdBuildMicromapsEXT funcCmdBuildMicromaps =
      (PFN_vkCmdBuildMicromapsEXT)vkGetInstanceProcAddr(
        g_framework->instance, "vkCmdBuildMicromapsEXT");
    funcCmdBuildMicromaps(commandBuffer, 1, &buildInfo);
    g_framework->EndSingleTimeCommands(commandBuffer);

    vkDestroyBuffer(g_framework->device, dmmScratchBuffer, nullptr);
    vkFreeMemory(g_framework->device, dmmScratchMemory, nullptr);
    vkDestroyBuffer(g_framework->device, dmmInputBuffer, nullptr);
    vkFreeMemory(g_framework->device, dmmInputMemory, nullptr);

    // Load displacement directions
    uint32_t ddsize = base_model.per_vertex_normals.size() * sizeof(f16vec4);
    g_framework->CreateBuffer(ddsize,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      g_dmm_displacement_vector_buffer, g_dmm_displacement_vector_memory);
    {
      vkMapMemory(g_framework->device, g_dmm_displacement_vector_memory, 0, ddsize, 0, (void**)&mapped);
      f16vec4* ptr = reinterpret_cast<f16vec4*>(mapped);
      for (uint32_t i = 0; i < base_model.per_vertex_normals.size(); i++) {
        glm::vec3 nrm = base_model.per_vertex_normals[i];
        ptr[i] = { float16_t(nrm.x), float16_t(nrm.y), float16_t(nrm.z), 0 };
      }
      vkUnmapMemory(g_framework->device, g_dmm_displacement_vector_memory);

      dmm_attachment_info.displacementVectorBufferAddress = g_framework->GetBufferDeviceAddress(g_dmm_displacement_vector_buffer);
      dmm_attachment_info.displacementVectorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
      dmm_attachment_info.displacementVectorStride = 8;
    }

    // Load displacement bias and scale
    uint32_t bssize = base_model.per_vertex_normals.size() * sizeof(float) * 2;
    g_framework->CreateBuffer(bssize,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      g_dmm_displacement_bias_and_scale_buffer, g_dmm_displacement_bias_and_scale_memory);
    {
      vkMapMemory(g_framework->device, g_dmm_displacement_bias_and_scale_memory, 0, bssize, 0, (void**)&mapped);
      float* ptr = reinterpret_cast<float*>(mapped);
      for (uint32_t i = 0; i < base_model.per_vertex_normals.size(); i++) {
        ptr[i*2] = 0.0f;
        ptr[i*2 + 1] = 1.0f;
      }
      vkUnmapMemory(g_framework->device, g_dmm_displacement_bias_and_scale_memory);

      dmm_attachment_info.displacementBiasAndScaleBufferAddress = g_framework->GetBufferDeviceAddress(g_dmm_displacement_bias_and_scale_buffer);
      dmm_attachment_info.displacementBiasAndScaleFormat = VK_FORMAT_R32G32_SFLOAT;
      dmm_attachment_info.displacementBiasAndScaleStride = 8;
    }

    // Manually come up with an index buffer (Tri --> micromap)
    uint32_t ibsize = base_model.indices.size() / 3 * sizeof(uint32_t);
    g_framework->CreateBuffer(ibsize,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      g_dmm_tri_idx_buffer, g_dmm_tri_idx_memory);
    {
      vkMapMemory(g_framework->device, g_dmm_tri_idx_memory, 0, bssize, 0, (void**)&mapped);
      uint32_t* ptr = reinterpret_cast<uint32_t*>(mapped);
      for (uint32_t i = 0; i < base_model.indices.size() / 3; i++) {
        ptr[i] = i;
      }
      vkUnmapMemory(g_framework->device, g_dmm_tri_idx_memory);
      dmm_attachment_info.indexBufferAddress = g_framework->GetBufferDeviceAddress(g_dmm_tri_idx_buffer);
    }
  }
}

int main(int argv, char** argc) {
  printf("Hey.\n");
  base_model = LoadModel("murex_romosus.obj");
  LoadBary("murex_romosus.bary");
  naive_utris = PopulateDisplacedMicroTriangles(base_model, g_dmm_displacements, g_dmm_levels);
  g_framework = new MyFrameworkVk();
  g_framework->InitWindow("VK DMM CLAS BLAS Comparison", WIDTH, HEIGHT, KeyCallback);
  g_window = g_framework->window;
  g_num_indices = base_model.indices.size();
  g_num_vertices = base_model.positions.size();

  LoadCompressedBaryForRT("umesh_Murex_Romosus_compressed.bary");

  // InitVulkan
  g_framework->InitDeviceAndCommandQ();
  g_framework->InitSwapchain();
  g_framework->InitRenderPassAndFramebuffers(true);
  g_framework->CreateSyncObjects();
  g_framework->InitImGui();
  g_framework->InitImGuiRenderPass();
  g_framework->CreateImGuiFramebuffers();

  // DMM
  if (g_framework->hasDMM) {
    BuildCompressedBaryForRT();
  }

  // Cluster
  if (g_framework->hasCLAS) {
    std::vector<MyFrameworkVk::VertexAndIndex> clusters = PackTrianglesIntoClusters(naive_utris, 64);
    g_framework->BuildClusteredBLAS(clustered_blas, clustered_blas_result_buffer, clustered_blas_result_memory, clusters, &clustered_blas_build_info);
    g_framework->BuildTLAS(clustered_tlas, { clustered_blas }, { clustered_blas_result_buffer }, clustered_tlas_result_buffer, clustered_tlas_result_memory);
  }

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
  size_t vb_size = sizeof(glm::vec3) * base_model.positions.size();
  g_framework->CreateBuffer(vb_size, usage, props,
    vertex_buffer, vertex_buffer_memory
  );
  size_t ib_size = sizeof(uint32_t) * base_model.indices.size();
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
  memcpy(data, base_model.positions.data(), vb_size);
  vkUnmapMemory(g_framework->device, vertex_buffer_memory);
  vkMapMemory(g_framework->device, index_buffer_memory, 0, ib_size, 0, &data);
  memcpy(data, base_model.indices.data(), ib_size);
  vkUnmapMemory(g_framework->device, index_buffer_memory);
  vkMapMemory(g_framework->device, normals_memory, 0, normals_size, 0, &data);
  memcpy(data, base_model.normals.data(), normals_size);
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
  g_framework->CreateBuffer(sizeof(MeshSceneUniformBuffer), usage, props, mesh_perscene_cb_buffer, mesh_perscene_cb_buffer_memory);
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
  rsli.use_dmm = (g_framework->hasDMM);
  g_framework->CreateMyRtPipeline(&g_my_rt_pipeline, rsli);

  if (g_framework->hasDMM) {
    MyFrameworkVk::MyRtShaderListInfo rsli_dmm = rsli;
    rsli_dmm.closest_hit_shader = "shaders/rchit_dmm.spv";
    g_framework->CreateMyRtPipeline(&g_my_rt_dmm_pipeline, rsli_dmm);
  }

  if (g_framework->hasCLAS) {
    MyFrameworkVk::MyRtShaderListInfo rsli_clas = rsli;
    rsli_clas.closest_hit_shader = "shaders/rchit_clas.spv";
    rsli_clas.use_clas = true;
    g_framework->CreateMyRtPipeline(&g_my_rt_clas_pipeline, rsli_clas);
  }

  // Blas
  g_framework->BuildBLAS(blas, blas_result_buffer, blas_result_memory, vertex_buffer, index_buffer, base_model.positions.size(), g_num_indices, sizeof(glm::vec3), nullptr, &blas_build_info);
  g_framework->BuildTLAS(tlas, { blas }, { blas_result_buffer }, tlas_result_buffer, tlas_result_memory);
  g_framework->BuildBLAS(naive_utris_blas, naive_utris_blas_result_buffer, naive_utris_blas_result_memory,
    naive_utris_vertex_buffer, naive_utris_index_buffer,
    naive_utris.positions.size(), g_num_naive_utris_indices, sizeof(glm::vec3), nullptr, &naive_utris_blas_build_info);
  g_framework->BuildTLAS(naive_utris_tlas, { naive_utris_blas }, { naive_utris_blas_result_buffer }, naive_utris_tlas_result_buffer, naive_utris_tlas_result_memory);
  if (g_framework->hasDMM) {
    g_framework->BuildBLASWithDMM(dmm_blas, dmm_blas_result_buffer, dmm_blas_result_memory, vertex_buffer, index_buffer, base_model.positions.size(), g_num_indices, sizeof(glm::vec3), &dmm_attachment_info, &dmm_blas_build_info);
    g_framework->BuildTLAS(dmm_tlas, { dmm_blas }, { dmm_blas_result_buffer }, dmm_tlas_result_buffer, dmm_tlas_result_memory);
  }

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