#include "myframework_vk.h"

#include <stdio.h>
#include <stdexcept>

MyFrameworkVk* g_framework{};

const uint32_t WIN_W = 800, WIN_H = 600;

MyFrameworkVk::MyRtShaderListInfo g_info;
VkDescriptorSet g_ds;
VkImage g_rt_output_image;
VkImageView g_rt_output_image_view;
VkDeviceMemory g_rt_output_memory;

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
  VkSemaphore semaphore = g_framework->imageAvailableSemaphore;

  vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
  vkResetFences(device, 1, &fence);
  uint32_t imageIndex;
  vkAcquireNextImageKHR(device, g_framework->swapChain, UINT64_MAX, semaphore, VK_NULL_HANDLE, &imageIndex);
  vkResetCommandBuffer(g_framework->commandBuffer, 0);

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
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_info.rtPipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_info.rtPipelineLayout, 0, 1, &g_ds, 0, nullptr);
  PFN_vkCmdTraceRaysKHR funcCmdTraceRaysKHR =
    (PFN_vkCmdTraceRaysKHR)vkGetInstanceProcAddr(
      g_framework->instance, "vkCmdTraceRaysKHR");
  funcCmdTraceRaysKHR(commandBuffer, &g_info.rtRgenRegion, &g_info.rtMissRegion, &g_info.rtHitRegion, &g_info.rtCallRegion, WIN_W, WIN_H, 1);

  // BLIT
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.image = g_rt_output_image;
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
  barrier.image = g_framework->swapChainImages[imageIndex];
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(commandBuffer,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, nullptr, 0, nullptr, 1, &barrier);

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
  barrier.image = g_rt_output_image;
  vkCmdPipelineBarrier(commandBuffer,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    0, 0, nullptr, 0, nullptr, 1, &barrier);

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
  g_info = g_framework->CreateMyRtPipeline();
  std::vector<std::pair<VkDescriptorType, uint32_t>> sizes = {
    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
  };
  uint32_t nfif = g_framework->swapChainFramebuffers.size();
  VkDescriptorPool pool = g_framework->CreateCBVSRVUAVPool(sizes, nfif);
  g_ds = g_framework->CreateDescriptorSet(g_info.rtPipeDSL, pool);
  g_framework->CreateRtOutputResource(WIN_W, WIN_H, g_rt_output_image, g_rt_output_memory, g_rt_output_image_view);
  g_framework->CreateUAVTexture2D(g_rt_output_image_view, g_ds, 1);
  MainLoop();
  printf("MyFrameworkVk is done.\n");
  exit(0);
}