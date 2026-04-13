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

  // Shared by all scenes.
  void InitWindow(const char* appName, uint32_t width, uint32_t height, GLFWkeyfun keyCallback);
  void InitDeviceAndCommandQ();
  void InitSwapchain();
  void InitRenderPassAndFramebuffers();
  GLFWwindow* GetWindow() {
    return window;
  }
  VkDevice GetLogicalDevice() {
    return device;
  }

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
};