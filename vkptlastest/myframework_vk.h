#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#define VK_ENABLE_BETA_EXTENSIONS
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <stdint.h>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

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
    const char* anyhit_shader{};
    bool use_omm{};  // Sets VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT
    bool use_dmm{};  // Sets VK_PIPELINE_CREATE_RAY_TRACING_DISPLACEMENT_MICROMAP_BIT_EXT
    bool use_clas{};

    VkPipelineLayout in_pipeline_layout{VK_NULL_HANDLE};  // Use this pipeline layout, otherwise create a sample one
  };
  struct MyRtPipeline {
    VkPipeline rtPipeline{};
    VkDescriptorSetLayout rtPipeDSL{};
    VkPipelineLayout rtPipelineLayout{VK_NULL_HANDLE};  // If no pipe is set in Info, create new one

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

    VkMicromapEXT outOmmArray;
  };

  struct MyDmmAttachmentInfo {
    VkDeviceAddress displacementVectorBufferAddress;
    uint32_t displacementVectorStride;
    VkFormat displacementVectorFormat;
    VkDeviceAddress displacementBiasAndScaleBufferAddress;
    uint32_t displacementBiasAndScaleStride;
    VkFormat displacementBiasAndScaleFormat;

    VkDeviceAddress indexBufferAddress;
    uint32_t num_idxes;

    std::vector<VkMicromapUsageEXT> usageCounts;
    VkMicromapEXT dmmMicromap;
  };

  // Cluster
  struct VertexAndIndex {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    std::vector<VkClusterAccelerationStructureGeometryIndexAndGeometryFlagsNV> geom_idx_and_flags;

    uint32_t vert_offset;
    uint32_t index_offset;
    uint32_t gigf_offset;
  };

  struct MyASBuildInfo {
    size_t as_size{};
    size_t clusters_size{};
  };

  // Shared by all scenes.
  void InitWindow(const char* appName, uint32_t width, uint32_t height, GLFWkeyfun keyCallback);
  void InitDeviceAndCommandQ();
  void InitSwapchain();
  void InitRenderPassAndFramebuffers(bool has_depth = false);
  void InitImGui();
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
  void CreateSRVCombinedImageSampler(VkImageView iv, VkSampler sampler, VkDescriptorSet dstSet, uint32_t binding);
  void CreateUAVBuffer(VkBuffer buffer, uint32_t offset, uint32_t range, VkDescriptorSet dstSet, uint32_t binding);
  void CreateCBVBuffer(VkBuffer buffer, uint32_t offset, uint32_t range, VkDescriptorSet dstSet, uint32_t binding);
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
    uint32_t indexCount,
    uint32_t vertex_stride = sizeof(float)*3,
    MyOmmAttachmentInfo* omminfo = nullptr,
    MyASBuildInfo* buildinfo = nullptr);
  void BuildBLASWithDMM(VkAccelerationStructureKHR& as,
    VkBuffer& outBlasResultBuffer, VkDeviceMemory& outBlasResultMemory,
    VkBuffer vb, VkBuffer ib, uint32_t maxVertex,
    uint32_t indexCount,
    uint32_t vertex_stride = sizeof(float) * 3,
    MyDmmAttachmentInfo* dmminfo = nullptr,
    MyASBuildInfo* buildinfo = nullptr);
  void BuildClusteredBLAS(
    VkAccelerationStructureKHR& as,
    VkBuffer& outBlasResultBuffer, VkDeviceMemory& outBlasResultMemory,
    std::vector<VertexAndIndex>& clusters,  // Will modify offsets
    MyASBuildInfo* buildinfo = nullptr);
  void BuildTLAS(VkAccelerationStructureKHR& outTlas,
    const VkAccelerationStructureKHR& blas0, const VkBuffer& blas0Buffer,
    VkBuffer& outTlasResultBuffer, VkDeviceMemory& outTlasResultMemory
    );
  void BuildTLAS(VkAccelerationStructureKHR& outTlas,
    std::vector<VkAccelerationStructureKHR> blases, std::vector<VkBuffer> blas_buffers,
    VkBuffer& outTlasResultBuffer, VkDeviceMemory& outTlasResultMemory
  );
  void CreateBufferForCPUSideData(void* data, uint32_t len, VkBuffer& buf, VkDeviceMemory& mem);

  void CmdTransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
  void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
  VkShaderModule CreateShaderModule(const std::vector<char>& code);
  void LoadImageFromFile(const char* fn, VkImage& image, VkImageView& image_view, VkDeviceMemory& imageMemory);
  VkSampler CreateTextureSampler();
  void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
  void CreateSyncObjects();
  VkCommandBuffer BeginSingleTimeCommands();
  void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
  VkDeviceAddress GetBufferDeviceAddress(const VkBuffer& buf);

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
  bool checkDmmExtensionSupport(VkPhysicalDevice device);
  bool checkClasExtensionSupport(VkPhysicalDevice device);
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
  VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
  void createCommandPool();
  void createCommandBuffer();
  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
  void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
  void setObjectName(uint64_t handle, VkObjectType type, const char* name);
  void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
  VkImageView createImageView(VkImage image, VkFormat format);
  void createDepthResources();
  VkFormat findDepthFormat();
  VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
  void do_BuildBLAS(VkAccelerationStructureKHR& as,
    VkBuffer& outBlasResultBuffer, VkDeviceMemory& outBlasResultMemory,
    VkBuffer vb, VkBuffer ib, uint32_t maxVertex,
    uint32_t indexCount,
    uint32_t vertex_stride = sizeof(float) * 3,
    MyOmmAttachmentInfo* omminfo = nullptr,
    MyDmmAttachmentInfo* dmminfo = nullptr,
    MyASBuildInfo* buildinfo = nullptr);

public:
  GLFWwindow* window{};
  std::string appName;
  VkInstance instance;
  std::string deviceName;
  uint32_t driverVersion[3];
  VkDebugUtilsMessengerEXT debugMessenger;
  VkSurfaceKHR surface;
  VkPhysicalDevice physicalDevice;
  bool hasOMM{ false };
  bool hasDMM{ false };
  bool hasCLAS{ false };
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
  VkImage depthImage;
  VkDeviceMemory depthImageMemory;
  VkImageView depthImageView;

  VkRenderPass imguiRenderPass{};
  std::vector<VkFramebuffer> imguiFramebuffers;
  VkDescriptorPool imguiDescriptorPool;
};