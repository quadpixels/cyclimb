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
    const char* raygen_shader{};  // Byte code path
    const char* closest_hit_shader{};
    const char* miss_shader{};
  };
  struct MyRtPipeline {
    VkPipeline rtPipeline{};
    VkDescriptorSetLayout rtPipeDSL{};
    VkPipelineLayout rtPipelineLayout{};

    // Matches DX12's hit groups
    VkStridedDeviceAddressRegionKHR rtRgenRegion, rtMissRegion, rtHitRegion, rtCallRegion;
    VkDeviceMemory sbtMemory;
    VkBuffer sbtBuffer;
  };

  struct MyOmmAttachmentInfo {
    // Input
    VkDeviceSize arrayDataSize;
    VkDeviceAddress arrayBufferAddress;
    VkDeviceAddress arrayDescsAddress;
    void* descArray;
    uint32_t descArrayCount;
    VkDeviceAddress indexBufferAddress;
    uint32_t num_idxes;

    std::vector<VkMicromapUsageEXT> usageCounts;
    std::vector<VkMicromapUsageEXT> indexHistograms;

    // Output
    VkMicromapEXT outOmmArray;
  };

  // Shared by all scenes.
  void InitWindow(const char* appName, uint32_t width, uint32_t height, GLFWkeyfun keyCallback);
  void InitDeviceAndCommandQ();
  void InitSwapchain();
  void InitRenderPassAndFramebuffers();
  void InitImGuiRenderPass();
  void CreateImGuiFramebuffers();

  void CreateMyRtPipeline(MyRtPipeline* my_rt_pipeline, MyRtShaderListInfo& info);
  static std::vector<char> ReadFile(const std::string& filename);

  GLFWwindow* GetWindow() {
    return window;
  }
  VkDevice GetLogicalDevice() {
    return device;
  }

  void CreateUAVTexture2D(VkImageView iv, VkDescriptorSet dstSet, uint32_t dstBinding);
  void CreateSRVAccelerationStructure(VkAccelerationStructureKHR as, VkDescriptorSet dstSet, uint32_t binding);
  VkDescriptorPool CreateCBVSRVUAVPool(std::vector<std::pair<VkDescriptorType, uint32_t>> sizes, uint32_t maxSets);
  VkDescriptorSet CreateDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorPool pool);
  void CreateRtOutputResource(uint32_t w, uint32_t h, VkImage& image, VkDeviceMemory& memory, VkImageView& imageView);
  void ImageMemoryBarrier(VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
    VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask);
  void BuildBLAS(VkAccelerationStructureKHR& as,
    VkBuffer& outBlasResultBuffer, VkDeviceMemory& outBlasResultMemory,
    VkBuffer vb, VkBuffer ib, uint32_t maxVertex,
    uint32_t vertex_stride = sizeof(float)*3,
    MyOmmAttachmentInfo* omminfo = nullptr);
  void BuildTLAS(VkAccelerationStructureKHR& outTlas,
    const VkAccelerationStructureKHR& blas0, const VkBuffer& blas0Buffer,
    VkBuffer& outTlasResultBuffer, VkDeviceMemory& outTlasResultMemory
    );
  void BuildTLAS(VkAccelerationStructureKHR& outTlas,
    std::vector<VkAccelerationStructureKHR> blases, std::vector<VkBuffer> blas_buffers,
    VkBuffer& outTlasResultBuffer, VkDeviceMemory& outTlasResultMemory
  );
  void CreateBufferForCPUSideData(void* data, uint32_t len, VkBuffer& buf, VkDeviceMemory& mem);

  void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
  VkShaderModule CreateShaderModule(const std::vector<char>& code);
  void LoadImageFromFile(const char* fn, VkImage& image, VkImageView& image_view, VkDeviceMemory& imageMemory);
  VkSampler CreateTextureSampler();
  void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
  void CreateSyncObjects();
  VkCommandBuffer BeginSingleTimeCommands();
  void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

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
  VkDeviceAddress getBufferDeviceAddress(const VkBuffer& buf);
  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
  void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
  void setObjectName(uint64_t handle, VkObjectType type, const char* name);
  void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
  VkImageView createImageView(VkImage image, VkFormat format);

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
  std::vector<VkSemaphore> imageAvailableSemaphore;
  VkSemaphore renderFinishedSemaphore;
  VkFence inFlightFence;
  QueueFamilyIndices queueFamilyIndices;

  VkRenderPass imguiRenderPass;
  std::vector<VkFramebuffer> imguiFramebuffers;
};