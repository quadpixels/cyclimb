#include "myframework_vk.h"

#include <stdio.h>
#include <stdexcept>

#include <glm/glm.hpp>

MyFrameworkVk* g_framework{};

const uint32_t WIN_W = 800, WIN_H = 600;

MyFrameworkVk::MyRtPipeline g_my_rt_pipeline;

VkDescriptorSet g_ds;
VkImage g_rt_output_image;
VkImageView g_rt_output_image_view;
VkDeviceMemory g_rt_output_memory;
VkBuffer g_vb;
VkDeviceMemory g_vb_memory;
VkBuffer g_ib;
VkDeviceMemory g_ib_memory;
VkAccelerationStructureKHR g_blas;
VkBuffer g_blas_result_buf;
VkDeviceMemory g_blas_result_memory;
VkAccelerationStructureKHR g_tlas;
VkBuffer g_tlas_result_buf;
VkDeviceMemory g_tlas_result_memory;

static bool should_exit = false;

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  if (action == GLFW_PRESS) {
    switch (key) {
    case GLFW_KEY_ESCAPE: {
      should_exit = true;
      break;
    }
    case GLFW_KEY_SPACE: {
      break;
    }
    default:
      break;
    }
  }
}

void Render() {
  VkDevice device = g_framework->GetLogicalDevice();
  VkFence fence = g_framework->inFlightFence;
  VkCommandBuffer commandBuffer = g_framework->commandBuffer;
  static uint32_t lastImageIndex;

  VkSemaphore semaphore = g_framework->imageAvailableSemaphore[lastImageIndex];
  vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
  vkResetFences(device, 1, &fence);
  uint32_t imageIndex;
  vkAcquireNextImageKHR(device, g_framework->swapChain, UINT64_MAX, semaphore, VK_NULL_HANDLE, &imageIndex);
  vkResetCommandBuffer(g_framework->commandBuffer, 0);
  lastImageIndex = imageIndex;

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
  renderPassInfo.clearValueCount = 1;
  renderPassInfo.pClearValues = &clearColor;

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdEndRenderPass(commandBuffer);

  // RT op must be outside of a pass
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_my_rt_pipeline.rtPipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_my_rt_pipeline.rtPipelineLayout, 0, 1, &g_ds, 0, nullptr);
  PFN_vkCmdTraceRaysKHR funcCmdTraceRaysKHR =
    (PFN_vkCmdTraceRaysKHR)vkGetInstanceProcAddr(
      g_framework->instance, "vkCmdTraceRaysKHR");
  funcCmdTraceRaysKHR(commandBuffer,
    &g_my_rt_pipeline.rtRgenRegion,
    &g_my_rt_pipeline.rtMissRegion,
    &g_my_rt_pipeline.rtHitRegion,
    &g_my_rt_pipeline.rtCallRegion, WIN_W, WIN_H, 1);

  // BLIT
  g_framework->ImageMemoryBarrier(commandBuffer,
    g_rt_output_image,
    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    0, VK_ACCESS_TRANSFER_READ_BIT,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT
  );

  g_framework->ImageMemoryBarrier(commandBuffer,
    g_framework->swapChainImages[imageIndex],
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    0, VK_ACCESS_TRANSFER_WRITE_BIT,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT
  );

  VkImageBlit blit{};
  blit.srcOffsets[1] = { WIN_W, WIN_H, 1 };
  blit.dstOffsets[1] = { WIN_W, WIN_H, 1 };
  blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.srcSubresource.layerCount = 1;
  blit.dstSubresource.layerCount = 1;

  vkCmdBlitImage(commandBuffer,
    g_rt_output_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    g_framework->swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    1, &blit, VK_FILTER_LINEAR);

  g_framework->ImageMemoryBarrier(commandBuffer,
    g_framework->swapChainImages[imageIndex],
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    VK_ACCESS_TRANSFER_WRITE_BIT, 0,
    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
  );

  g_framework->ImageMemoryBarrier(commandBuffer,
    g_rt_output_image,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
    VK_ACCESS_TRANSFER_WRITE_BIT, 0,
    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
  );

  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("Could not end command buffer");
  }

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  VkSemaphore waitSemaphores[] = { semaphore };
  submitInfo.pWaitSemaphores = waitSemaphores;
  VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  VkSemaphore signalSemaphores[] = { g_framework->renderFinishedSemaphore };
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;
  if (vkQueueSubmit(g_framework->graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
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

void MainLoop() {
  while (!glfwWindowShouldClose(g_framework->GetWindow()) && !should_exit) {
    glfwPollEvents();
    Render();
  }
  vkDeviceWaitIdle(g_framework->GetLogicalDevice());
}

int main() {
  g_framework = new MyFrameworkVk();
  g_framework->InitWindow("PTLASTest", WIN_W, WIN_H, KeyCallback);
  g_framework->InitDeviceAndCommandQ();
  g_framework->InitSwapchain();
  g_framework->InitRenderPassAndFramebuffers();

  // Info
  MyFrameworkVk::MyRtShaderListInfo info{};
  info.raygen_shader = "shaders/rgen.spv";
  info.miss_shader = "shaders/miss.spv";
  info.closest_hit_shader = "shaders/rchit.spv";

  g_framework->CreateMyRtPipeline(&g_my_rt_pipeline, info);
  std::vector<std::pair<VkDescriptorType, uint32_t>> sizes = {
    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
  };

  uint32_t nfif = g_framework->swapChainFramebuffers.size();
  VkDescriptorPool pool = g_framework->CreateCBVSRVUAVPool(sizes, nfif);
  g_ds = g_framework->CreateDescriptorSet(g_my_rt_pipeline.rtPipeDSL, pool);
  g_framework->CreateRtOutputResource(WIN_W, WIN_H, g_rt_output_image, g_rt_output_memory, g_rt_output_image_view);
  g_framework->CreateUAVTexture2D(g_rt_output_image_view, g_ds, 1);
  std::vector<glm::vec3> verts = {
    { -0.5, -0.5, 0 },
    { +0.5, -0.5, 0 },
    { 0, +0.5, 0 }
  };
  std::vector<uint32_t> idxes = { 0, 1, 2 };
  g_framework->CreateBufferForCPUSideData(verts.data(), sizeof(verts[0]) * verts.size(), g_vb, g_vb_memory);
  g_framework->CreateBufferForCPUSideData(idxes.data(), sizeof(idxes[0]) * idxes.size(), g_ib, g_ib_memory);
  g_framework->BuildBLAS(g_blas, g_blas_result_buf, g_blas_result_memory, g_vb, g_ib, 3);
  g_framework->BuildTLAS(g_tlas, g_blas, g_blas_result_buf, g_tlas_result_buf, g_tlas_result_memory);
  g_framework->CreateSRVAccelerationStructure(g_tlas, g_ds, 0);

  MainLoop();
  printf("MyFrameworkVk is done.\n");
  exit(0);
}