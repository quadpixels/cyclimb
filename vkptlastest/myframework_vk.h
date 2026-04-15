#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <stdint.h>
#include <optional>
#include <string>
#include <vector>

class MyFrameworkVk {
public:
  struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
  };
  struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    bool isComplete() {
      return graphicsFamily.has_value() && presentFamily.has_value();
    }
  };
  struct MyRtShaderListInfo {
    VkPipeline rtPipeline{};
    VkDescriptorSetLayout rtPipeDSL{};
    VkPipelineLayout rtPipelineLayout{};

    VkStridedDeviceAddressRegionKHR rtRgenRegion, rtMissRegion, rtHitRegion, rtCallRegion;
    VkDeviceMemory sbtMemory;
    VkBuffer sbtBuffer;
  };

  // Shared by all scenes.
  void InitWindow(const char* appName, uint32_t width, uint32_t height, GLFWkeyfun keyCallback);
  void InitDeviceAndCommandQ();
  void InitSwapchain();
  void InitRenderPassAndFramebuffers();

  MyRtShaderListInfo CreateMyRtPipeline();

  GLFWwindow* GetWindow() {
    return window;
  }
  VkDevice GetLogicalDevice() {
    return device;
  }

  void CreateUAVTexture2D(VkImageView iv, VkDescriptorSet dstSet, uint32_t dstBinding);
  VkDescriptorPool CreateCBVSRVUAVPool(std::vector<std::pair<VkDescriptorType, uint32_t>> sizes, uint32_t maxSets);
  VkDescriptorSet CreateDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorPool pool);
  void CreateRtOutputResource(uint32_t w, uint32_t h, VkImage& image, VkDeviceMemory& memory, VkImageView& imageView);

private:
  void createInstance();
  bool checkValidationLayerSupport();
  void setupDebugMessenger();
  void createSurface();
  void pickPhysicalDevice();
  void createLogicalDevice();
  bool isDeviceSuitable(VkPhysicalDevice device);
  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice);
  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
  bool do_checkDeviceExtensionSupport(VkPhysicalDevice device, std::vector<const char*> devexts);
  bool checkDeviceExtensionSupport(VkPhysicalDevice device);
  bool checkOmmExtensionSupport(VkPhysicalDevice device);
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
  VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
  void createCommandPool();
  void createCommandBuffer();
  void createSyncObjects();
  VkShaderModule createShaderModule(const std::vector<char>& code);
  void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
  VkDeviceAddress getBufferDeviceAddress(VkBuffer& buf);
  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
  void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
  VkCommandBuffer beginSingleTimeCommands();
  void endSingleTimeCommands(VkCommandBuffer commandBuffer);

public:
  GLFWwindow* window{};
  std::string appName;
  VkInstance instance;
  VkDebugUtilsMessengerEXT debugMessenger;
  VkSurfaceKHR surface;
  VkPhysicalDevice physicalDevice;
  bool hasOMM{ false };
  VkDevice device;
  VkQueue graphicsQueue, presentQueue;
  uint32_t width, height;
  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;
  std::vector<VkImageView> swapChainImageViews;
  VkCommandPool commandPool;
  VkCommandBuffer commandBuffer;
  VkRenderPass renderPass;
  std::vector<VkFramebuffer> swapChainFramebuffers;
  VkSemaphore imageAvailableSemaphore;
  VkSemaphore renderFinishedSemaphore;
  VkFence inFlightFence;
};