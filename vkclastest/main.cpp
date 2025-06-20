#define _CRT_SECURE_NO_WARNINGS

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stdio.h>
#include <stdint.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <optional>
#include <vector>

std::string g_gltf_filename = "bunny.gltf";

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

class HelloClasApplication;
HelloClasApplication* g_app{};
GLFWwindow* window{};
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
bool g_is_rt{ false };
bool g_is_cluster{ false };  // applies to both rast and rt
bool g_is_rotate{ true };
constexpr bool UseIndirect = true;  // TODO: Fix normals
const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;
std::vector<const char*> validationLayers = {
  "VK_LAYER_KHRONOS_validation"
};
#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif
const uint32_t MAX_FRAMES_IN_FLIGHT = 3;

static uint32_t AlignUp(uint32_t x, uint32_t align) {
  return align * ((x - 1) / align + 1);
}

struct PerSceneUniformBuffer {
  glm::mat4 M, V, P;
};

struct RtPerSceneUniformBuffer {
  glm::mat4 inv_view, inv_proj;
  int is_cluster;
};

struct RtPerSceneVertexProcessingDataBuffer {
  glm::mat4 M;
  uint32_t num_verts;
};

struct QueueFamilyIndices {
  std::optional<uint32_t> graphicsFamily;
  std::optional<uint32_t> presentFamily;
  bool isComplete() {
    return graphicsFamily.has_value() && presentFamily.has_value();
  }
};

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

std::vector<const char*> deviceExtensions = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
  VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
  VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
  VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
  VK_KHR_SPIRV_1_4_EXTENSION_NAME,
  VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
  VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
  VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME,
  VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
  VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME,

  VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME,
  VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME
};

struct MyIndirectClusterDraw {
  VkDrawIndexedIndirectCommand command;
};

void SetWindowTitle() {
  if (!g_is_rt) {
    if (!g_is_cluster) {
      glfwSetWindowTitle(window, "Vulkan CLAS (rast)");
    }
    else {
      glfwSetWindowTitle(window, "Vulkan CLAS (rast, cluster)");
    }
  }
  else {
    if (!g_is_cluster) {
      glfwSetWindowTitle(window, "Vulkan CLAS (RT)");
    }
    else {
      glfwSetWindowTitle(window, "Vulkan CLAS (RT, cluster)");
    }
  }
}

void initWindow() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  window = glfwCreateWindow(800, 600, "Vulkan window", nullptr, nullptr);
  glfwSetKeyCallback(window, KeyCallback);
  SetWindowTitle();
}

bool checkValidationLayerSupport() {
  uint32_t layerCount{};
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
  for (const auto& lp : availableLayers) {
    for (const char* val : validationLayers) {
      if (!strcmp(val, lp.layerName)) {
        return true;
      }
    }
  }
  return false;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageType,
  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
  void* pUserData) {
  printf("Validation error: %s\n", pCallbackData->pMessage);
  return VK_FALSE;
}

void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
  createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity =
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
    | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
    | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType =
    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
    | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
    | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
  createInfo.pUserData = nullptr;
}

std::vector<const char*> getRequiredExtensions() {
  uint32_t glfwExtensionsCount = 0;
  const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);
  std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionsCount);
  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  return extensions;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
  const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
  const VkAllocationCallbacks* pAllocator,
  VkDebugUtilsMessengerEXT* pDebugMessenger) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
    instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  }
  else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
  for (const auto& f : availableFormats) {
    if (f.format == VK_FORMAT_R8G8B8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return f;
    }
  }
  return availableFormats.at(0);
}

VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
  for (const auto& p : availablePresentModes) {
    if (p == VK_PRESENT_MODE_MAILBOX_KHR) {
      return p;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  }
  else {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    VkExtent2D actualExtent = { uint32_t(width), uint32_t(height) };
    actualExtent.width = std::clamp(actualExtent.width,
      capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
      capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return actualExtent;
  }
}

static std::vector<char> readFile(const std::string& filename) {
  std::ifstream ifs(filename, std::ios::ate | std::ios::binary);
  if (!ifs.is_open()) {
    throw std::runtime_error("Failed to open file");
  }
  size_t fileSize = (size_t)ifs.tellg();
  std::vector<char> buffer(fileSize);
  ifs.seekg(0);
  ifs.read(buffer.data(), fileSize);
  ifs.close();
  return buffer;
}

class HelloClasApplication {
public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }
  bool should_exit{ false };

private:
  void initVulkan() {
    createInstance();
    if (enableValidationLayers) {
      setupDebugMessenger();
    }
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createCommandPool();
    createCommandBuffer();

    createSwapChain();
    createImageViews();
    createRenderPass();
    createDepthResources();
    createUniformBuffer();

    readGLTF();
    readClusters();
    createVertexIndexNormalBuffer();

    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets();

    createRtUniformBuffer();
    createRtOutputImages();
    createRtDescriptorSetLayout();
    createRtDescriptorPool();
    createRtDescriptorSets();

    createAS();
    createClusterAS();

    createGraphicsPipeline();
    createRtPipeline();
    createRtSBT();

    createComputeDescriptorSetLayout();
    createComputeDescriptorPool();
    createComputeDescriptorSets();
    createComputePipeline();

    createFramebuffers();
    createSyncObjects();
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window) && !should_exit) {
      glfwPollEvents();
      drawFrame();
    }
    vkDeviceWaitIdle(device);
  }

  VkDeviceAddress getBufferDeviceAddress(VkBuffer& buf) {
    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.buffer = buf;
    addrInfo.pNext = nullptr;
    return vkGetBufferDeviceAddress(device, &addrInfo);
  }

  void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    // Update descriptor set for indirect/not indirect
    {
      VkDescriptorBufferInfo dbi[2]{};
      if (g_is_cluster) {
        dbi[0].buffer = clusterNormalsBufferForIndirect;
        dbi[0].offset = 0;
        dbi[0].range = sizeof(glm::vec3) * cluster_normals.size();
      }
      else {
        dbi[0].buffer = normalBuffer;
        dbi[0].offset = 0;
        dbi[0].range = sizeof(glm::vec3) * normals.size();
      }

      // For non-cluster, only the first entry, 0, will be used, so it's still correct
      dbi[1].buffer = clusterNormalsOffsetBufferForIndirect;
      dbi[1].offset = 0;
      dbi[1].range = sizeof(uint32_t) * vertex_and_indices.size();

      VkWriteDescriptorSet writeDesc[2]{};
      writeDesc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDesc[0].dstSet = descriptorSets[0];
      writeDesc[0].dstBinding = 0;
      writeDesc[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writeDesc[0].descriptorCount = 1;
      writeDesc[0].pBufferInfo = &(dbi[0]);
      writeDesc[1] = writeDesc[0];
      writeDesc[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writeDesc[1].dstBinding = 2;
      writeDesc[1].pBufferInfo = &(dbi[1]);

      vkUpdateDescriptorSets(device, _countof(writeDesc), writeDesc, 0, nullptr);
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("Failed to begin command buffer");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = swapChainExtent;
    VkClearValue clearColor = { {{0.3f, 0.3f, 0.3f, 1.0f }} };
    VkClearValue clearDepth{};
    clearDepth.depthStencil = { 1.f, 0 };
    VkClearValue clearValues[] = { clearColor, clearDepth };
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
      1, &(descriptorSets[0]), 0, nullptr);

    VkViewport viewport{};
    viewport.x = viewport.y = 0;
    viewport.width = (float)(swapChainExtent.width);
    viewport.height = (float)(swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkDeviceSize zero{ 0 };
    if (!g_is_cluster) {
      vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &zero);
      vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(commandBuffer, indices.size(), 1, 0, 0, 0);
    }
    else {
      if (!UseIndirect) {
        for (uint32_t i = 0; i < vertex_and_indices.size(); i++) {
          vkCmdBindVertexBuffers(commandBuffer, 0, 1, &(clusterVertexBuffers[i]), &zero);
          vkCmdBindIndexBuffer(commandBuffer, clusterIndexBuffers[i], 0, VK_INDEX_TYPE_UINT32);
          vkCmdDrawIndexed(commandBuffer, vertex_and_indices[i].indices.size(), 1, 0, 0, 0);
        }
      }
      else {
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &clusterVertexBufferForIndirect, &zero);
        vkCmdBindIndexBuffer(commandBuffer, clusterIndexBufferForIndirect, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(
          commandBuffer,
          clusterIndirectCommandBuffer,
          0,
          vertex_and_indices.size(),
          sizeof(MyIndirectClusterDraw)
        );
      }
    }

    vkCmdEndRenderPass(commandBuffer);
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not end command buffer");
    }
  }

  void recordRtCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    // Update descriptor
    {
      VkWriteDescriptorSet writes[6]{};

      // Output image
      writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[0].descriptorCount = 1;
      writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      writes[0].dstArrayElement = 0;
      writes[0].dstBinding = 1;
      writes[0].dstSet = rtDescriptorSet;
      VkDescriptorImageInfo ii{};
      ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      ii.imageView = rtOutputImageViews[imageIndex];
      ii.sampler = VK_NULL_HANDLE;
      writes[0].pImageInfo = &ii;

      VkDescriptorBufferInfo bi[4]{};

      // Vertex buffer
      writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[1].descriptorCount = 1;
      writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[1].dstBinding = 3;
      writes[1].dstSet = rtDescriptorSet;
      if (g_is_cluster) {
        bi[0].buffer = clusterVertexBufferForIndirect;
        bi[0].range = sizeof(glm::vec3) * g_cluster_vert_count;
      }
      else {
        bi[0].buffer = vertexBuffer;
        bi[0].range = sizeof(glm::vec3) * g_vertex_count;
      }
      bi[0].offset = 0;
      writes[1].pBufferInfo = &(bi[0]);

      // Index buffer
      writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[2].descriptorCount = 1;
      writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[2].dstBinding = 4;
      writes[2].dstSet = rtDescriptorSet;
      if (g_is_cluster) {
        bi[1].buffer = clusterIndexBufferForIndirect;
        bi[1].range = sizeof(uint32_t) * g_cluster_tri_count * 3;
      }
      else {
        bi[1].buffer = indexBuffer;
        bi[1].range = sizeof(uint32_t) * g_index_count;
      }
      bi[1].offset = 0;
      writes[2].pBufferInfo = &(bi[1]);

      // TLAS
      VkWriteDescriptorSetAccelerationStructureKHR writeDescAS{};
      writeDescAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
      writeDescAS.accelerationStructureCount = 1;
      if (!g_is_cluster)
        writeDescAS.pAccelerationStructures = &tlas;
      else
        writeDescAS.pAccelerationStructures = &clusterTlas;
      writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[3].dstSet = rtDescriptorSet;
      writes[3].dstBinding = 0;
      writes[3].descriptorCount = 1;
      writes[3].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
      writes[3].pNext = &writeDescAS;
      
      // Normal offset
      writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[4].descriptorCount = 1;
      writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[4].dstBinding = 5;
      writes[4].dstSet = rtDescriptorSet;
      bi[2].buffer = clusterNormalsOffsetBufferForIndirect;
      bi[2].offset = 0;
      bi[2].range = sizeof(uint32_t) * vertex_and_indices.size();
      writes[4].pBufferInfo = &(bi[2]);

      // Vertex offset
      writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[5].descriptorCount = 1;
      writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[5].dstBinding = 6;
      writes[5].dstSet = rtDescriptorSet;
      bi[3].buffer = clusterVertexIdxOffsetBuffer;
      bi[3].offset = 0;
      bi[3].range = sizeof(uint32_t) * vertex_and_indices.size();
      writes[5].pBufferInfo = &(bi[3]);

      vkUpdateDescriptorSets(device, _countof(writes), writes, 0, nullptr);
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("Failed to begin command buffer");
    }

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipelineLayout, 0,
      1, &rtDescriptorSet, 0, nullptr);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipeline);
    PFN_vkCmdTraceRaysKHR funcCmdTraceRaysKHR =
      (PFN_vkCmdTraceRaysKHR)vkGetInstanceProcAddr(
        instance, "vkCmdTraceRaysKHR");
    funcCmdTraceRaysKHR(commandBuffer,
      &rtRGenRegion,
      &rtMissRegion,
      &rtHitRegion,
      &rtCallRegion,
      WIDTH, HEIGHT, 1);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = rtOutputImages[imageIndex];
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 0, nullptr, 1, &barrier);

    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = swapChainImages[imageIndex];
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkImageBlit blit{};
    blit.srcOffsets[1] = { WIDTH, HEIGHT, 1 };
    blit.dstOffsets[1] = { WIDTH, HEIGHT, 1 };
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.dstSubresource.layerCount = 1;

    vkCmdBlitImage(commandBuffer,
      rtOutputImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1, &blit, VK_FILTER_LINEAR);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0, 0, nullptr, 0, nullptr, 1, &barrier);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = rtOutputImages[imageIndex];
    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not end RT command buffer");
    }
  }

  void callComputeShader(VkCommandBuffer commandBuffer) {
    // Update cb
    uint8_t* mapped{};
    vkMapMemory(device, rtPerSceneVertexProcessingBufferMemory, 0, sizeof(RtPerSceneVertexProcessingDataBuffer), 0, (void**)(&mapped));
    RtPerSceneVertexProcessingDataBuffer rtpb{};
    float angle = g_is_rotate ? glfwGetTime() : 0;
    rtpb.M = glm::mat4(1);
    rtpb.M = glm::rotate(rtpb.M, angle * 3.14159f / 2, glm::vec3(0, 1, 0));
    rtpb.num_verts = g_vertex_count;
    memcpy(mapped, &rtpb, sizeof(RtPerSceneVertexProcessingDataBuffer));
    vkUnmapMemory(device, rtPerSceneVertexProcessingBufferMemory);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("Failed to begin compute command buffer");
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
      computePipelineLayout, 0, 1, &computeDescriptorSet, 0, 0);

    vkCmdDispatch(commandBuffer, 256, 1, 1);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Failed to record compute command buffer");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = &computeFinishedSemaphore;
    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, computeInFlightFence) != VK_SUCCESS) {
      throw std::runtime_error("Failed to submit");
    }

    vkWaitForFences(device, 1, &computeInFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &computeInFlightFence);

    vkResetCommandBuffer(commandBuffer, 0);

    // Read something back
    glm::vec3 v0, v1;
    vkMapMemory(device, vertexBufferMemory, 0, sizeof(glm::vec3), 0, (void**)&mapped);
    memcpy(&v0, mapped, sizeof(glm::vec3));
    vkUnmapMemory(device, vertexBufferMemory);

    vkMapMemory(device, vertexBufferDisplacedMemory, 0, sizeof(glm::vec3), 0, (void**)&mapped);
    memcpy(&v1, mapped, sizeof(glm::vec3));
    vkUnmapMemory(device, vertexBufferDisplacedMemory);
  }

  void drawFrame() {
    // Update cb
    {
      PerSceneUniformBuffer psub{};
      psub.M = glm::mat4(1.0f);

      float angle = g_is_rotate ? glfwGetTime() : 0;
      psub.M = glm::rotate(psub.M, angle * 3.14159f / 2, glm::vec3(0, 1, 0));

      psub.V = glm::lookAt(
        glm::vec3(0, 0, 10),
        glm::vec3(0, 0, 0),
        glm::vec3(0,-1, 0));
      psub.P = glm::perspectiveFovRH_ZO(3.1415926f / 4,
        WIDTH * 1.0f, HEIGHT * 1.0f, 0.1f, 10000.0f);
      //(3.1415926f / 4, WIDTH * 1.0f / HEIGHT, 0.1f, 10000.0f);

      uint8_t* mapped;
      vkMapMemory(device, perSceneUniformBufferMemory, 0, sizeof(PerSceneUniformBuffer), 0, (void**)&mapped);
      memcpy(mapped, &psub, sizeof(psub));
      vkUnmapMemory(device, perSceneUniformBufferMemory);

      RtPerSceneUniformBuffer rtpsub{};
      rtpsub.inv_proj = glm::inverse(psub.P);
      rtpsub.inv_view = glm::inverse(psub.V);
      rtpsub.is_cluster = (int)g_is_cluster;
      vkMapMemory(device, perSceneRtUniformBufferMemory, 0, sizeof(RtPerSceneUniformBuffer), 0, (void**)&mapped);
      memcpy(mapped, &rtpsub, sizeof(rtpsub));
      vkUnmapMemory(device, perSceneRtUniformBufferMemory);
    }

    //
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);
    
    uint32_t imageIndex;
    vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    vkResetCommandBuffer(commandBuffer, 0);

    if (!g_is_rt) {
      recordCommandBuffer(commandBuffer, imageIndex);
    }
    else {
      callComputeShader(commandBuffer);
      updateAS();
      recordRtCommandBuffer(commandBuffer, imageIndex);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
    submitInfo.pWaitSemaphores = waitSemaphores;
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence) != VK_SUCCESS) {
      throw std::runtime_error("Failed to submit draw command buffer");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr;

    if (vkQueuePresentKHR(presentQueue, &presentInfo) != VK_SUCCESS) {
      throw std::runtime_error("Failed to present");
    }
  }

  void cleanup() {
    vkDestroyPipelineLayout(device, rtPipelineLayout, nullptr);
    vkDestroyPipeline(device, rtPipeline, nullptr);
    vkDestroyBuffer(device, sbtBuffer, nullptr);
    vkFreeMemory(device, sbtMemory, nullptr);
    vkDestroyBuffer(device, blasResultBuffer, nullptr);
    vkFreeMemory(device, blasResultMemory, nullptr);
    PFN_vkDestroyAccelerationStructureKHR funcDestroyAccelerationStructureKHR =
      (PFN_vkDestroyAccelerationStructureKHR)vkGetInstanceProcAddr(
        instance, "vkDestroyAccelerationStructureKHR");
    assert(funcDestroyAccelerationStructureKHR);
    funcDestroyAccelerationStructureKHR(device, blas, nullptr);
    vkDestroyBuffer(device, tlasInstancesBuffer, nullptr);
    vkFreeMemory(device, tlasInstancesMemory, nullptr);
    vkDestroyBuffer(device, tlasResultBuffer, nullptr);
    vkFreeMemory(device, tlasResultMemory, nullptr);
    funcDestroyAccelerationStructureKHR(device, tlas, nullptr);
    vkDestroyBuffer(device, clasBuildTriangleClusterInfosBuffer, nullptr);
    vkFreeMemory(device, clasBuildTriangleClusterInfosMemory, nullptr);
    vkDestroyBuffer(device, clusterBuffer, nullptr);
    vkFreeMemory(device, clusterMemory, nullptr);
    vkDestroyBuffer(device, clusterBlasBuffer, nullptr);
    vkFreeMemory(device, clusterBlasMemory, nullptr);
  }

  void createInstance() {
    if (enableValidationLayers) {
      bool ok = checkValidationLayerSupport();
      printf("Validation layers enabled, check result=%d\n", ok);
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    std::vector<const char*> extensions = getRequiredExtensions();

    createInfo.enabledExtensionCount = uint32_t(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

    if (enableValidationLayers) {
      createInfo.enabledLayerCount = uint32_t(validationLayers.size());
      createInfo.ppEnabledLayerNames = validationLayers.data();

      populateDebugMessengerCreateInfo(debugCreateInfo);
      createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else {
      createInfo.enabledLayerCount = 0;
      createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create instance");
    }

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    printf("%u Instance extensions:\n", extensionCount);
    std::vector<VkExtensionProperties> instanceExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, instanceExtensions.data());
    for (uint32_t i = 0; i < instanceExtensions.size(); i++) {
      auto& ex = instanceExtensions.at(i);
      printf("  [%u]: %s\n", i, ex.extensionName);
    }

    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    printf("%u Instance Layers\n", layerCount);
    for (uint32_t i = 0; i < layerCount; i++) {
      auto& l = availableLayers[i];
      printf("  [%u]: %s\n", i, l.layerName);
    }
  }

  void setupDebugMessenger() {
    if (!enableValidationLayers) return;
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);
    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
      throw std::runtime_error("Failed to set up debug messenger");
    }
  }

  void createSurface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create surface");
    }
  }

  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
    const bool verbose = (device != physicalDevice);
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount > 0) {
      details.formats.resize(formatCount);
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (verbose) {
      printf("%u surface formats, %u present modes supported.\n", formatCount, presentModeCount);
    }
    if (presentModeCount > 0) {
      details.presentModes.resize(presentModeCount);
      vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }
    return details;
  }

  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices{};
    uint32_t count{};
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> qprops(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, qprops.data());
    int idx = 0;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);
    bool verbose = (device != physicalDevice);
    if (verbose)
      printf("Device \"%s\" has %zu queue families\n", props.deviceName, qprops.size());
    for (const VkQueueFamilyProperties qp : qprops) {
      if (qp.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        indices.graphicsFamily = idx;
      }

      if (verbose) {
        printf("  %u queues (flag=0x%X):", qp.queueCount, qp.queueFlags);
        if (qp.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
          printf(" Graphics");
        }
        if (qp.queueFlags & VK_QUEUE_COMPUTE_BIT) {
          printf(" Compute");
        }
        if (qp.queueFlags & VK_QUEUE_TRANSFER_BIT) {
          printf(" Transfer");
        }
        if (qp.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) {
          printf(" SparseBinding");
        }
        if (qp.queueFlags & VK_QUEUE_PROTECTED_BIT) {
          printf(" Protected");
        }
        if (qp.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) {
          printf(" VideoDecode");
        }
        if (qp.queueFlags & 0x40 /* VK_QUEUE_VIDEO_ENCODE_BIT_KHR */) {
          printf(" VideoEncode");
        }
        if (qp.queueFlags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) {
          printf(" OpticalFlow");
        }
      }

      VkBool32 presentSupport = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, idx, surface, &presentSupport);
      if (presentSupport) {
        if (verbose)
          printf(" Present");
        indices.presentFamily = idx;
      }

      idx++;
      if (verbose)
        printf("\n");
    }
    return indices;
  }

  bool do_checkDeviceExtensionSupport(VkPhysicalDevice device, std::vector<const char*> devExts) {
    uint32_t extensionCount;  // 188 on 660M, 187 on 6550M
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
    std::set<std::string> requiredExtensions(devExts.begin(), devExts.end());
    for (const VkExtensionProperties& ep : extensions) {
      if (requiredExtensions.count(ep.extensionName)) {
        requiredExtensions.erase(ep.extensionName);
      }
    }
    return requiredExtensions.empty();
  }

  bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
    return do_checkDeviceExtensionSupport(device, deviceExtensions);
  }

  bool isDeviceSuitable(VkPhysicalDevice device) {
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);


    // Find memory type index
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(device, &memoryProperties);
    printf("Memory heaps:\n");
    for (uint32_t int_h = 0; int_h < memoryProperties.memoryHeapCount; int_h++) {
      printf(" [%u]:", int_h);
      VkMemoryHeapFlags f = memoryProperties.memoryHeaps[int_h].flags;
      if (f & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        printf(" DeviceLocal");
      }
      if (f & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT) {
        printf(" MultiInstance");
      }
      printf("\n");
    }
    printf("Memory types:\n");
    for (uint32_t int_ty = 0; int_ty < memoryProperties.memoryTypeCount; int_ty++) {
      printf(" [%u], heap[%d]:", int_ty, memoryProperties.memoryTypes[int_ty].heapIndex);
      VkMemoryPropertyFlags f = memoryProperties.memoryTypes[int_ty].propertyFlags;
      printf(" 0x%x,", f);
      if (f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        printf(" DeviceLocal");
      }
      if (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        printf(" HostVisible");
      }
      if (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
        printf(" HostCoherent");
      }
      if (f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
        printf(" HostCached");
      }
      if (f & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) {
        printf(" LazilyAllocated");
      }
      if (f & VK_MEMORY_PROPERTY_PROTECTED_BIT) {
        printf(" Protected");
      }
      if (f & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD) {
        printf(" CoherentAMD");
      }
      if (f & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD) {
        printf(" UncachedAMD");
      }
      if (f & VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV) {
        printf(" RdmaNV");
      }
      printf("\n");
    }

    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
    if (deviceFeatures.geometryShader) {
      QueueFamilyIndices qfi = findQueueFamilies(device);
      bool extensionSupported = checkDeviceExtensionSupport(device);
      return qfi.isComplete() && extensionSupported && swapChainSupport.presentModes.size() > 0;
    }
    return false;
  }

  void pickPhysicalDevice() {
    physicalDevice = VK_NULL_HANDLE;
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    printf("%u physical devices present\n", deviceCount);
    if (deviceCount < 1) {
      throw std::runtime_error("Failed to find GPUs with Vulkan support\n");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    for (const VkPhysicalDevice& d : devices) {
      if (isDeviceSuitable(d) && physicalDevice == VK_NULL_HANDLE) {
        physicalDevice = d;
      }
    }

    for (const VkPhysicalDevice& d : devices) {
      VkPhysicalDeviceProperties props{};
      vkGetPhysicalDeviceProperties(d, &props);
      printf("Device: %s ", props.deviceName);
      if (d == physicalDevice) {
        printf("  <----- Chosen");
      }
      printf("\n");
    }

    if (physicalDevice == VK_NULL_HANDLE) {
      throw std::runtime_error("Failed to find a suitable GPU\n");
    }

    VkPhysicalDeviceProperties2 prop2{};
    prop2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProps{};
    rtPipelineProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    prop2.pNext = &rtPipelineProps;
    vkGetPhysicalDeviceProperties2(physicalDevice, &prop2);
  }

  void createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
      VkDeviceQueueCreateInfo queueCreateInfo{};
      queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueCreateInfo.queueFamilyIndex = queueFamily;
      queueCreateInfo.queueCount = 1;
      queueCreateInfo.pQueuePriorities = &queuePriority;
      queueCreateInfos.push_back(queueCreateInfo);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.queueCreateInfoCount = uint32_t(queueCreateInfos.size());
    createInfo.pEnabledFeatures = nullptr;
    if (enableValidationLayers) {
      createInfo.enabledLayerCount = uint32_t(validationLayers.size());
      createInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else {
      createInfo.enabledLayerCount = 0;
    }
    createInfo.enabledExtensionCount = uint32_t(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Feature train/chain
    VkPhysicalDeviceRayTracingValidationFeaturesNV rtValidationFeatures{};
    rtValidationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV;

    VkPhysicalDeviceOpacityMicromapFeaturesEXT ommFeatures{};
    ommFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT;
    ommFeatures.pNext = &rtValidationFeatures;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
    rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rayTracingPipelineFeatures.pNext = &ommFeatures;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
    accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelerationStructureFeatures.pNext = &rayTracingPipelineFeatures;

    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{};
    descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    descriptorIndexingFeatures.pNext = &accelerationStructureFeatures;

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
    bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bufferDeviceAddressFeatures.pNext = &descriptorIndexingFeatures;

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.pNext = &bufferDeviceAddressFeatures;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures2);

    assert(bufferDeviceAddressFeatures.bufferDeviceAddress);
    //assert(accelerationStructureFeatures.accelerationStructureHostCommands);  // Does not support AS build on the host?
    assert(accelerationStructureFeatures.accelerationStructure);
    assert(rtValidationFeatures.rayTracingValidation);

    // RT and buffer device address
    createInfo.pNext = &deviceFeatures2;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create logical device");
    }

    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
  }

  void createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[3]{};

    descriptorSetLayoutBindings[0].binding = 0;  // Normals
    descriptorSetLayoutBindings[0].descriptorCount = 1;
    descriptorSetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBindings[0].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    descriptorSetLayoutBindings[1].binding = 1;  // MVP matrix
    descriptorSetLayoutBindings[1].descriptorCount = 1;
    descriptorSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorSetLayoutBindings[1].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    descriptorSetLayoutBindings[2].binding = 2;  // Normal offsets
    descriptorSetLayoutBindings[2].descriptorCount = 1;
    descriptorSetLayoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBindings[2].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = _countof(descriptorSetLayoutBindings);
    descriptorSetLayoutInfo.pBindings = descriptorSetLayoutBindings;
    if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create descriptor set layout");
    }
  }

  void readGLTF() {
    cgltf_options options{};
    cgltf_result  cgltfResult{};
    cgltf_data* rawData{};
    cgltfResult = cgltf_parse_file(&options, g_gltf_filename.c_str(), &rawData);
    if (cgltfResult != cgltf_result_success || rawData == nullptr)
    {
      printf("Oh! failed to load gltf.\n");
      exit(1);
    } else {
      printf("Successfully parsed GLTF file %s \n", g_gltf_filename.c_str());
    }

    cgltfResult = cgltf_validate(rawData);
    if (cgltfResult != cgltf_result_success)
    {
      printf("Oh! failed to validate.\n");
      exit(1);
    }
    else {
      printf("Successfully validated GLTF file %s\n", g_gltf_filename.c_str());
    }

    cgltfResult = cgltf_load_buffers(&options, rawData, g_gltf_filename.c_str());

    printf("[readGLTF]\n");
    printf("material count:     %zu\n", rawData->materials_count);
    printf("meshes count:       %zu\n", rawData->meshes_count);
    printf("nv_micromaps_count: %zu\n", rawData->nv_micromaps_count);
    printf("accessors count:    %zu\n", rawData->accessors_count);  // names are all NULL

    assert(rawData->meshes_count > 0);
    cgltf_mesh& mesh0 = rawData->meshes[0];
    printf("mesh0 has %zu prims\n", mesh0.primitives_count);
    for (uint32_t pidx = 0; pidx < mesh0.primitives_count; pidx++) {
      cgltf_primitive& prim = mesh0.primitives[pidx];
      printf("mesh0's prim[%u] has %zu indices, type=%d, component_type=%d\n",
        pidx, prim.indices->count, prim.indices->type, prim.indices->component_type);

      assert(prim.indices->component_type == cgltf_component_type_r_32u);
      cgltf_accessor* pos_accessor = nullptr;
      cgltf_accessor* norm_accessor = nullptr;
      cgltf_accessor* index_accessor = prim.indices;

      for (uint32_t aidx = 0; aidx < prim.attributes_count; aidx++)
      {
        const cgltf_attribute& attr = prim.attributes[aidx];
        switch (attr.type)
        {
        case cgltf_attribute_type_position:
          pos_accessor = attr.data;
          printf("Found position accessor.\n");
          break;
        case cgltf_attribute_type_normal:
          norm_accessor = attr.data;
          printf("Found normals accessor. %u normals\n",
            norm_accessor->count);
          break;
        }
      }

      assert(pos_accessor);

      uint32_t* idxes = (uint32_t*)((uint8_t*)index_accessor->buffer_view->buffer->data + index_accessor->buffer_view->offset);
      glm::vec3* positions = (glm::vec3*)((uint8_t*)pos_accessor->buffer_view->buffer->data + pos_accessor->buffer_view->offset);
      glm::vec3* nrms = (glm::vec3*)((uint8_t*)norm_accessor->buffer_view->buffer->data + norm_accessor->buffer_view->offset);

      const uint32_t NT = prim.indices->count;
      for (uint32_t iidx = 0; iidx < NT; iidx += 3)
      {
        uint32_t i0 = idxes[iidx], i1 = idxes[iidx + 1], i2 = idxes[iidx + 2];
        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);

        glm::vec3 p0 = positions[i0], p1 = positions[i1], p2 = positions[i2];
        glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        normals.push_back(n);
      }

      for (uint32_t vidx = 0; vidx < pos_accessor->count; vidx++) {
        vertices.push_back(positions[vidx]);
      }

    }

    // 34817, 208890
    printf("%zu verts and %zu indices loaded.\n", vertices.size(), indices.size());
    g_vertex_count = vertices.size();
    g_index_count = indices.size();
  }

  struct VertexAndIndex {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    uint32_t vert_offset;
    uint32_t index_offset;
  };

  bool readOBJ(const std::string& fn, VertexAndIndex& out) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, fn.c_str(), ".");
    if (!err.empty()) {
      printf("TinyObj error: %s\n", err.c_str());
    }
    if (!ret) {
      return false;
    }
    printf("File %s has %zu verts and %zu shapes\n",
      fn.c_str(), attrib.vertices.size(), shapes.size());
    for (unsigned i = 0; i < attrib.vertices.size(); i += 3) {
      glm::vec3 v{};
      v.x = attrib.vertices.at(i);
      v.y = attrib.vertices.at(i + 1);
      v.z = attrib.vertices.at(i + 2);
      out.vertices.push_back(v);
      g_cluster_vert_count++;
    }
    for (unsigned i = 0; i < shapes.size(); i++) {
      for (uint32_t fidx = 0; fidx < shapes[i].mesh.indices.size() / 3; fidx++) {
        tinyobj::index_t i0 = shapes[i].mesh.indices[fidx * 3];
        tinyobj::index_t i1 = shapes[i].mesh.indices[fidx * 3 + 1];
        tinyobj::index_t i2 = shapes[i].mesh.indices[fidx * 3 + 2];
        out.indices.push_back(i0.vertex_index);
        out.indices.push_back(i1.vertex_index);
        out.indices.push_back(i2.vertex_index);
        g_cluster_tri_count++;

        // Calculate normals (per face), accumulated, process in readClusters()
        glm::vec3 p0 = out.vertices.at(i0.vertex_index);
        glm::vec3 p1 = out.vertices.at(i1.vertex_index);
        glm::vec3 p2 = out.vertices.at(i2.vertex_index);
        glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        cluster_normals.push_back(n);
      }
    }
    out.vert_offset = g_cluster_vert_count;
    out.index_offset = g_cluster_tri_count * 3;
    return true;
  }

  void readClusters() {
    std::filesystem::path directoryPath = "cluster_dump";
    uint32_t count = 0;
#ifdef NDEBUG
    const int READ_LIMIT = 10000;
#else
    const int READ_LIMIT = 100;
#endif
    if (std::filesystem::exists(directoryPath) &&
      std::filesystem::is_directory(directoryPath)) {
      for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
        std::cout << entry.path() << std::endl;
        VertexAndIndex vi;
        readOBJ(entry.path().string(), vi);
        vertex_and_indices.push_back(vi);
        count++;
        g_cluster_count++;
        if (count >= READ_LIMIT) break;
      }
    }
    else {
      std::cerr << "Error: Directory does not exist or is not a directory." << std::endl;
    }
    printf("%u cluster files found.\n", count);

    // Allocate memory
    VkBufferCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.size = sizeof(glm::vec3) * 64;
    createInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer tempVertexBuffer;
    if (vkCreateBuffer(device, &createInfo, nullptr, &tempVertexBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not create vertex buffer");
    }

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(device, tempVertexBuffer, &memReq);
    uint32_t vb_alignment = memReq.alignment;
    printf("Alignment: %u\n", vb_alignment);
    vkDestroyBuffer(device, tempVertexBuffer, nullptr);

    size_t total_size = 0;
    for (uint32_t i = 0; i < vertex_and_indices.size(); i++) {
      const VertexAndIndex& vi = vertex_and_indices[i];
      total_size += vi.vertices.size() * sizeof(glm::vec3);
      total_size = AlignUp(total_size, vb_alignment);
      total_size += vi.indices.size() * sizeof(uint32_t);
      total_size = AlignUp(total_size, vb_alignment);
    }
    printf("VB + IB buffer size: %u\n", total_size);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = total_size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocInfo.pNext = &allocFlagsInfo;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &clusterVertexAndIndexMemory) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate memory for cluster vertex and index");
    }

    size_t offset = 0;
    for (uint32_t i = 0; i < vertex_and_indices.size(); i++) {
      const VertexAndIndex& vi = vertex_and_indices[i];
      createInfo.size = sizeof(glm::vec3) * vi.vertices.size();
      createInfo.usage &= (~VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
      createInfo.usage |= (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
      VkBuffer clusterVertexBuffer;
      if (vkCreateBuffer(device, &createInfo, nullptr, &clusterVertexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Could not create vertex buffer");
      }
      vkBindBufferMemory(device, clusterVertexBuffer, clusterVertexAndIndexMemory, offset);
      clusterVertexBuffers.push_back(clusterVertexBuffer);

      offset += vi.vertices.size() * sizeof(glm::vec3);
      offset = AlignUp(offset, vb_alignment);

      VkBuffer clusterIndexBuffer;
      createInfo.size = sizeof(uint32_t) * vi.indices.size();
      createInfo.usage &= (~VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
      createInfo.usage |= (VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
      if (vkCreateBuffer(device, &createInfo, nullptr, &clusterIndexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Could not create index buffer");
      }
      vkBindBufferMemory(device, clusterIndexBuffer, clusterVertexAndIndexMemory, offset);
      clusterIndexBuffers.push_back(clusterIndexBuffer);

      offset += vi.indices.size() * sizeof(uint32_t);
      offset = AlignUp(offset, vb_alignment);
    }

    // Not using indirect
    offset = 0;
    void* data;
    vkMapMemory(device, clusterVertexAndIndexMemory, 0, createInfo.size, 0, &data);
    for (uint32_t i = 0; i < vertex_and_indices.size(); i++) {
      const VertexAndIndex& vi = vertex_and_indices[i];
      memcpy(((uint8_t*)data) + offset, vi.vertices.data(), sizeof(glm::vec3) * vi.vertices.size());
      offset += vi.vertices.size() * sizeof(glm::vec3);
      offset = AlignUp(offset, vb_alignment);
      memcpy(((uint8_t*)data) + offset, vi.indices.data(), sizeof(uint32_t)* vi.indices.size());
      offset += vi.indices.size() * sizeof(uint32_t);
      offset = AlignUp(offset, vb_alignment);
    }
    vkUnmapMemory(device, clusterVertexAndIndexMemory);

    // Using indirect
    std::vector<uint32_t> vert_idx_offsets;
    offset = 0;
    uint32_t tot_vertex_count = 0, tot_index_count = 0;
    uint32_t tot_index_offset = 0;
    for (uint32_t i = 0; i < vertex_and_indices.size(); i++) {
      const VertexAndIndex& vi = vertex_and_indices[i];
      offset += sizeof(glm::vec3) * vi.vertices.size();
      vert_idx_offsets.push_back(tot_vertex_count);
      tot_vertex_count += vi.vertices.size();
    }
    offset = AlignUp(offset, vb_alignment);
    tot_index_offset = offset;
    for (uint32_t i = 0; i < vertex_and_indices.size(); i++) {
      const VertexAndIndex& vi = vertex_and_indices[i];
      offset += sizeof(uint32_t) * vi.indices.size();
      tot_index_count += vi.indices.size();
    }
    offset = AlignUp(offset, vb_alignment);
    allocInfo.allocationSize = offset;
    printf("VB + IB Buffer for indirect size: %u\n", offset);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &clusterVertexAndIndexMemoryForIndirect) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate memory for cluster vertex and index for indirect");
    }
    createInfo.size = sizeof(glm::vec3) * tot_vertex_count;
    createInfo.usage &= (~VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    createInfo.usage |= (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    if (vkCreateBuffer(device, &createInfo, nullptr, &clusterVertexBufferForIndirect) != VK_SUCCESS) {
      throw std::runtime_error("Could not create vertex buffer for indirect");
    }
    vkBindBufferMemory(device, clusterVertexBufferForIndirect, clusterVertexAndIndexMemoryForIndirect, 0);
    createInfo.size = sizeof(uint32_t) * tot_index_count;
    createInfo.usage &= (~VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    createInfo.usage |= (VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (vkCreateBuffer(device, &createInfo, nullptr, &clusterIndexBufferForIndirect) != VK_SUCCESS) {
      throw std::runtime_error("Could not create index buffer for indirect");
    }
    vkBindBufferMemory(device, clusterIndexBufferForIndirect, clusterVertexAndIndexMemoryForIndirect, tot_index_offset);
    vkMapMemory(device, clusterVertexAndIndexMemoryForIndirect, 0, allocInfo.allocationSize, 0, &data);
    offset = 0;
    for (uint32_t i = 0; i < vertex_and_indices.size(); i++) {
      const VertexAndIndex& vi = vertex_and_indices[i];
      memcpy(((uint8_t*)data) + offset, vi.vertices.data(), sizeof(glm::vec3)* vi.vertices.size());
      offset += sizeof(glm::vec3) * vi.vertices.size();
    }
    offset = tot_index_offset;
    for (uint32_t i = 0; i < vertex_and_indices.size(); i++) {
      const VertexAndIndex& vi = vertex_and_indices[i];
      memcpy(((uint8_t*)data) + offset, vi.indices.data(), sizeof(uint32_t) * vi.indices.size());
      offset += sizeof(uint32_t) * vi.indices.size();
    }
    vkUnmapMemory(device, clusterVertexAndIndexMemoryForIndirect);

    // Indirect Cmds and normal offsets
    std::vector<uint32_t> normal_offsets;
    uint32_t tri_count = 0;
    uint32_t index_offset = 0;
    uint32_t vertex_offset = 0;
    for (uint32_t i = 0; i < vertex_and_indices.size(); i++) {
      const VertexAndIndex& vi = vertex_and_indices[i];
      MyIndirectClusterDraw draw{};
      draw.command.firstIndex = index_offset;
      draw.command.firstInstance = i;  // Use as cluster ID
      draw.command.indexCount = vi.indices.size();
      draw.command.instanceCount = 1;
      draw.command.vertexOffset = vertex_offset;  // Vertex idx is local to cluster so we need set it here
      normal_offsets.push_back(tri_count);
      tri_count += vi.indices.size() / 3;
      index_offset += vi.indices.size();
      vertex_offset += vi.vertices.size();
      clusterIndirectCommands.push_back(draw);
    }
    size_t indirectCmdsSize = sizeof(MyIndirectClusterDraw) * clusterIndirectCommands.size();
    createBuffer(
      indirectCmdsSize,
      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      clusterIndirectCommandBuffer, clusterIndirectCommandMemory);
    vkMapMemory(device, clusterIndirectCommandMemory, 0, indirectCmdsSize, 0, &data);
    memcpy(data, clusterIndirectCommands.data(), indirectCmdsSize);
    vkUnmapMemory(device, clusterIndirectCommandMemory);

    // Indirect normals
    size_t normalsSize = sizeof(glm::vec3) * cluster_normals.size();
    createBuffer(
      normalsSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      clusterNormalsBufferForIndirect, clusterNormalsMemoryForIndirect);
    vkMapMemory(device, clusterNormalsMemoryForIndirect, 0, normalsSize, 0, &data);
    memcpy(data, cluster_normals.data(), normalsSize);
    vkUnmapMemory(device, clusterNormalsMemoryForIndirect);

    // Indirect normal offsets, should be index offsets divided by 3 ?
    size_t normalOffsetsSize = sizeof(uint32_t) * vertex_and_indices.size();
    createBuffer(
      normalOffsetsSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      clusterNormalsOffsetBufferForIndirect, clusterNormalsOffsetMemoryForIndirect);
    vkMapMemory(device, clusterNormalsOffsetMemoryForIndirect, 0, normalOffsetsSize, 0, &data);
    memcpy(data, normal_offsets.data(), normalOffsetsSize);
    vkUnmapMemory(device, clusterNormalsOffsetMemoryForIndirect);

    // Indirect (and RT) vertex offsets
    size_t vertOffsetsSize = sizeof(uint32_t) * vertex_and_indices.size();
    createBuffer(
      vertOffsetsSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      clusterVertexIdxOffsetBuffer, clusterVertexIdxOffsetMemory);
    vkMapMemory(device, clusterVertexIdxOffsetMemory, 0, vertOffsetsSize, 0, &data);
    memcpy(data, vert_idx_offsets.data(), vertOffsetsSize);
    vkUnmapMemory(device, clusterVertexIdxOffsetMemory);
  }

  void createSwapChain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 &&
      imageCount > swapChainSupport.capabilities.maxImageCount) {
      imageCount = swapChainSupport.capabilities.maxImageCount;
    }
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
      | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };
    if (indices.graphicsFamily != indices.presentFamily) {
      createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      createInfo.queueFamilyIndexCount = 2;
      createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else {
      createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      createInfo.queueFamilyIndexCount = 0;
      createInfo.pQueueFamilyIndices = nullptr;
    }
    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create swap chain");
    }
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

    swapChainImageFormat = createInfo.imageFormat;
    swapChainExtent = extent;
    printf("Create swapchain with %u images, format 0x%x, extent %ux%u\n",
      imageCount, swapChainImageFormat, swapChainExtent.width, swapChainExtent.height);
  }

  void createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());
    for (uint32_t i = 0; i < swapChainImages.size(); i++) {
      VkImageViewCreateInfo createInfo{};
      createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      createInfo.image = swapChainImages[i];
      createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      createInfo.format = swapChainImageFormat;
      createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      createInfo.subresourceRange.baseMipLevel = 0;
      createInfo.subresourceRange.levelCount = 1;
      createInfo.subresourceRange.baseArrayLayer = 0;
      createInfo.subresourceRange.layerCount = 1;
      if (vkCreateImageView(device, &createInfo, nullptr, &(swapChainImageViews[i])) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view");
      }
    }
  }

  void createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };

    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = _countof(attachments);
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create render pass");
    }
  }

  void createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);
    VkCommandPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &createInfo, nullptr, &commandPool) != VK_SUCCESS) {
      throw std::runtime_error("Could not create command pool");
    }
  }

  void createCommandBuffer() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not allocate command buffer");
    }
  }

  VkShaderModule createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = (uint32_t*)(code.data());
    VkShaderModule ret;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &ret) != VK_SUCCESS) {
      throw std::runtime_error("Could not create shader module");
    }
    return ret;
  }

  void createGraphicsPipeline() {
    std::vector<char> vertShaderCode = readFile("shaders/vert.spv");
    std::vector<char> fragShaderCode = readFile("shaders/frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
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
    viewport.width = float(swapChainExtent.width);
    viewport.height = float(swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapChainExtent;

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
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
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
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    pipelineInfo.pDepthStencilState = &depthStencil;
    

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create graphics pipeline");
    }

    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
  }

  void createFramebuffers() {
    swapChainFramebuffers.resize(swapChainImages.size());
    for (uint32_t i = 0; i < swapChainImages.size(); i++) {
      VkImageView attachments[] = {
        swapChainImageViews[i],
        depthImageView
      };

      VkFramebufferCreateInfo framebufferInfo{};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebufferInfo.renderPass = renderPass;
      framebufferInfo.attachmentCount = _countof(attachments);
      framebufferInfo.pAttachments = attachments;
      framebufferInfo.width = swapChainExtent.width;
      framebufferInfo.height = swapChainExtent.height;
      framebufferInfo.layers = 1;

      if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create framebuffer");
      }
    }
  }

  void createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create semaphore");
    }

    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create semaphore");
    }

    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &computeFinishedSemaphore) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create computeFinishedSemaphore");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create fence");
    }

    if (vkCreateFence(device, &fenceInfo, nullptr, &computeInFlightFence) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create computeInFlightFence");
    }
    vkResetFences(device, 1, &computeInFlightFence);
  }

  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
      if (typeFilter & (1 << i)) {
        if ((memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
          return i;
        }
      }
    }
    throw std::runtime_error("Could not find suitable memory type");
  }

  void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
      throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
      allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
      allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
      allocInfo.pNext = &allocFlagsInfo;
    }

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
  }

  void createUniformBuffer() {
    size_t sz = sizeof(PerSceneUniformBuffer);
    createBuffer(sz,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      perSceneUniformBuffer,
      perSceneUniformBufferMemory);
  }

  void createVertexIndexNormalBuffer() {
    size_t sz = sizeof(glm::vec3) * vertices.size();
    createBuffer(sz,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      vertexBuffer,
      vertexBufferMemory);

    createBuffer(sz,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      vertexBufferDisplaced,
      vertexBufferDisplacedMemory);

    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, sz, 0, &data);
    memcpy(data, vertices.data(), sz);
    vkUnmapMemory(device, vertexBufferMemory);
  
    sz = sizeof(uint32_t) * indices.size();
    createBuffer(sz,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      indexBuffer,
      indexBufferMemory);

    vkMapMemory(device, indexBufferMemory, 0, sz, 0, &data);
    memcpy(data, indices.data(), sz);
    vkUnmapMemory(device, indexBufferMemory);

    sz = sizeof(glm::vec3) * normals.size();
    createBuffer(sz,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      normalBuffer,
      normalBufferMemory);

    vkMapMemory(device, normalBufferMemory, 0, sz, 0, &data);
    memcpy(data, normals.data(), sz);
    vkUnmapMemory(device, normalBufferMemory);
  }

  void createDescriptorPool() {
    VkDescriptorPoolSize poolSizes[1]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 3;
    VkDescriptorPoolCreateInfo poolInfo{};

    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = _countof(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create descriptor pool");
    }
  }

  void createDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    std::vector<VkDescriptorSetLayout> layouts(1, descriptorSetLayout);
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate descriptor sets");
    }

    // Populate initial data
    VkDescriptorBufferInfo dbi[2]{};
    dbi[0].buffer = normalBuffer;
    dbi[0].offset = 0;
    dbi[0].range = sizeof(glm::vec3) * normals.size();

    dbi[1].buffer = perSceneUniformBuffer;
    dbi[1].offset = 0;
    dbi[1].range = sizeof(PerSceneUniformBuffer);

    VkWriteDescriptorSet writeDesc[2]{};
    writeDesc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDesc[0].dstSet = descriptorSets[0];
    writeDesc[0].dstBinding = 0;
    writeDesc[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeDesc[0].descriptorCount = 1;
    writeDesc[0].pBufferInfo = &(dbi[0]);
    writeDesc[1] = writeDesc[0];
    writeDesc[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writeDesc[1].dstBinding = 1;
    writeDesc[1].pBufferInfo = &(dbi[1]);

    vkUpdateDescriptorSets(device, _countof(writeDesc), writeDesc, 0, nullptr);
  }

  VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
      VkFormatProperties props;
      vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

      if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
        return format;
      }
      else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
        return format;
      }
    }
    throw std::runtime_error("Could not find supported format");
  }

  VkFormat findDepthFormat() {
    return findSupportedFormat(
      { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
      VK_IMAGE_TILING_OPTIMAL,
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
  }

  void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
      throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(device, image, imageMemory, 0);
  }

  VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
      throw std::runtime_error("failed to create texture image view!");
    }

    return imageView;
  }

  VkCommandBuffer beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
  }

  void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
  }

  void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;

    VkPipelineStageFlags sourceStage{}, destinationStage{};

    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = 0;

      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    else {
      throw std::invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(commandBuffer,
      sourceStage, destinationStage,
      0, 0, nullptr, 0, nullptr, 1, &barrier);
    endSingleTimeCommands(commandBuffer);
  }

  void createDepthResources() {
    VkFormat depthFormat = findDepthFormat();
    createImage(swapChainExtent.width, swapChainExtent.height, depthFormat,
      VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
    depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    transitionImageLayout(depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  }
  
  void createRtOutputImages() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      createImage(swapChainExtent.width, swapChainExtent.height,
        VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, rtOutputImages[i],
        rtOutputImageMemories[i]);
      rtOutputImageViews[i] = createImageView(rtOutputImages[i],
        VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
      transitionImageLayout(rtOutputImages[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);
    }
  }

  void createRtDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[7]{};
    descriptorSetLayoutBindings[0].binding = 1;
    descriptorSetLayoutBindings[0].descriptorCount = 1;
    descriptorSetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorSetLayoutBindings[0].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    descriptorSetLayoutBindings[1].binding = 0;
    descriptorSetLayoutBindings[1].descriptorCount = 1;
    descriptorSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    descriptorSetLayoutBindings[1].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    descriptorSetLayoutBindings[2].binding = 2;
    descriptorSetLayoutBindings[2].descriptorCount = 1;
    descriptorSetLayoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorSetLayoutBindings[2].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[2].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    descriptorSetLayoutBindings[3].binding = 3;  // Vertex buffer
    descriptorSetLayoutBindings[3].descriptorCount = 1;
    descriptorSetLayoutBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBindings[3].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[3].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;;

    descriptorSetLayoutBindings[4].binding = 4;  // Index buffer
    descriptorSetLayoutBindings[4].descriptorCount = 1;
    descriptorSetLayoutBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBindings[4].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[4].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;;

    descriptorSetLayoutBindings[5].binding = 5;  // Norm ofst
    descriptorSetLayoutBindings[5].descriptorCount = 1;
    descriptorSetLayoutBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBindings[5].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[5].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    descriptorSetLayoutBindings[6].binding = 6;  // Vert ofst
    descriptorSetLayoutBindings[6].descriptorCount = 1;
    descriptorSetLayoutBindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBindings[6].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[6].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
    descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.bindingCount = _countof(descriptorSetLayoutBindings);
    descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBindings;
    if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &rtDescriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create RT descriptor set layout");
    }
  }

  void createRtDescriptorPool() {
    VkDescriptorPoolSize poolSizes[1]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 7;
    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.poolSizeCount = _countof(poolSizes);
    poolCreateInfo.pPoolSizes = poolSizes;
    poolCreateInfo.maxSets = 1;
    poolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if (vkCreateDescriptorPool(device, &poolCreateInfo, nullptr, &rtDescriptorPool) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create RT descriptor pool");
    }
  }

  void createRtDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = rtDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &rtDescriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &rtDescriptorSet) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate rt descriptor set");
    }
  }

  void createRtPipeline() {
    VkPipelineLayoutCreateInfo rtPipelineLayoutCreateInfo{};
    rtPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    rtPipelineLayoutCreateInfo.setLayoutCount = 1;
    rtPipelineLayoutCreateInfo.pSetLayouts = &rtDescriptorSetLayout;
    if (vkCreatePipelineLayout(device, &rtPipelineLayoutCreateInfo, nullptr, &rtPipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("Could not create rt pipeline layout");
    }

    VkPipelineShaderStageCreateInfo stages[3]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].pName = "main";  // entry point name
    VkShaderModule raygenShaderModule = createShaderModule(readFile("shaders/rgen.spv"));
    stages[0].module = raygenShaderModule;
    stages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].pName = "main";
    VkShaderModule missShaderModule = createShaderModule(readFile("shaders/rmiss.spv"));
    stages[1].module = missShaderModule;
    stages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;

    stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[2].pName = "main";
    VkShaderModule chitShaderModule = createShaderModule(readFile("shaders/rchit.spv"));
    stages[2].module = chitShaderModule;
    stages[2].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    
    VkRayTracingShaderGroupCreateInfoKHR shaderGroupInfos[3]{};
    shaderGroupInfos[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroupInfos[0].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[0].closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[0].generalShader = 0;  // RGen
    shaderGroupInfos[0].intersectionShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;

    shaderGroupInfos[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroupInfos[1].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[1].closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[1].generalShader = 1;  // Miss
    shaderGroupInfos[1].intersectionShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;

    shaderGroupInfos[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroupInfos[2].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[2].closestHitShader = 2;
    shaderGroupInfos[2].generalShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[2].intersectionShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    
    VkRayTracingPipelineCreateInfoKHR rtPipelineCreateInfo{};
    rtPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rtPipelineCreateInfo.flags = 0;
    rtPipelineCreateInfo.stageCount = _countof(stages);
    rtPipelineCreateInfo.pStages = stages;
    rtPipelineCreateInfo.groupCount = _countof(shaderGroupInfos);
    rtPipelineCreateInfo.pGroups = shaderGroupInfos;
    rtPipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
    rtPipelineCreateInfo.layout = rtPipelineLayout;

    // NEW for clusters! we need to enable their usage explicitly for a ray tracing pipeline
    VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV pipeClusters = {
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CLUSTER_ACCELERATION_STRUCTURE_CREATE_INFO_NV };
    pipeClusters.allowClusterAccelerationStructure = true;
    //pipeClusters.allowClusterAccelerationStructures = true;

    // chain extension it into next of the pipeline create info
    rtPipelineCreateInfo.pNext = &pipeClusters;

    PFN_vkCreateRayTracingPipelinesKHR funcCreateRayTracingPipelines =
      (PFN_vkCreateRayTracingPipelinesKHR)vkGetInstanceProcAddr(
        instance, "vkCreateRayTracingPipelinesKHR");
    assert(funcCreateRayTracingPipelines);
    if (funcCreateRayTracingPipelines(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rtPipelineCreateInfo, nullptr, &rtPipeline) != VK_SUCCESS) {
      throw std::runtime_error("Could not create RT pipeline");
    }

    vkDestroyShaderModule(device, raygenShaderModule, nullptr);
    vkDestroyShaderModule(device, missShaderModule, nullptr);
  }

  void createRtSBT() {
    const size_t sbtSize = 32;
    const size_t sbtAlignment = 64;
    const size_t numSBTs = 3;  // raygen, miss, closest-hit

    createBuffer(
      sbtAlignment * numSBTs,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
      | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      sbtBuffer, sbtMemory);

    VkBufferDeviceAddressInfo sbtDevAddrInfo{};
    sbtDevAddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    sbtDevAddrInfo.buffer = sbtBuffer;
    VkDeviceAddress sbtDeviceAddress = vkGetBufferDeviceAddress(device, &sbtDevAddrInfo);

    char shaderGroupHandle[sbtSize * numSBTs]{};
    PFN_vkGetRayTracingShaderGroupHandlesKHR funcGetRayTracingShaderGroupHandlesKHR =
      (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetInstanceProcAddr(
        instance, "vkGetRayTracingShaderGroupHandlesKHR");
    if (funcGetRayTracingShaderGroupHandlesKHR(device, rtPipeline, 0, numSBTs, sbtSize * numSBTs, shaderGroupHandle) != VK_SUCCESS) {
      throw std::runtime_error("Could not get RT shader group handles");
    }

    uint8_t* mapped{};
    vkMapMemory(device, sbtMemory, 0, sbtSize * numSBTs, 0, (void**)&mapped);
    memcpy(mapped, shaderGroupHandle, sbtSize);  // rgen
    memcpy(mapped + sbtAlignment, shaderGroupHandle + sbtSize, sbtSize);  // miss
    memcpy(mapped + sbtAlignment * 2, shaderGroupHandle + sbtSize * 2, sbtSize);  // c-hit
    vkUnmapMemory(device, sbtMemory);

    rtRGenRegion.deviceAddress = sbtDeviceAddress;
    rtRGenRegion.size = sbtSize;
    rtRGenRegion.stride = sbtSize;

    rtMissRegion.deviceAddress = sbtDeviceAddress + sbtAlignment;
    rtMissRegion.size = sbtSize;
    rtMissRegion.stride = sbtSize;
    
    rtHitRegion.deviceAddress = sbtDeviceAddress + sbtAlignment * 2;
    rtHitRegion.size = sbtSize;
    rtHitRegion.stride = sbtSize;
  }

  void createOrUpdateAS(bool update) {
    // Build Blas
    VkAccelerationStructureGeometryTrianglesDataKHR triASData{};
    triASData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triASData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    if (update) {
      triASData.vertexData.deviceAddress = getBufferDeviceAddress(vertexBufferDisplaced);
    }
    else {
      triASData.vertexData.deviceAddress = getBufferDeviceAddress(vertexBuffer);
    }
    triASData.vertexStride = sizeof(glm::vec3);
    triASData.indexType = VK_INDEX_TYPE_UINT32;
    triASData.indexData.deviceAddress = getBufferDeviceAddress(indexBuffer);
    triASData.maxVertex = g_vertex_count - 1;

    VkAccelerationStructureGeometryKHR geomData{};
    geomData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geomData.flags = 0;
    geomData.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geomData.geometry.triangles = triASData;

    uint32_t primCount = g_index_count / 3;

    VkAccelerationStructureBuildGeometryInfoKHR buildGeomInfo{};
    buildGeomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildGeomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (update) {
      buildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
      buildGeomInfo.srcAccelerationStructure = blas;
      buildGeomInfo.dstAccelerationStructure = blas;
    }
    else {
      buildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      buildGeomInfo.srcAccelerationStructure = VK_NULL_HANDLE;
      buildGeomInfo.dstAccelerationStructure = VK_NULL_HANDLE;
    }
    buildGeomInfo.geometryCount = 1;
    buildGeomInfo.pGeometries = &geomData;
    buildGeomInfo.ppGeometries = nullptr;
    buildGeomInfo.scratchData.deviceAddress = 0;

    VkAccelerationStructureBuildSizesInfoKHR asBuildSizeInfo{};
    asBuildSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR funcGetAccelerationStructureBuildSizes =
      (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetInstanceProcAddr(
        instance, "vkGetAccelerationStructureBuildSizesKHR");
    assert(funcGetAccelerationStructureBuildSizes);
    funcGetAccelerationStructureBuildSizes(device,
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &buildGeomInfo,
      &primCount,
      &asBuildSizeInfo);
    if (!update) {
      printf("BLAS build size: scratch=%u, AS=%u\n",
        asBuildSizeInfo.buildScratchSize,
        asBuildSizeInfo.accelerationStructureSize);
    }

    VkBuffer blasScratchBuffer{};
    VkDeviceMemory blasScratchMemory{};

    if (!update) {
      createBuffer(asBuildSizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        0,
        blasScratchBuffer, blasScratchMemory);

      createBuffer(asBuildSizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        0,
        blasResultBuffer, blasResultMemory);
    }
    else {
      createBuffer(asBuildSizeInfo.updateScratchSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        0,
        blasScratchBuffer, blasScratchMemory);
    }

    buildGeomInfo.scratchData.deviceAddress = getBufferDeviceAddress(blasScratchBuffer);

    VkAccelerationStructureCreateInfoKHR asCreateInfo{};
    asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCreateInfo.createFlags = 0;
    asCreateInfo.buffer = blasResultBuffer;
    asCreateInfo.offset = 0;
    asCreateInfo.size = asBuildSizeInfo.accelerationStructureSize;
    asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    PFN_vkCreateAccelerationStructureKHR funcCreateAccelerationStructure =
      (PFN_vkCreateAccelerationStructureKHR)vkGetInstanceProcAddr(
        instance, "vkCreateAccelerationStructureKHR");

    if (!update) {
      if (funcCreateAccelerationStructure(device, &asCreateInfo, nullptr, &blas) != VK_SUCCESS) {
        throw std::runtime_error("Could not create BLAS");
      }
      buildGeomInfo.dstAccelerationStructure = blas;
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.primitiveCount = g_index_count / 3;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.transformOffset = 0;

    VkAccelerationStructureBuildRangeInfoKHR* const buildRangeInfos[] = { &buildRangeInfo };
    PFN_vkCmdBuildAccelerationStructuresKHR funcCmdBuildAccelerationStructuresKHR =
      (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetInstanceProcAddr(
        instance, "vkCmdBuildAccelerationStructuresKHR");
    funcCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildGeomInfo, buildRangeInfos);
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      0, 1, &barrier,
      0, nullptr,
      0, nullptr);
    endSingleTimeCommands(commandBuffer);

    vkDestroyBuffer(device, blasScratchBuffer, nullptr);
    vkFreeMemory(device, blasScratchMemory, nullptr);

    uint32_t instCount = 1;

    // Build Tlas
    // VkAccelerationStructureBuildSizesInfoKHR asBuildSizeInfo{};
    asBuildSizeInfo = {};
    asBuildSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    // Instance data
    VkAccelerationStructureInstanceKHR instance{};
    instance.transform.matrix[0][0] = 1.0f;
    instance.transform.matrix[1][1] = 1.0f;
    instance.transform.matrix[2][2] = 1.0f;
    instance.mask = 0xFF;
    instance.accelerationStructureReference = getBufferDeviceAddress(blasResultBuffer);

    size_t instanceDataSize = sizeof(VkAccelerationStructureInstanceKHR) * 1;

    uint8_t* data;
    if (!update) {
      createBuffer(instanceDataSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        tlasInstancesBuffer, tlasInstancesMemory);

      vkMapMemory(device, tlasInstancesMemory, 0, instanceDataSize, 0, (void**)&data);
      memcpy(data, &instance, instanceDataSize);
      vkUnmapMemory(device, tlasInstancesMemory);
    }

    // VkAccelerationStructureGeometryKHR geomData{};
    geomData = {};
    geomData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geomData.flags = 0;
    geomData.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geomData.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geomData.geometry.instances.data.deviceAddress = getBufferDeviceAddress(tlasInstancesBuffer);

    // VkAccelerationStructureBuildGeometryInfoKHR buildGeomInfo{};
    buildGeomInfo = {};
    buildGeomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildGeomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    if (update) {
      buildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
      buildGeomInfo.srcAccelerationStructure = tlas;
      buildGeomInfo.dstAccelerationStructure = tlas;
    }
    else {
      buildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      buildGeomInfo.srcAccelerationStructure = VK_NULL_HANDLE;
      buildGeomInfo.dstAccelerationStructure = VK_NULL_HANDLE;
    }
    buildGeomInfo.geometryCount = instCount;
    buildGeomInfo.pGeometries = &geomData;
    buildGeomInfo.ppGeometries = nullptr;

    funcGetAccelerationStructureBuildSizes(device,
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &buildGeomInfo,
      &instCount,
      &asBuildSizeInfo);

    if (!update) {
      printf("TLAS build size: scratch=%u, AS=%u\n",
        asBuildSizeInfo.buildScratchSize,
        asBuildSizeInfo.accelerationStructureSize);
    }

    VkBuffer tlasScratchBuffer{};
    VkDeviceMemory tlasScratchMemory{};

    if (!update) {
      createBuffer(asBuildSizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        0,
        tlasScratchBuffer, tlasScratchMemory);

      createBuffer(asBuildSizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        0,
        tlasResultBuffer, tlasResultMemory);
    }
    else {
      createBuffer(asBuildSizeInfo.updateScratchSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        0,
        tlasScratchBuffer, tlasScratchMemory);
    }

    buildGeomInfo.scratchData.deviceAddress = getBufferDeviceAddress(tlasScratchBuffer);

    // VkAccelerationStructureCreateInfoKHR asCreateInfo{};
    asCreateInfo = {};
    asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCreateInfo.createFlags = 0;
    asCreateInfo.buffer = tlasResultBuffer;
    asCreateInfo.offset = 0;
    asCreateInfo.size = asBuildSizeInfo.accelerationStructureSize;
    asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (!update) {
      if (funcCreateAccelerationStructure(device, &asCreateInfo, nullptr, &tlas) != VK_SUCCESS) {
        throw std::runtime_error("Could not create TLAS");
      }
      buildGeomInfo.dstAccelerationStructure = tlas;
    }

    //VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    commandBuffer = beginSingleTimeCommands();

    //VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.primitiveCount = instCount;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.transformOffset = 0;

    //VkAccelerationStructureBuildRangeInfoKHR* const buildRangeInfos[] = { &buildRangeInfo };
    funcCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildGeomInfo, buildRangeInfos);
    //VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      0, 1, &barrier,
      0, nullptr,
      0, nullptr);
    endSingleTimeCommands(commandBuffer);

    vkDestroyBuffer(device, tlasScratchBuffer, nullptr);
    vkFreeMemory(device, tlasScratchMemory, nullptr);
  }

  void createAS() {
    createOrUpdateAS(false);

    // Update to RT's descriptor set
    VkWriteDescriptorSetAccelerationStructureKHR writeDescAS{};
    writeDescAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    writeDescAS.accelerationStructureCount = 1;
    writeDescAS.pAccelerationStructures = &tlas;

    VkWriteDescriptorSet writeDesc[2]{};
    writeDesc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDesc[0].dstSet = rtDescriptorSet;
    writeDesc[0].dstBinding = 0;
    writeDesc[0].descriptorCount = 1;
    writeDesc[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writeDesc[0].pNext = &writeDescAS;
    writeDesc[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDesc[1].dstSet = rtDescriptorSet;
    writeDesc[1].dstBinding = 2;
    writeDesc[1].descriptorCount = 1;
    writeDesc[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkDescriptorBufferInfo bi{};
    bi.buffer = perSceneRtUniformBuffer;
    bi.offset = 0;
    bi.range = sizeof(RtPerSceneUniformBuffer);
    writeDesc[1].pBufferInfo = &bi;

    vkUpdateDescriptorSets(device, _countof(writeDesc), writeDesc, 0, nullptr);
  }

  void createClusterAS() {
    VkClusterAccelerationStructureInputInfoNV inputs{};
    inputs.sType = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV;
    inputs.maxAccelerationStructureCount = g_cluster_count;
    inputs.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
    inputs.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;

    VkClusterAccelerationStructureTriangleClusterInputNV clusterTriangleInput{};
    clusterTriangleInput.sType = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV;
    clusterTriangleInput.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    clusterTriangleInput.maxClusterUniqueGeometryCount = 0;
    clusterTriangleInput.maxClusterTriangleCount = 64;
    clusterTriangleInput.maxClusterVertexCount = 64;
    clusterTriangleInput.maxTotalTriangleCount = g_cluster_tri_count;
    clusterTriangleInput.maxTotalVertexCount = g_cluster_vert_count;
    clusterTriangleInput.minPositionTruncateBitCount = 0;
    
    inputs.opInput.pTriangleClusters = &clusterTriangleInput;
    inputs.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

    VkAccelerationStructureBuildSizesInfoKHR sizesInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    PFN_vkGetClusterAccelerationStructureBuildSizesNV
      funcGetClusterAccelerationStructureBuildSizesNV =
      (PFN_vkGetClusterAccelerationStructureBuildSizesNV)vkGetInstanceProcAddr(
        instance, "vkGetClusterAccelerationStructureBuildSizesNV");
    funcGetClusterAccelerationStructureBuildSizesNV(device, &inputs, &sizesInfo);
    size_t scratchSize = sizesInfo.buildScratchSize;

    VkBuffer scratchBuffer;
    VkDeviceMemory scratchMemory;
    createBuffer(
      scratchSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
      0,
      scratchBuffer, scratchMemory);

    bool useExplicit = false;
    bool useDedicatedVertices = true;

    if (useExplicit) {
    }
    else {
      createBuffer(
        sizesInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        0,
        clusterBuffer, clusterMemory);
    }

    VkBuffer clusterBuildInfoBuffer;
    VkDeviceMemory clusterBuildInfoMemory;
    VkBuffer clusterDstBuffer;
    VkDeviceMemory clusterDstMemory;
    VkBuffer clusterSizeBuffer;
    VkDeviceMemory clusterSizeMemory;

    size_t clusterBuildInfoSize = sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV) * g_cluster_count;
    createBuffer(clusterBuildInfoSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      clusterBuildInfoBuffer, clusterBuildInfoMemory);

    size_t clusterDstBufferSize = sizeof(uint64_t) * g_cluster_count;
    createBuffer(clusterDstBufferSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      clusterDstBuffer, clusterDstMemory);

    size_t clusterSizeBufferSize = sizeof(uint32_t) * g_cluster_count;
    createBuffer(clusterSizeBufferSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      clusterSizeBuffer, clusterSizeMemory);

    // Fill build info
    std::vector<VkClusterAccelerationStructureBuildTriangleClusterInfoNV> buildInfos(g_cluster_count);
    std::vector<uint64_t> buildDsts(useExplicit ? g_cluster_count : 0);

    for (uint32_t c = 0; c < g_cluster_count; c++) {
      const VertexAndIndex& vi = vertex_and_indices[c];
      // input
      VkClusterAccelerationStructureBuildTriangleClusterInfoNV& buildInfo = buildInfos[c];
      buildInfo = {};
      buildInfo.clusterID = c;
      buildInfo.vertexCount = vi.vertices.size();
      buildInfo.triangleCount = vi.indices.size() / 3;
      buildInfo.baseGeometryIndexAndGeometryFlags.geometryFlags = VK_CLUSTER_ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT_NV;

      if (useDedicatedVertices) {
        buildInfo.indexBuffer = getBufferDeviceAddress(clusterIndexBuffers[c]);
        buildInfo.indexBufferStride = sizeof(uint32_t);
        buildInfo.indexType = VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_32BIT_NV;
        buildInfo.vertexBuffer = getBufferDeviceAddress(clusterVertexBuffers[c]);
        buildInfo.vertexBufferStride = sizeof(glm::vec3);
      }
      else {
      }

      if (useExplicit) {
        assert(0);
      }
    }

    uint8_t* data{};
    vkMapMemory(device, clusterBuildInfoMemory, 0, clusterBuildInfoSize, 0, (void**)&data);
    memcpy(data, buildInfos.data(), clusterBuildInfoSize);
    vkUnmapMemory(device, clusterBuildInfoMemory);

    // UpdateRayTracingClusters
    VkClusterAccelerationStructureCommandsInfoNV cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV;
    //VkClusterAccelerationStructureInputInfoNV inputs{};
    inputs = {};
    inputs.sType = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV;

    inputs.maxAccelerationStructureCount = g_cluster_count;
    inputs.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
    inputs.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
    inputs.opInput.pTriangleClusters = &clusterTriangleInput;
    inputs.flags = 0;

    cmdInfo.dstImplicitData = getBufferDeviceAddress(clusterBuffer);

    cmdInfo.dstAddressesArray.deviceAddress = getBufferDeviceAddress(clusterDstBuffer);
    cmdInfo.dstAddressesArray.size = clusterDstBufferSize;
    cmdInfo.dstAddressesArray.stride = sizeof(uint64_t);

    cmdInfo.dstSizesArray.deviceAddress = getBufferDeviceAddress(clusterSizeBuffer);
    cmdInfo.dstSizesArray.size = clusterSizeBufferSize;
    cmdInfo.dstSizesArray.stride = sizeof(uint32_t);

    cmdInfo.srcInfosArray.deviceAddress = getBufferDeviceAddress(clusterBuildInfoBuffer);
    cmdInfo.srcInfosArray.size = clusterBuildInfoSize;
    cmdInfo.srcInfosArray.stride = sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV);

    cmdInfo.scratchData = getBufferDeviceAddress(scratchBuffer);
    cmdInfo.input = inputs;

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    PFN_vkCmdBuildClusterAccelerationStructureIndirectNV
      funcCmdBuildClusterAccelerationStructureIndirectNV =
      (PFN_vkCmdBuildClusterAccelerationStructureIndirectNV)vkGetInstanceProcAddr(
        instance, "vkCmdBuildClusterAccelerationStructureIndirectNV");
    assert(funcCmdBuildClusterAccelerationStructureIndirectNV);
    funcCmdBuildClusterAccelerationStructureIndirectNV(commandBuffer, &cmdInfo);
    endSingleTimeCommands(commandBuffer);

    if (true) {
      printf("Done. Dst Sizes:");
      vkMapMemory(device, clusterSizeMemory, 0, clusterSizeBufferSize, 0, (void**)&data);
      for (uint32_t i = 0; i < g_cluster_count; i++) {
        uint32_t s = ((uint32_t*)(data))[i];
        printf(" %u", s);
      }
      printf("\n");
      vkUnmapMemory(device, clusterSizeMemory);

      printf("Dst Addrs:");
      vkMapMemory(device, clusterDstMemory, 0, clusterDstBufferSize, 0, (void**)&data);
      for (uint32_t i = 0; i < g_cluster_count; i++) {
        uint64_t addr = ((uint64_t*)(data))[i];
        printf(" %p", (void*)(addr));
      }
      printf("\n");
      vkUnmapMemory(device, clusterDstMemory);
    }

    if (false) {
      vkMapMemory(device, clusterMemory, 0, sizesInfo.accelerationStructureSize, 0, (void**)&data);
      printf("cluster buffer:");
      for (uint32_t i = 0; i < std::min(1000, int(sizesInfo.accelerationStructureSize)); i++) {
        if (i % 32 == 0) {
          printf("\n%u:", i);
        }
        printf(" %02x", data[i]);
      }
      printf("\n");
      vkUnmapMemory(device, clusterMemory);
    }

    // Init cluster_BLAS
    VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV blasInfo{};
    blasInfo.clusterReferences = getBufferDeviceAddress(clusterDstBuffer);
    blasInfo.clusterReferencesCount = g_cluster_count;
    blasInfo.clusterReferencesStride = sizeof(uint64_t);

    VkBuffer clusterBlasInfoBuffer;
    VkDeviceMemory clusterBlasInfoMemory;
    VkBuffer clusterBlasSizeBuffer;
    VkDeviceMemory clusterBlasSizeMemory;

    size_t clusterBlasInfoSize = sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV);
    createBuffer(clusterBlasInfoSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      clusterBlasInfoBuffer, clusterBlasInfoMemory);

    vkMapMemory(device, clusterBlasInfoMemory, 0, clusterBlasInfoSize, 0, (void**)&data);
    memcpy(data, &blasInfo, sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV));
    vkUnmapMemory(device, clusterBlasInfoMemory);

    size_t clusterBlasSizeSize = sizeof(uint32_t);
    createBuffer(clusterBlasSizeSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      clusterBlasSizeBuffer, clusterBlasSizeMemory);

    VkClusterAccelerationStructureClustersBottomLevelInputNV clusterBlasInput{};
    clusterBlasInput.sType = VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV;
    clusterBlasInput.maxClusterCountPerAccelerationStructure = g_cluster_count;
    clusterBlasInput.maxTotalClusterCount = g_cluster_count;
    
    // VkClusterAccelerationStructureInputInfoNV inputs{};
    inputs = {};
    inputs.maxAccelerationStructureCount = 1;
    inputs.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
    inputs.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
    inputs.opInput.pClustersBottomLevel = &clusterBlasInput;
    inputs.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

    funcGetClusterAccelerationStructureBuildSizesNV(device, &inputs, &sizesInfo);
    scratchSize = sizesInfo.buildScratchSize;
    printf("cluster blas, AS=%zu, scratch=%zu\n", sizesInfo.accelerationStructureSize, scratchSize);

    vkDestroyBuffer(device, scratchBuffer, nullptr);
    vkFreeMemory(device, scratchMemory, nullptr);

    createBuffer(
      scratchSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      scratchBuffer, scratchMemory);

    VkBuffer clusterBlasDstBuffer;
    VkDeviceMemory clusterBlasDstMemory;
    createBuffer(
      sizeof(uint64_t),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      clusterBlasDstBuffer, clusterBlasDstMemory);

    // BLAS itself.
    createBuffer(
      sizesInfo.accelerationStructureSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      clusterBlasBuffer, clusterBlasMemory);

    // Update Blas
    cmdInfo.dstAddressesArray.deviceAddress = getBufferDeviceAddress(clusterBlasDstBuffer);
    cmdInfo.dstAddressesArray.size = sizeof(uint64_t);
    cmdInfo.dstAddressesArray.stride = sizeof(uint64_t);
    
    cmdInfo.dstSizesArray.deviceAddress = getBufferDeviceAddress(clusterBlasSizeBuffer);
    cmdInfo.dstSizesArray.size = sizeof(uint32_t);
    cmdInfo.dstSizesArray.stride = sizeof(uint32_t);

    cmdInfo.srcInfosArray.deviceAddress = getBufferDeviceAddress(clusterBlasInfoBuffer);
    cmdInfo.srcInfosArray.size = sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV);
    cmdInfo.srcInfosArray.stride = sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV);

    cmdInfo.dstImplicitData = getBufferDeviceAddress(clusterBlasBuffer);
    cmdInfo.scratchData = getBufferDeviceAddress(scratchBuffer);
    cmdInfo.input = inputs;

    commandBuffer = beginSingleTimeCommands();
    funcCmdBuildClusterAccelerationStructureIndirectNV(commandBuffer, &cmdInfo);
    endSingleTimeCommands(commandBuffer);

    {
      // Should be the same
      vkMapMemory(device, clusterBlasDstMemory, 0, sizeof(uint64_t), 0, (void**)&data);
      uint64_t x = *((uint64_t*)data);
      printf("BLAS addr: %p vs %p\n",
        (void*)(x), (void*)(getBufferDeviceAddress(clusterBlasBuffer)));
      vkUnmapMemory(device, clusterBlasDstMemory);
    }

    // TLAS
    // Instance Info
    VkAccelerationStructureInstanceKHR blasInst{};
    blasInst.transform.matrix[0][0] = 1;
    blasInst.transform.matrix[1][1] = 1;
    blasInst.transform.matrix[2][2] = 1;
    blasInst.mask = 0xFF;
    blasInst.flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
    blasInst.accelerationStructureReference = getBufferDeviceAddress(clusterBlasBuffer);
    
    createBuffer(
      sizeof(VkAccelerationStructureInstanceKHR),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      clusterTlasInstancesBuffer, clusterTlasInstancesMemory);

    vkMapMemory(device, clusterTlasInstancesMemory, 0, sizeof(VkAccelerationStructureInstanceKHR), 0, (void**)&data);
    memcpy(data, &blasInst, sizeof(blasInst));
    vkUnmapMemory(device, clusterTlasInstancesMemory);

    // Geom Info
    VkAccelerationStructureGeometryKHR geomData{};
    geomData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geomData.flags = 0;
    geomData.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geomData.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geomData.geometry.instances.data.deviceAddress = getBufferDeviceAddress(clusterTlasInstancesBuffer);

    // TLAS build info
    VkAccelerationStructureBuildGeometryInfoKHR tlasGeomInfo{};
    tlasGeomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tlasGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlasGeomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    tlasGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tlasGeomInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    tlasGeomInfo.dstAccelerationStructure = VK_NULL_HANDLE;
    tlasGeomInfo.geometryCount = 1;
    tlasGeomInfo.pGeometries = &geomData;

    uint32_t primCount = 1;

    PFN_vkGetAccelerationStructureBuildSizesKHR funcGetAccelerationStructureBuildSizes =
      (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetInstanceProcAddr(
        instance, "vkGetAccelerationStructureBuildSizesKHR");
    assert(funcGetAccelerationStructureBuildSizes);
    funcGetAccelerationStructureBuildSizes(device,
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &tlasGeomInfo,
      &primCount,
      &sizesInfo);

    printf("tlas build: as=%u, build scratch=%u, upd scratch=%u\n",
      sizesInfo.accelerationStructureSize, sizesInfo.buildScratchSize, sizesInfo.updateScratchSize);

    vkDestroyBuffer(device, scratchBuffer, nullptr);
    vkFreeMemory(device, scratchMemory, nullptr);

    createBuffer(sizesInfo.buildScratchSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
      0,
      scratchBuffer, scratchMemory);

    createBuffer(sizesInfo.accelerationStructureSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
      0,
      clusterTlasBuffer, clusterTlasMemory);

    tlasGeomInfo.scratchData.deviceAddress = getBufferDeviceAddress(scratchBuffer);
    VkAccelerationStructureCreateInfoKHR asCreateInfo{};
    asCreateInfo = {};
    asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCreateInfo.createFlags = 0;
    asCreateInfo.buffer = clusterTlasBuffer;
    asCreateInfo.offset = 0;
    asCreateInfo.size = sizesInfo.accelerationStructureSize;
    asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    
    PFN_vkCreateAccelerationStructureKHR funcCreateAccelerationStructure =
      (PFN_vkCreateAccelerationStructureKHR)vkGetInstanceProcAddr(
        instance, "vkCreateAccelerationStructureKHR");
    if (funcCreateAccelerationStructure(device, &asCreateInfo, nullptr, &clusterTlas) != VK_SUCCESS) {
      throw std::runtime_error("Could not create TLAS");
    }
    tlasGeomInfo.dstAccelerationStructure = clusterTlas;
    commandBuffer = beginSingleTimeCommands();

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.primitiveCount = 1;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.transformOffset = 0;

    VkAccelerationStructureBuildRangeInfoKHR* const buildRangeInfos[] = { &buildRangeInfo };
    PFN_vkCmdBuildAccelerationStructuresKHR funcCmdBuildAccelerationStructuresKHR =
      (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetInstanceProcAddr(
        instance, "vkCmdBuildAccelerationStructuresKHR");
    funcCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &tlasGeomInfo, buildRangeInfos);
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      0, 1, &barrier,
      0, nullptr,
      0, nullptr);
    endSingleTimeCommands(commandBuffer);

    vkDestroyBuffer(device, clusterBuildInfoBuffer, nullptr);
    vkFreeMemory(device, clusterBuildInfoMemory, nullptr);
    vkDestroyBuffer(device, clusterDstBuffer, nullptr);
    vkFreeMemory(device, clusterDstMemory, nullptr);
    vkDestroyBuffer(device, clusterSizeBuffer, nullptr);
    vkFreeMemory(device, clusterSizeMemory, nullptr);
    vkDestroyBuffer(device, clusterBlasInfoBuffer, nullptr);
    vkFreeMemory(device, clusterBlasInfoMemory, nullptr);
    vkDestroyBuffer(device, clusterBlasSizeBuffer, nullptr);
    vkFreeMemory(device, clusterBlasSizeMemory, nullptr);
    vkDestroyBuffer(device, scratchBuffer, nullptr);
    vkFreeMemory(device, scratchMemory, nullptr);
  }

  void updateAS() {
    createOrUpdateAS(true);
  }

  void createRtUniformBuffer() {
    size_t sz = sizeof(RtPerSceneUniformBuffer);
    createBuffer(sz,
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      perSceneRtUniformBuffer,
      perSceneRtUniformBufferMemory);
  }

  void createComputeDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[3]{};

    bindings[0].binding = 0;
    bindings[0].descriptorCount = 1;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].pImmutableSamplers = nullptr;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorCount = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].pImmutableSamplers = nullptr;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorCount = 1;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].pImmutableSamplers = nullptr;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = _countof(bindings);
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create compute descriptor set layout");
    }

    // compute per-scene cb
    size_t sz = sizeof(RtPerSceneVertexProcessingDataBuffer);
    createBuffer(sz,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      rtPerSceneVertexProcessingBuffer,
      rtPerSceneVertexProcessingBufferMemory);
  }

  void createComputeDescriptorPool() {
    VkDescriptorPoolSize poolSizes[1]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 3;
    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.poolSizeCount = _countof(poolSizes);
    poolCreateInfo.pPoolSizes = poolSizes;
    poolCreateInfo.maxSets = 1;
    poolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if (vkCreateDescriptorPool(device, &poolCreateInfo, nullptr, &computeDescriptorPool) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create RT descriptor pool");
    }
  }

  void createComputeDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = computeDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &computeDescriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &computeDescriptorSet) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate compute descriptor set");
    }

    VkWriteDescriptorSet writes[3]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = computeDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    VkDescriptorBufferInfo bi[3]{};
    bi[0].buffer = vertexBuffer;
    bi[0].offset = 0;
    bi[0].range = sizeof(glm::vec3) * g_vertex_count;
    writes[0].pBufferInfo = &(bi[0]);
    
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = computeDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bi[1].buffer = vertexBufferDisplaced;
    bi[1].offset = 0;
    bi[1].range = sizeof(glm::vec3) * g_vertex_count;
    writes[1].pBufferInfo = &(bi[1]);

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = computeDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bi[2].buffer = rtPerSceneVertexProcessingBuffer;
    bi[2].offset = 0;
    bi[2].range = sizeof(RtPerSceneVertexProcessingDataBuffer);
    writes[2].pBufferInfo = &(bi[2]);

    vkUpdateDescriptorSets(device, _countof(writes), writes, 0, nullptr);
  }

  void createComputePipeline() {
    // 1. pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) {
      printf("Failed to create compute pipeline layout");
    }

    // 2. pipeline shader stage
    std::vector<char> compShaderCode = readFile("shaders/comp.spv");
    VkShaderModule compShaderModule = createShaderModule(compShaderCode);
    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = compShaderModule;
    computeShaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = computePipelineLayout;
    pipelineInfo.stage = computeShaderStageInfo;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create compute pipeline");
    }
  }

  uint32_t g_vertex_count = 0;
  uint32_t g_index_count = 0;

  uint32_t g_cluster_count = 0;
  uint32_t g_cluster_tri_count = 0;
  uint32_t g_cluster_vert_count = 0;

  std::vector<glm::vec3> vertices;
  std::vector<glm::vec3> normals;
  std::vector<uint32_t> indices;

  VkInstance instance;
  VkDebugUtilsMessengerEXT debugMessenger;
  VkSurfaceKHR surface;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  VkQueue graphicsQueue, presentQueue;
  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;
  std::vector<VkImageView> swapChainImageViews;
  VkRenderPass renderPass;
  VkCommandPool commandPool;
  VkCommandBuffer commandBuffer;
  VkPipeline graphicsPipeline;
  VkPipelineLayout pipelineLayout;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorPool descriptorPool;
  VkDescriptorSet descriptorSets[1]{};
  std::vector<VkFramebuffer> swapChainFramebuffers;
  VkSemaphore imageAvailableSemaphore;
  VkSemaphore renderFinishedSemaphore;
  VkFence inFlightFence;
  VkBuffer vertexBuffer;
  VkBuffer indexBuffer;
  VkBuffer normalBuffer;
  VkDeviceMemory vertexBufferMemory;
  VkDeviceMemory indexBufferMemory;
  VkDeviceMemory normalBufferMemory;
  VkBuffer perSceneUniformBuffer;
  VkDeviceMemory perSceneUniformBufferMemory;
  VkImage depthImage;
  VkDeviceMemory depthImageMemory;
  VkImageView depthImageView;
  VkImage rtOutputImages[MAX_FRAMES_IN_FLIGHT];
  VkDeviceMemory rtOutputImageMemories[MAX_FRAMES_IN_FLIGHT];
  VkImageView rtOutputImageViews[MAX_FRAMES_IN_FLIGHT];

  std::vector<VertexAndIndex> vertex_and_indices;

  // Not using indirect
  std::vector<VkBuffer> clusterVertexBuffers;
  std::vector<VkBuffer> clusterIndexBuffers;
  VkDeviceMemory clusterVertexAndIndexMemory;
  // Using indirect
  VkBuffer clusterVertexBufferForIndirect;
  VkBuffer clusterIndexBufferForIndirect;
  VkDeviceMemory clusterVertexAndIndexMemoryForIndirect;

  std::vector<glm::vec3> cluster_normals;  // Used with vkCmdDrawIndexedIndirect
  std::vector<MyIndirectClusterDraw> clusterIndirectCommands;
  VkBuffer clusterIndirectCommandBuffer;
  VkDeviceMemory clusterIndirectCommandMemory;
  VkBuffer clusterNormalsBufferForIndirect;
  VkDeviceMemory clusterNormalsMemoryForIndirect;
  VkBuffer clusterNormalsOffsetBufferForIndirect;
  VkDeviceMemory clusterNormalsOffsetMemoryForIndirect;
  VkBuffer clusterVertexIdxOffsetBuffer;
  VkDeviceMemory clusterVertexIdxOffsetMemory;

  // RT
  VkDescriptorSetLayout rtDescriptorSetLayout;
  VkDescriptorPool rtDescriptorPool;
  VkDescriptorSet rtDescriptorSet;
  VkPipelineLayout rtPipelineLayout;
  VkPipeline rtPipeline;
  VkBuffer sbtBuffer;
  VkDeviceMemory sbtMemory;
  VkStridedDeviceAddressRegionKHR rtRGenRegion{}, rtMissRegion{}, rtHitRegion{}, rtCallRegion{};
  VkBuffer blasResultBuffer;
  VkDeviceMemory blasResultMemory;
  VkAccelerationStructureKHR blas;
  VkBuffer tlasInstancesBuffer;
  VkDeviceMemory tlasInstancesMemory;
  VkBuffer tlasResultBuffer;
  VkDeviceMemory tlasResultMemory;
  VkAccelerationStructureKHR tlas;
  VkBuffer perSceneRtUniformBuffer;
  VkDeviceMemory perSceneRtUniformBufferMemory;

  // Compute
  VkDescriptorSetLayout computeDescriptorSetLayout;
  VkDescriptorPool computeDescriptorPool;
  VkDescriptorSet computeDescriptorSet;
  VkPipelineLayout computePipelineLayout;
  VkPipeline computePipeline;
  VkBuffer rtPerSceneVertexProcessingBuffer;
  VkDeviceMemory rtPerSceneVertexProcessingBufferMemory;
  VkFence computeInFlightFence;
  VkSemaphore computeFinishedSemaphore;
  VkBuffer vertexBufferDisplaced;  // Displaced by compute shader
  VkDeviceMemory vertexBufferDisplacedMemory;

  // CLAS
  VkBuffer clasBuildTriangleClusterInfosBuffer;
  VkDeviceMemory clasBuildTriangleClusterInfosMemory;
  VkBuffer clusterBuffer;  // CLAS
  VkDeviceMemory clusterMemory;
  VkBuffer clusterBlasBuffer; // Cluster BLAS
  VkDeviceMemory clusterBlasMemory;
  VkBuffer clusterTlasBuffer; // Cluster TLAS
  VkAccelerationStructureKHR clusterTlas;
  VkDeviceMemory clusterTlasMemory;
  VkBuffer clusterTlasInstancesBuffer;
  VkDeviceMemory clusterTlasInstancesMemory;
};

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  if (action == GLFW_PRESS) {
    switch (key) {
    case GLFW_KEY_ESCAPE: {
      g_app->should_exit = true;
      break;
    }
    case GLFW_KEY_SPACE: {
      g_is_rt = !g_is_rt;
      SetWindowTitle();
      break;
    }
    case GLFW_KEY_C: {
      g_is_cluster = !g_is_cluster;
      SetWindowTitle();
      break;
    }
    case GLFW_KEY_R: {
      g_is_rotate = !g_is_rotate;
      break;
    }
    default:
      break;
    }
  }
}

int main() {
  printf("Hey.\n");

  g_app = new HelloClasApplication();
  try {
    g_app->run();
  }
  catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  delete g_app;
  return EXIT_SUCCESS;
}