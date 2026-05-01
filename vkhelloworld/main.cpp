#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// Make NVRHI-VK build
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include <stdio.h>
#include <stdint.h>
#include <omm.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <optional>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "omm-gpu-nvrhi/omm-gpu-nvrhi.h"
#include "nvrhi/vulkan.h"

// ImGuI
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

// OMM handling
bool g_use_omm{ false };
const omm::Cpu::BakeResultDesc* bakeOmmForMask(uint32_t prim_idx, uint32_t level);

const char* g_tex_files[2] = {
      "textures/Paris_ivy_leaf_a_diff.png",
      "textures/Paris_ivy_leaf_a_mask.png",
};

std::vector<std::string> g_diff_maps(128), g_alpha_maps(128);

struct Vertex {
  alignas(16) glm::vec3 pos;
  alignas(16) glm::vec3 color;
  alignas(16) glm::vec2 uv; int mat_idx;
  int pad;
};

std::string g_obj_name = "paris_ivy_leaf.obj";

// Clockwise
std::vector<Vertex> g_vertices = {
      { { -0.5, -0.5, 0}, { 1, 0, 0 }, { 0, 0 }, -1 },
      { { 0.5, 0.5, 0}, { 0, 1, 0 }, { 1, 1 }, -1 },
      { { -0.5, 0.5, 0}, { 0, 0, 1 }, { 0, 1 }, -1 },

      { { -0.5, -0.5, 0}, { 1, 0, 0 }, { 0, 0 }, -1 },
      { { 0.5, -0.5, 0}, { 0, 0, 1 }, { 1, 0 }, -1 },
      { { 0.5, 0.5, 0}, { 0, 1, 0 }, { 1, 1 }, -1 },
};

uint32_t g_alpha_tex_w{ 0 }, g_alpha_tex_h{ 0 };

GLFWwindow* window{};
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
bool g_is_rt = false;
const uint32_t MAX_FRAMES_IN_FLIGHT = 3;
class HelloTriangleApplication;
HelloTriangleApplication* g_app;

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
};

std::vector<const char*> ommExtensions = {
  VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME,
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

void SetWindowTitle() {
  if (!g_is_rt) {
    glfwSetWindowTitle(window, "Vulkan triangle (rast)");
  }
  else {
    glfwSetWindowTitle(window, "Vulkan triangle (RT)");
  }
}

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

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

std::vector<const char*> getRequiredExtensions() {
  uint32_t glfwExtensionsCount = 0;
  const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);
  std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionsCount);
  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  return extensions;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageType,
  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
  void* pUserData) {
  printf("Validation error: %s\n", pCallbackData->pMessage);
  return VK_FALSE;
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

void DestroyDebugUtilsMessengerEXT(VkInstance instance,
  VkDebugUtilsMessengerEXT debugMessenger,
  const VkAllocationCallbacks* pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
    instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func) {
    func(instance, debugMessenger, pAllocator);
  }
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

class HelloTriangleApplication {
public:
  void run() {
    initWindow();
    initVulkan();
    initImGui();
    initOMMBaker();
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
    createSwapChain();
    createImageViews();
    createRenderPass();
    createCommandPool();
    createCommandBuffer();
    readOBJ();
    createTextureImage();
    createTextureImageView();
    createTextureSampler();
    createVertexBuffer();
    createUVBuffer();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets();
    createGraphicsPipeline();
    createFramebuffers();
    createSyncObjects();
    createAS();
    createRtOutputImage();
    createRtDescriptorSetLayout();
    createRtDescriptorPool();
    createRtDescriptorSets();
    createRtPipeline();
    createRtSBT();
  }

  void initImGui() {
    printf("Checking IMGUI version: %s\n", IMGUI_VERSION);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui_ImplGlfw_InitForVulkan(window, true);

    VkDescriptorPoolSize imgui_pool_sizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(imgui_pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(imgui_pool_sizes);
    pool_info.pPoolSizes = imgui_pool_sizes;

    vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiDescriptorPool);


    // 4. 初始化 Vulkan 后端（新版需要提供 PipelineRenderingCreateInfo 或 color attachment 格式）
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = findQueueFamilies(physicalDevice).graphicsFamily.value();
    init_info.Queue = graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = imguiDescriptorPool;
    init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
    init_info.ImageCount = swapChainImages.size();
    init_info.UseDynamicRendering = false;
    init_info.PipelineInfoMain.RenderPass = renderPass;
    init_info.PipelineInfoMain.Subpass = 0;

    // 初始化 Vulkan 后端
    bool ret = ImGui_ImplVulkan_Init(&init_info);      // 这里的 renderPass 是你现有的渲染通道
    printf("ImGui_ImplVulkan_Init returned %d\n", ret);
  }

  void initOMMBaker() {
    vk::detail::defaultDispatchLoaderDynamic.init(vkGetInstanceProcAddr);
    vk::detail::defaultDispatchLoaderDynamic.init(instance, vkGetInstanceProcAddr);

    nvrhi::vulkan::DeviceDesc desc{};
    desc.instance = instance;
    desc.physicalDevice = physicalDevice;
    desc.device = device;
    desc.graphicsQueue = graphicsQueue;
    desc.graphicsQueueIndex = graphicsQueueFamilyIndex;
    desc.deviceExtensions = deviceExtensions.data();
    desc.numDeviceExtensions = deviceExtensions.size();
    nvrhi::DeviceHandle nvrhiDevice = nvrhi::vulkan::createDevice(desc);
    auto commandList = nvrhiDevice->createCommandList();
    commandList->open();
    omm::GpuBakeNvrhi baker(nvrhiDevice, commandList, false);
    printf("[initOMMBaker] Successfully created a GPU Baker.\n");

    nvrhi::TextureDesc texDesc{};
    texDesc.width = 256;
    texDesc.height = 256;
    texDesc.depth = 1;
    texDesc.arraySize = 1;
    texDesc.mipLevels = 1;
    texDesc.dimension = nvrhi::TextureDimension::Texture2D;
    texDesc.format = nvrhi::Format::SRGBA8_UNORM;
    texDesc.debugName = "AlphaTexture";
    texDesc.isShaderResource = true;
    texDesc.initialState = nvrhi::ResourceStates::Unknown;
    texDesc.keepInitialState = false;

    nvrhi::TextureHandle alphaTextureNvrhi =
      nvrhiDevice->createHandleForNativeTexture(
        nvrhi::ObjectTypes::VK_Image,
        alphaMapImage[0],
        texDesc
      );

    omm::GpuBakeNvrhi::Input input{};
    input.alphaTexture = alphaTextureNvrhi;
    input.alphaTextureChannel = 0;
    input.operation = omm::GpuBakeNvrhi::Operation::SetupAndBake;

    // alpha test rule
    input.alphaCutoff = 0.5f;
    input.alphaCutoffLessEqual = omm::OpacityState::Transparent;
    input.alphaCutoffGreater = omm::OpacityState::Opaque;

    // UV buffer
    nvrhi::BufferDesc bufDesc{};
    nvrhi::BufferHandle uvBufferHandle;
    bufDesc.byteSize = sizeof(glm::vec2) * g_vertices.size();
    bufDesc.debugName = "UVBufferForOMMBaker";
    bufDesc.canHaveRawViews = true;
    bufDesc.canHaveUAVs = false;
    bufDesc.canHaveTypedViews = true;
    bufDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    bufDesc.keepInitialState = true;
    bufDesc.setFormat(nvrhi::Format::RG32_FLOAT);

    uvBufferHandle = nvrhiDevice->createHandleForNativeBuffer(
      nvrhi::ObjectTypes::VK_Buffer,
      uvBuffer,
      bufDesc
    );

    // Index Buffer
    bufDesc = {};
    nvrhi::BufferHandle indexBufferHandle;
    bufDesc.byteSize = sizeof(uint32_t) * g_vertices.size();
    bufDesc.debugName = "IndexBufferForOMMBaker";
    bufDesc.canHaveRawViews = true;
    bufDesc.canHaveUAVs = false;
    bufDesc.canHaveTypedViews = true;
    bufDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    bufDesc.keepInitialState = true;
    bufDesc.setFormat(nvrhi::Format::R32_UINT);

    indexBufferHandle = nvrhiDevice->createHandleForNativeBuffer(
      nvrhi::ObjectTypes::VK_Buffer,
      indexBuffer,
      bufDesc
    );

    input.texCoordBuffer = uvBufferHandle;
    input.texCoordFormat = nvrhi::Format::R32_FLOAT; // 对应 float2 UV
    input.texCoordStrideInBytes = sizeof(glm::vec2);

    // triangle index buffer
    input.indexBuffer = indexBufferHandle;
    input.numIndices = 6;

    // OMM quality / format
    input.maxSubdivisionLevel = 5;
    input.format = nvrhi::rt::OpacityMicromapFormat::OC1_4_State;

    // misc
    input.dynamicSubdivisionScale = 0.0f;
    input.enableStats = true;
    input.enableSpecialIndices = false;
    input.force32BitIndices = false;
    input.enableTexCoordDeduplication = true;
    input.computeOnly = false;
    input.maxOutOmmArraySize = 0xFFFFFFFF;

    omm::GpuBakeNvrhi::PreDispatchInfo info = {};
    baker.GetPreDispatchInfo(input, info);
    printf("OMM PreDispatch Info:\n");
    printf("ommArrayBufferSize            = %u\n", info.ommArrayBufferSize);
    printf("ommDescArrayHistogramSize     = %u\n", info.ommDescArrayHistogramSize);
    printf("ommDescBufferSize             = %u\n", info.ommDescBufferSize);
    printf("ommIndexBufferSize            = %u\n", info.ommIndexBufferSize);
    printf("ommIndexCount                 = %u\n", info.ommIndexCount);
    printf("ommIndexFormat                = %u\n", info.ommIndexFormat);
    printf("ommIndexHistogramSize         = %u\n", info.ommIndexHistogramSize);
    printf("ommPostDispatchInfoBufferSize = %u\n", info.ommPostDispatchInfoBufferSize);

    // Output is in gpu_omm_output
    auto createRawUavBuffer = [&](size_t byteSize, const char* name)
      {
        nvrhi::BufferDesc desc{};
        desc.byteSize = std::max<size_t>(byteSize, 4);
        desc.debugName = name;
        desc.canHaveRawViews = true;
        desc.canHaveUAVs = true;
        desc.canHaveTypedViews = true;
        desc.initialState = nvrhi::ResourceStates::Common;
        desc.keepInitialState = false;

        return nvrhiDevice->createBuffer(desc);
      };

    nvrhi::BufferHandle& ommArrayBuffer = gpu_omm_output.ommArrayBuffer;
    nvrhi::BufferHandle& ommDescBuffer = gpu_omm_output.ommDescBuffer;
    nvrhi::BufferHandle& ommIndexBuffer = gpu_omm_output.ommIndexBuffer;
    nvrhi::BufferHandle& ommDescArrayHistogramBuffer = gpu_omm_output.ommDescArrayHistogramBuffer;
    nvrhi::BufferHandle& ommIndexHistogramBuffer = gpu_omm_output.ommIndexHistogramBuffer;
    nvrhi::BufferHandle& ommPostDispatchInfoBuffer = gpu_omm_output.ommPostDispatchInfoBuffer;

    ommArrayBuffer = createRawUavBuffer(
      info.ommArrayBufferSize,
      "OMM Array Buffer"
    );

    ommDescBuffer = createRawUavBuffer(
      info.ommDescBufferSize,
      "OMM Desc Buffer"
    );

    ommIndexBuffer = createRawUavBuffer(
      info.ommIndexBufferSize,
      "OMM Index Buffer"
    );

    ommDescArrayHistogramBuffer = createRawUavBuffer(
      info.ommDescArrayHistogramSize,
      "OMM Desc Array Histogram Buffer"
    );

    ommIndexHistogramBuffer = createRawUavBuffer(
      info.ommIndexHistogramSize,
      "OMM Index Histogram Buffer"
    );

    ommPostDispatchInfoBuffer = createRawUavBuffer(
      info.ommPostDispatchInfoBufferSize,
      "OMM Post Dispatch Info Buffer"
    );

    commandList->beginTrackingTextureState(
      alphaTextureNvrhi,
      nvrhi::TextureSubresourceSet(),
      nvrhi::ResourceStates::ShaderResource
    );

    commandList->beginTrackingBufferState(
      uvBufferHandle,
      nvrhi::ResourceStates::ShaderResource
    );

    commandList->beginTrackingBufferState(
      indexBufferHandle,
      nvrhi::ResourceStates::ShaderResource
    );

    commandList->beginTrackingBufferState(ommDescArrayHistogramBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(ommIndexHistogramBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(ommDescBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(ommPostDispatchInfoBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(ommArrayBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(ommIndexBuffer, nvrhi::ResourceStates::Common);

    baker.Dispatch(commandList, input, gpu_omm_output);

    // Copy back
    auto createReadbackBuffer = [&](nvrhi::BufferHandle src, const char* name)
      {
        nvrhi::BufferDesc desc{};
        desc.byteSize = src->getDesc().byteSize;
        desc.debugName = name;
        desc.cpuAccess = nvrhi::CpuAccessMode::Read;
        desc.initialState = nvrhi::ResourceStates::Common;
        desc.keepInitialState = false;

        return nvrhiDevice->createBuffer(desc);
      };

    nvrhi::BufferHandle ommArrayReadback =
      createReadbackBuffer(ommArrayBuffer, "OMM Array Readback");

    nvrhi::BufferHandle ommDescReadback =
      createReadbackBuffer(ommDescBuffer, "OMM Desc Readback");

    nvrhi::BufferHandle ommIndexReadback =
      createReadbackBuffer(ommIndexBuffer, "OMM Index Readback");

    nvrhi::BufferHandle ommDescHistogramReadback =
      createReadbackBuffer(ommDescArrayHistogramBuffer, "OMM Desc Histogram Readback");

    nvrhi::BufferHandle ommIndexHistogramReadback =
      createReadbackBuffer(ommIndexHistogramBuffer, "OMM Index Histogram Readback");

    nvrhi::BufferHandle ommPostInfoReadback =
      createReadbackBuffer(ommPostDispatchInfoBuffer, "OMM Post Info Readback");

    commandList->beginTrackingBufferState(ommArrayReadback, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(ommDescReadback, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(ommIndexReadback, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(ommDescHistogramReadback, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(ommIndexHistogramReadback, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(ommPostInfoReadback, nvrhi::ResourceStates::Common);

    commandList->copyBuffer(ommArrayReadback, 0, ommArrayBuffer, 0, ommArrayBuffer->getDesc().byteSize);
    commandList->copyBuffer(ommDescReadback, 0, ommDescBuffer, 0, ommDescBuffer->getDesc().byteSize);
    commandList->copyBuffer(ommIndexReadback, 0, ommIndexBuffer, 0, ommIndexBuffer->getDesc().byteSize);
    commandList->copyBuffer(ommDescHistogramReadback, 0, ommDescArrayHistogramBuffer, 0, ommDescArrayHistogramBuffer->getDesc().byteSize);
    commandList->copyBuffer(ommIndexHistogramReadback, 0, ommIndexHistogramBuffer, 0, ommIndexHistogramBuffer->getDesc().byteSize);
    commandList->copyBuffer(ommPostInfoReadback, 0, ommPostDispatchInfoBuffer, 0, ommPostDispatchInfoBuffer->getDesc().byteSize);

    commandList->close();

    nvrhiDevice->executeCommandList(commandList);
    nvrhiDevice->waitForIdle();

    printf("Executed OMM buliding commandlist\n");

    // Print out
    auto readBuffer = [&](nvrhi::BufferHandle buffer, size_t size = SIZE_MAX)
      {
        std::vector<uint8_t> data;

        const size_t byteSize =
          (size == SIZE_MAX)
          ? buffer->getDesc().byteSize
          : std::min<size_t>(size, buffer->getDesc().byteSize);

        if (byteSize == 0)
          return data;

        void* mapped = nvrhiDevice->mapBuffer(buffer, nvrhi::CpuAccessMode::Read);
        assert(mapped);

        data.resize(byteSize);
        memcpy(data.data(), mapped, byteSize);

        nvrhiDevice->unmapBuffer(buffer);

        return data;
      };

    std::vector<uint8_t> postInfoData = readBuffer(ommPostInfoReadback);

    omm::GpuBakeNvrhi::PostDispatchInfo postInfo{};
    omm::GpuBakeNvrhi::ReadPostDispatchInfo(
      postInfoData.data(),
      postInfoData.size(),
      postInfo
    );

    printf("OMM PostDispatch Info:\n");
    printf("ommArrayBufferSize       = %u\n", postInfo.ommArrayBufferSize);
    printf("ommDescBufferSize        = %u\n", postInfo.ommDescBufferSize);
    printf("ommTotalOpaqueCount      = %u\n", postInfo.ommTotalOpaqueCount);
    printf("ommTotalTransparentCount = %u\n", postInfo.ommTotalTransparentCount);
    printf("ommTotalUnknownCount     = %u\n", postInfo.ommTotalUnknownCount);

    std::vector<uint8_t> ommArrayData = readBuffer(ommArrayReadback, postInfo.ommArrayBufferSize);
    std::vector<uint8_t> ommDescData = readBuffer(ommDescReadback, postInfo.ommDescBufferSize);
    std::vector<uint8_t> ommIndexData = readBuffer(ommIndexReadback, ommIndexBuffer->getDesc().byteSize);
    std::vector<uint8_t> ommDescHistogramData = readBuffer(ommDescHistogramReadback);
    std::vector<uint8_t> ommIndexHistogramData = readBuffer(ommIndexHistogramReadback);

    // Print first few bytes
    auto dumpBytes = [](const char* name, const std::vector<uint8_t>& data, size_t n = 64)
      {
        printf("%s (%zu bytes):\n", name, data.size());
        for (size_t i = 0; i < std::min(n, data.size()); ++i)
        {
          printf("%02X ", data[i]);
          if ((i + 1) % 16 == 0) printf("\n");
        }
        printf("\n");
      };

    dumpBytes("OMM Array", ommArrayData);
    dumpBytes("OMM Desc", ommDescData);
    dumpBytes("OMM Index", ommIndexData);
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window) && !should_exit) {
      glfwPollEvents();
      drawFrame();
    }
    vkDeviceWaitIdle(device);
  }

  void cleanup() {
    for (VkBuffer& b : asResultBuffers) {
      vkDestroyBuffer(device, b, nullptr);
    }
    for (VkDeviceMemory& m : asResultMemories) {
      vkFreeMemory(device, m, nullptr);
    }
    for (VkBuffer& b : ommBuffersToDelete) {
      vkDestroyBuffer(device, b, nullptr);
    }
    for (VkDeviceMemory& m : ommMemoriesToFree) {
      vkFreeMemory(device, m, nullptr);
    }
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkDestroyBuffer(device, indexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);
    vkFreeMemory(device, indexBufferMemory, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    vkDestroySampler(device, textureSampler, nullptr);
    for (uint32_t i = 0; i < 2; i++) {
      vkFreeMemory(device, textureImageMemory[i], nullptr);
      vkDestroyImageView(device, textureImageView[i], nullptr);
      vkDestroyImage(device, textureImage[i], nullptr);
    }
    for (uint32_t i = 0; i < 128; i++) {
      if (g_diff_maps[i].empty() == false) {
        vkFreeMemory(device, diffMapImageMemory[i], nullptr);
        vkDestroyImageView(device, diffMapImageView[i], nullptr);
        vkDestroyImage(device, diffMapImage[i], nullptr);
      }
      if (g_alpha_maps[i].empty() == false) {
        vkFreeMemory(device, alphaMapImageMemory[i], nullptr);
        vkDestroyImageView(device, alphaMapImageView[i], nullptr);
        vkDestroyImage(device, alphaMapImage[i], nullptr);
      }
    }
    vkFreeMemory(device, sbtMemory, nullptr);
    vkDestroyBuffer(device, sbtBuffer, nullptr);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vkDestroyImage(device, rtOutputImages[i], nullptr);
      vkFreeMemory(device, rtOutputImageMemories[i], nullptr);
      vkDestroyImageView(device, rtOutputImageViews[i], nullptr);
    }
    PFN_vkDestroyAccelerationStructureKHR funcDestroyAccelerationStructureKHR =
      (PFN_vkDestroyAccelerationStructureKHR)vkGetInstanceProcAddr(
        instance, "vkDestroyAccelerationStructureKHR");
    assert(funcDestroyAccelerationStructureKHR);
    funcDestroyAccelerationStructureKHR(device, blas, nullptr);
    funcDestroyAccelerationStructureKHR(device, tlas, nullptr);
    funcDestroyAccelerationStructureKHR(device, blas0, nullptr);
    funcDestroyAccelerationStructureKHR(device, blas1, nullptr);
    vkDestroyBuffer(device, blasResultBuffer0, nullptr);
    vkDestroyBuffer(device, blasResultBuffer1, nullptr);
    vkFreeDescriptorSets(device, rtDescriptorPool, _countof(rtDescriptorSets), rtDescriptorSets);
    vkDestroyDescriptorPool(device, rtDescriptorPool, nullptr);
    vkDestroyPipeline(device, rtPipeline, nullptr);
    vkDestroyPipelineLayout(device, rtPipelineLayout, nullptr);
    vkFreeMemory(device, blasResultMemory, nullptr);
    vkFreeMemory(device, tlasResultMemory, nullptr);
    vkDestroyBuffer(device, blasResultBuffer, nullptr);
    vkDestroyBuffer(device, tlasResultBuffer, nullptr);
    vkDestroyDescriptorSetLayout(device, rtDescriptorSetLayout, nullptr);
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);
    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
    vkDestroyFence(device, inFlightFence, nullptr);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkDestroyCommandPool(device, commandPool, nullptr);
    for (auto fb : swapChainFramebuffers) {
      vkDestroyFramebuffer(device, fb, nullptr);
    }
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    for (auto v : swapChainImageViews) {
      vkDestroyImageView(device, v, nullptr);
    }
    vkDestroySwapchainKHR(device, swapChain, nullptr);
    vkDestroyDevice(device, nullptr);
    if (enableValidationLayers) {
      DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
  }

  void readOBJ() {
    tinyobj::attrib_t attrib;

    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;

    std::filesystem::path infile_dir(g_obj_name);
    if (infile_dir.has_parent_path()) {
      infile_dir = infile_dir.parent_path();
    }
    else {
      infile_dir = ".";
    }
    printf("OBJ model's path: %s\n", infile_dir.c_str());

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, g_obj_name.c_str(), (infile_dir.string() + "\\").c_str());
    if (!err.empty()) {
      printf("Tinyobj error: %s\n", err.c_str());
    }

    if (!ret) {
      return;
    }

    g_vertices.clear();
    unsigned tot_num_verts = 0;
    int tot_num_idxes = 0;
    printf("%s: %zu verts, %zu shapes, %zu materials\n",
      g_obj_name.c_str(), attrib.vertices.size(),
      shapes.size(), materials.size());
    for (unsigned i = 0; i < shapes.size(); i++) {
      printf("shape[%u] has %zu indices, %zu material ids\n",
        i, shapes[i].mesh.indices.size(), shapes[i].mesh.material_ids.size());
      for (uint32_t fidx = 0; fidx < shapes[i].mesh.indices.size() / 3; fidx ++) {
        int mat_id = shapes[i].mesh.material_ids[fidx];
        const tinyobj::material_t* mat{};
        if (mat_id != -1) {
          mat = &(materials[mat_id]);
          printf("  Face %u mat %d, diff=%s, alpha=%s\n", fidx, mat_id,
            mat->diffuse_texname.c_str(), mat->alpha_texname.c_str());
          std::filesystem::path diff_path = infile_dir / mat->diffuse_texname;
          std::filesystem::path mask_path = infile_dir / mat->alpha_texname;
          g_diff_maps.at(mat_id) = diff_path.string();
          g_alpha_maps.at(mat_id) = mask_path.string();
        }
        else {
          printf("  Face %u no mat\n", fidx);
        }

        Vertex v0{}, v1{}, v2{};
        tinyobj::index_t i0 = shapes[i].mesh.indices[fidx * 3];
        tinyobj::index_t i1 = shapes[i].mesh.indices[fidx * 3 + 1];
        tinyobj::index_t i2 = shapes[i].mesh.indices[fidx * 3 + 2];
        v0.pos.x = attrib.vertices[i0.vertex_index * 3];
        v0.pos.y = attrib.vertices[i0.vertex_index * 3 + 1];
        v0.pos.z = attrib.vertices[i0.vertex_index * 3 + 2];
        v0.uv.x = attrib.texcoords[i0.texcoord_index * 2];
        v0.uv.y = attrib.texcoords[i0.texcoord_index * 2 + 1];
        v0.mat_idx = mat_id;
        
        v1.pos.x = attrib.vertices[i1.vertex_index * 3];
        v1.pos.y = attrib.vertices[i1.vertex_index * 3 + 1];
        v1.pos.z = attrib.vertices[i1.vertex_index * 3 + 2];
        v1.uv.x = attrib.texcoords[i1.texcoord_index * 2];
        v1.uv.y = attrib.texcoords[i1.texcoord_index * 2 + 1];
        v1.mat_idx = mat_id;

        v2.pos.x = attrib.vertices[i2.vertex_index * 3];
        v2.pos.y = attrib.vertices[i2.vertex_index * 3 + 1];
        v2.pos.z = attrib.vertices[i2.vertex_index * 3 + 2];
        v2.uv.x = attrib.texcoords[i2.texcoord_index * 2];
        v2.uv.y = attrib.texcoords[i2.texcoord_index * 2 + 1];
        v2.mat_idx = mat_id;
      
        g_vertices.push_back(v0);
        g_vertices.push_back(v1);
        g_vertices.push_back(v2);
      }
    }
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

  bool checkOmmExtensionSupport(VkPhysicalDevice device) {
    return do_checkDeviceExtensionSupport(device, ommExtensions);
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
      if (checkOmmExtensionSupport(device)) {
        printf("Device supports OMM.\n");
        g_use_omm = true;
      }
      else {
        printf("Device does not support OMM.\n");
      }
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
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering{};
    dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRendering.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceSynchronization2Features sync2{};
    sync2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    sync2.synchronization2 = VK_TRUE;
    sync2.pNext = &dynamicRendering;

    VkPhysicalDeviceRayTracingValidationFeaturesNV rtValidationFeatures{};
    rtValidationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV;
    rtValidationFeatures.pNext = &sync2;

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

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = 0;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create render pass");
    }
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
    vertexBindingDesc.stride = sizeof(Vertex);
    vertexBindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertexAttrDesc[3]{};
    vertexAttrDesc[0].binding = 0;
    vertexAttrDesc[0].location = 0;
    vertexAttrDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttrDesc[0].offset = 0;
    vertexAttrDesc[1].binding = 0;
    vertexAttrDesc[1].location = 1;
    vertexAttrDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttrDesc[1].offset = 16;
    vertexAttrDesc[2].binding = 0;
    vertexAttrDesc[2].location = 2;
    vertexAttrDesc[2].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttrDesc[2].offset = 32;

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

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create graphics pipeline");
    }

    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
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

  void createFramebuffers() {
    swapChainFramebuffers.resize(swapChainImages.size());
    for (uint32_t i = 0; i < swapChainImages.size(); i++) {
      VkImageView attachments[] = {
        swapChainImageViews[i]
      };

      VkFramebufferCreateInfo framebufferInfo{};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebufferInfo.renderPass = renderPass;
      framebufferInfo.attachmentCount = 1;
      framebufferInfo.pAttachments = attachments;
      framebufferInfo.width = swapChainExtent.width;
      framebufferInfo.height = swapChainExtent.height;
      framebufferInfo.layers = 1;

      if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create framebuffer");
      }
    }
  }

  void createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);
    VkCommandPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    graphicsQueueFamilyIndex = createInfo.queueFamilyIndex;
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
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = swapChainExtent;
    VkClearValue clearColor = { {{0.3f, 0.3f, 0.3f, 1.0f }} };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

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
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &zero);

    vkCmdDraw(commandBuffer, 6, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    renderImGuiAndEndImGuiForFrame();

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not end command buffer");
    }
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

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
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


  void recordRtCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("Failed to begin command buffer");
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipeline);

    VkDescriptorSet sets[] = {
      rtDescriptorSets[imageIndex],
      rtDescriptorSet1Set
    };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipelineLayout, 0,
      2, sets, 0, nullptr);
    
    PFN_vkCmdTraceRaysKHR funcCmdTraceRaysKHR =
      (PFN_vkCmdTraceRaysKHR)vkGetInstanceProcAddr(
        instance, "vkCmdTraceRaysKHR");
    funcCmdTraceRaysKHR(commandBuffer, &rtRgenRegion, &rtMissRegion, &rtHitRegion, &rtCallRegion, WIDTH, HEIGHT, 1);

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

    renderImGuiAndEndImGuiForFrame();

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not end RT command buffer");
    }
  }

  void startImGuiForFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

  void renderImGuiAndEndImGuiForFrame() {
    ImGui::SetNextWindowSize(ImVec2(360, 320), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(32, 32), ImGuiCond_Once);
    ImGui::Begin("VK OMM Test.");
    ImGui::End();
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
  }

  void drawFrame() {
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);

    startImGuiForFrame();

    uint32_t imageIndex;
    vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    vkResetCommandBuffer(commandBuffer, 0);

    if (!g_is_rt) {
      recordCommandBuffer(commandBuffer, imageIndex);
    }
    else {
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

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create fence");
    }
  }

  void createVertexBuffer() {
    VkBufferCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.size = sizeof(Vertex) * g_vertices.size();
    createInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &createInfo, nullptr, &vertexBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not create vertex buffer");
    }

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(device, vertexBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocInfo.pNext = &allocFlagsInfo;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate memory for vertex buffer");
    }

    vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, createInfo.size, 0, &data);
    memcpy(data, g_vertices.data(), sizeof(Vertex) * g_vertices.size());
    vkUnmapMemory(device, vertexBufferMemory);

    // Index Buffer
    createInfo.size = sizeof(uint32_t) * g_vertices.size();
    if (vkCreateBuffer(device, &createInfo, nullptr, &indexBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not create index buffer");
    }
    vkGetBufferMemoryRequirements(device, indexBuffer, &memReq);
    allocInfo.allocationSize = memReq.size;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &indexBufferMemory) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate memory for index buffer");
    }
    vkBindBufferMemory(device, indexBuffer, indexBufferMemory, 0);
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < g_vertices.size(); i++) {
      indices.push_back(i);
    }
    vkMapMemory(device, indexBufferMemory, 0, createInfo.size, 0, &data);
    memcpy(data, indices.data(), sizeof(uint32_t) * indices.size());
    vkUnmapMemory(device, indexBufferMemory);
  }

  void createUVBuffer() {
    VkBufferCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.size = sizeof(glm::vec2) * g_vertices.size();
    createInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    ;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &createInfo, nullptr, &uvBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not create UV buffer");
    }

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(device, uvBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocInfo.pNext = &allocFlagsInfo;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &uvBufferMemory) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate memory for vertex buffer");
    }
    vkBindBufferMemory(device, uvBuffer, uvBufferMemory, 0);

    std::vector<glm::vec2> uvs;
    for (uint32_t i = 0; i < g_vertices.size(); i++) {
      uvs.push_back(g_vertices[i].uv);
    }

    void* data;
    vkMapMemory(device, uvBufferMemory, 0, createInfo.size, 0, &data);
    memcpy(data, uvs.data(), sizeof(glm::vec2) * uvs.size());
    vkUnmapMemory(device, uvBufferMemory);
  }

  // Quick test: 1 idx per BLAS
  VkAccelerationStructureKHR buildBLAS(uint32_t prim_idx, VkBuffer& outBlasResultBuffer, VkDeviceMemory& outBlasResultMemory) {
    VkDeviceAddress vbDeviceAddr = getBufferDeviceAddress(vertexBuffer);
    VkDeviceAddress ibDeviceAddr = getBufferDeviceAddress(indexBuffer);

    VkAccelerationStructureGeometryTrianglesDataKHR triASData{};
    triASData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triASData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triASData.vertexData.deviceAddress = vbDeviceAddr;
    triASData.vertexStride = sizeof(Vertex);
    triASData.indexType = VK_INDEX_TYPE_UINT32;
    triASData.indexData.deviceAddress = ibDeviceAddr + prim_idx * sizeof(uint32_t) * 3;
    triASData.maxVertex = 1;

    // OMM-related functionalities go here
    VkAccelerationStructureTrianglesOpacityMicromapEXT ommBlasDesc{};
    VkMicromapEXT ommArray{};
    if (g_use_omm) {
      // 1. Baked OMM result
      const omm::Cpu::BakeResultDesc* res_desc = bakeOmmForMask(prim_idx, prim_idx == 0 ? 3 : 5);

      // 2. OMM itself
      // FillMicromapBuildInfo
      VkMicromapBuildInfoEXT buildDesc = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT };
      buildDesc.pNext = nullptr;
      buildDesc.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
      buildDesc.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
      buildDesc.dstMicromap = NULL;
      buildDesc.usageCountsCount = res_desc->descArrayHistogramCount;
      assert(res_desc->descArrayHistogramCount == 1);
      VkMicromapUsageEXT usage{};
      auto usage0 = res_desc->descArrayHistogram[0];
      usage.count = usage0.count;
      usage.format = usage0.format;
      usage.subdivisionLevel = usage0.subdivisionLevel;
      buildDesc.pUsageCounts = &usage;
      buildDesc.data.deviceAddress = NULL;
      buildDesc.scratchData.deviceAddress = NULL;
      buildDesc.triangleArray.deviceAddress = NULL;
      buildDesc.triangleArrayStride = sizeof(VkMicromapTriangleEXT);

      VkMicromapBuildSizesInfoEXT preBuildInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT };
      PFN_vkGetMicromapBuildSizesEXT funcGetMicromapBuildSizes =
        (PFN_vkGetMicromapBuildSizesEXT)vkGetInstanceProcAddr(
          instance, "vkGetMicromapBuildSizesEXT");
      funcGetMicromapBuildSizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildDesc, &preBuildInfo);
      printf("OMM build size: scratch=%u, as=%u\n", preBuildInfo.buildScratchSize, preBuildInfo.micromapSize);

      // BindOmmToMemoryVK
      VkBuffer ommBuffer{};
      VkDeviceMemory ommMemory{};
      createBuffer(preBuildInfo.micromapSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
        | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT,
        0,
        ommBuffer, ommMemory);

      VkBuffer ommScratchBuffer{};
      VkDeviceMemory ommScratchMemory{};
      createBuffer(preBuildInfo.buildScratchSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
        | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT,
        0,
        ommScratchBuffer, ommScratchMemory);

      VkMicromapCreateInfoEXT ommDesc = { VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT };
      ommDesc.pNext = nullptr;
      ommDesc.createFlags = 0;
      ommDesc.buffer = ommBuffer;
      ommDesc.offset = 0;
      ommDesc.size = preBuildInfo.micromapSize;
      ommDesc.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
      ommDesc.deviceAddress = 0;
      auto func_vkCreateMicromap = (PFN_vkCreateMicromapEXT)vkGetInstanceProcAddr(
        instance, "vkCreateMicromapEXT");
      if (func_vkCreateMicromap(device, &ommDesc, nullptr, &ommArray) != VK_SUCCESS) {
        printf("Failed to create VkMicromap\n");
      }

      // OMM Array Data
      VkBuffer ommArrayBuffer{};
      VkDeviceMemory ommArrayMemory{};
      createBuffer(res_desc->arrayDataSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
        | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        ommArrayBuffer, ommArrayMemory);
      void* data{};
      vkMapMemory(device, ommArrayMemory, 0, res_desc->arrayDataSize, 0, &data);
      memcpy(data, res_desc->arrayData, res_desc->arrayDataSize);
      vkUnmapMemory(device, ommArrayMemory);

      // OMM descriptor array
      VkBuffer ommDescArrayBuffer{};
      VkDeviceMemory ommDescArrayMemory{};
      static_assert(sizeof(VkMicromapTriangleEXT) == sizeof(omm::Cpu::OpacityMicromapDesc));
      size_t size = res_desc->descArrayCount * sizeof(VkMicromapTriangleEXT);
      createBuffer(size,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
        | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        ommDescArrayBuffer, ommDescArrayMemory);
      vkMapMemory(device, ommDescArrayMemory, 0, size, 0, &data);
      memcpy(data, res_desc->descArray, size);
      vkUnmapMemory(device, ommDescArrayMemory);

      // FillMicromapBuildInfo for real
      buildDesc.pNext = nullptr;
      buildDesc.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
      buildDesc.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
      buildDesc.dstMicromap = ommArray;
      buildDesc.usageCountsCount = res_desc->descArrayHistogramCount;
      buildDesc.pUsageCounts = &usage;
      buildDesc.data.deviceAddress = getBufferDeviceAddress(ommArrayBuffer);
      buildDesc.scratchData.deviceAddress = getBufferDeviceAddress(ommScratchBuffer);
      buildDesc.triangleArray.deviceAddress = getBufferDeviceAddress(ommDescArrayBuffer);
      buildDesc.triangleArrayStride = sizeof(VkMicromapTriangleEXT);

      VkCommandBuffer commandBuffer = beginSingleTimeCommands();
      PFN_vkCmdBuildMicromapsEXT funcCmdBuildMicromaps =
        (PFN_vkCmdBuildMicromapsEXT)vkGetInstanceProcAddr(
          instance, "vkCmdBuildMicromapsEXT");
      funcCmdBuildMicromaps(commandBuffer, 1, &buildDesc);
      VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      barrier.srcQueueFamilyIndex = (~0U);
      barrier.dstQueueFamilyIndex = (~0U);
      barrier.buffer = ommScratchBuffer;
      barrier.offset = 0;
      barrier.size = preBuildInfo.buildScratchSize;
      vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 1, &barrier, 0, nullptr);
      endSingleTimeCommands(commandBuffer);

      // 3. OMM BLAS Info for BLAS build
      VkBuffer ommIndexBuffer{};
      VkDeviceMemory ommIndexMemory{};
      assert(res_desc->indexFormat == omm::IndexFormat::UINT_32);
      size = sizeof(uint32_t) * res_desc->indexCount;
      createBuffer(size,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
        | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        ommIndexBuffer, ommIndexMemory);
      vkMapMemory(device, ommIndexMemory, 0, size, 0, &data);
      memcpy(data, res_desc->indexBuffer, size);
      vkUnmapMemory(device, ommIndexMemory);

      // FillOmmTrianglesDesc
      ommBlasDesc.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT;
      ommBlasDesc.pNext = nullptr;
      ommBlasDesc.indexType = VK_INDEX_TYPE_UINT32;
      ommBlasDesc.indexBuffer.deviceAddress = getBufferDeviceAddress(ommIndexBuffer);
      ommBlasDesc.indexStride = sizeof(uint32_t);
      ommBlasDesc.baseTriangle = 0;
      ommBlasDesc.usageCountsCount = res_desc->indexHistogramCount;
      VkMicromapUsageEXT ih0{};  // ih = index histogram
      assert(res_desc->indexHistogramCount == 1);
      auto u0 = res_desc->indexHistogram[0];
      ih0.count = u0.count;
      ih0.format = u0.format;
      ommBlasDesc.pUsageCounts = &ih0;
      ommBlasDesc.micromap = ommArray;

      triASData.pNext = &ommBlasDesc;

      vkDestroyBuffer(device, ommScratchBuffer, nullptr);
      vkFreeMemory(device, ommScratchMemory, nullptr);

      ommBuffersToDelete.push_back(ommBuffer);
      ommBuffersToDelete.push_back(ommArrayBuffer);
      ommBuffersToDelete.push_back(ommDescArrayBuffer);
      ommBuffersToDelete.push_back(ommIndexBuffer);
      ommMemoriesToFree.push_back(ommMemory);
      ommMemoriesToFree.push_back(ommArrayMemory);
      ommMemoriesToFree.push_back(ommDescArrayMemory);
      ommMemoriesToFree.push_back(ommIndexMemory);
    }

    VkAccelerationStructureGeometryKHR geomData{};
    geomData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geomData.flags = 0;
    geomData.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geomData.geometry.triangles = triASData;


    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.primitiveCount = 1;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.transformOffset = 0;

    VkAccelerationStructureBuildGeometryInfoKHR buildGeomInfo{};
    buildGeomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildGeomInfo.flags = 0;
    buildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildGeomInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    buildGeomInfo.dstAccelerationStructure = VK_NULL_HANDLE;
    buildGeomInfo.geometryCount = 1;
    buildGeomInfo.pGeometries = &geomData;
    buildGeomInfo.ppGeometries = nullptr;
    buildGeomInfo.scratchData.deviceAddress = 0;

    uint32_t primCount = 1;
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
    printf("BLAS build size: scratch=%u, AS=%u\n",
      asBuildSizeInfo.buildScratchSize,
      asBuildSizeInfo.accelerationStructureSize);

    VkBuffer blasScratchBuffer{};
    VkDeviceMemory blasScratchMemory{};
    createBuffer(asBuildSizeInfo.buildScratchSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
      0,
      blasScratchBuffer,
      blasScratchMemory);

    VkBuffer blasResultBuffer{};
    VkDeviceMemory blasResultMemory{};
    createBuffer(asBuildSizeInfo.accelerationStructureSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
      0,
      blasResultBuffer,
      blasResultMemory);

    buildGeomInfo.scratchData.deviceAddress = getBufferDeviceAddress(blasScratchBuffer);

    VkAccelerationStructureKHR blas{};
    VkAccelerationStructureCreateInfoKHR blasCreateInfo{};
    blasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    blasCreateInfo.createFlags = 0;
    blasCreateInfo.size = asBuildSizeInfo.accelerationStructureSize;
    blasCreateInfo.buffer = blasResultBuffer;
    blasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    PFN_vkCreateAccelerationStructureKHR funcCreateAccelerationStructure =
      (PFN_vkCreateAccelerationStructureKHR)vkGetInstanceProcAddr(
        instance, "vkCreateAccelerationStructureKHR");
    if (funcCreateAccelerationStructure(device, &blasCreateInfo, nullptr, &blas) != VK_SUCCESS) {
      throw std::runtime_error("Could not create BLAS");
    }
    buildGeomInfo.dstAccelerationStructure = blas;

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
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

    outBlasResultBuffer = blasResultBuffer;
    outBlasResultMemory = blasResultMemory;

    vkDestroyBuffer(device, blasScratchBuffer, nullptr);
    vkFreeMemory(device, blasScratchMemory, nullptr);

    //asResultBuffers.push_back(outBlasResultBuffer);
    //asResultMemories.push_back(blasResultMemory);

    return blas;
  }

  void createAS() {
    blas0 = buildBLAS(0, blasResultBuffer0, blasResultMemory0);
    blas1 = buildBLAS(1, blasResultBuffer1, blasResultMemory1);

    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.pNext = nullptr;

    // TLAS
    VkBuffer tlasInstancesBuffer{};
    VkDeviceMemory tlasInstancesMemory{};

    size_t size = sizeof(VkAccelerationStructureInstanceKHR) * 2;
    createBuffer(size,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      tlasInstancesBuffer, tlasInstancesMemory);

    VkDeviceAddress blas0ResultDeviceAddr = getBufferDeviceAddress(blasResultBuffer0);

    VkDeviceAddress blas0ASAddress{}, blas1ASAddress;
    VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo{};
    blasAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    blasAddrInfo.accelerationStructure = blas0;
    PFN_vkGetAccelerationStructureDeviceAddressKHR funcGetAccelerationStructureDeviceAddressKHR =
      (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetInstanceProcAddr(
        instance, "vkGetAccelerationStructureDeviceAddressKHR");
    blas0ASAddress = funcGetAccelerationStructureDeviceAddressKHR(device, &blasAddrInfo);
    blasAddrInfo.accelerationStructure = blas1;
    blas1ASAddress = funcGetAccelerationStructureDeviceAddressKHR(device, &blasAddrInfo);

    void* data;
    VkAccelerationStructureInstanceKHR instances[2]{};
    instances[0].accelerationStructureReference = blas0ASAddress;
    instances[0].transform.matrix[0][0] = 1.0f;
    instances[0].transform.matrix[1][1] = 1.0f;
    instances[0].transform.matrix[2][2] = 1.0f;
    instances[0].instanceCustomIndex = 0;
    instances[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instances[0].mask = 0xFF;
    instances[0].instanceShaderBindingTableRecordOffset = 0;
    instances[1].accelerationStructureReference = blas1ASAddress;
    instances[1].transform.matrix[0][0] = 1.0f;
    instances[1].transform.matrix[1][1] = 1.0f;
    instances[1].transform.matrix[2][2] = 1.0f;
    instances[1].instanceCustomIndex = 0;
    instances[1].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instances[1].mask = 0xFF;
    instances[1].instanceShaderBindingTableRecordOffset = 0;

    vkMapMemory(device, tlasInstancesMemory, 0, size, 0, &data);
    memcpy(data, &instances[0], size);
    vkUnmapMemory(device, tlasInstancesMemory);

    VkAccelerationStructureGeometryKHR instData[2]{};
    instData[0].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    instData[0].flags = 0;// VK_GEOMETRY_OPAQUE_BIT_KHR;
    instData[0].geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    instData[0].geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instData[0].geometry.instances.data.deviceAddress = getBufferDeviceAddress(tlasInstancesBuffer);
    instData[1].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    instData[1].flags = 0;// VK_GEOMETRY_OPAQUE_BIT_KHR;
    instData[1].geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    instData[1].geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instData[1].geometry.instances.data.deviceAddress = getBufferDeviceAddress(tlasInstancesBuffer) + sizeof(VkAccelerationStructureInstanceKHR);

    // TLAS inst info
    VkAccelerationStructureBuildGeometryInfoKHR buildInstInfo{};
    buildInstInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInstInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInstInfo.flags = 0;
    buildInstInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInstInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    buildInstInfo.dstAccelerationStructure = VK_NULL_HANDLE;
    buildInstInfo.geometryCount = 1;
    buildInstInfo.pGeometries = instData;
    buildInstInfo.ppGeometries = nullptr;
    buildInstInfo.scratchData.deviceAddress = 0;

    uint32_t instCount = 1;
    VkAccelerationStructureBuildSizesInfoKHR tlasBuildSizeInfo{};
    tlasBuildSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR funcGetAccelerationStructureBuildSizes =
      (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetInstanceProcAddr(
        instance, "vkGetAccelerationStructureBuildSizesKHR");
    funcGetAccelerationStructureBuildSizes(device,
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &buildInstInfo,
      &instCount,
      &tlasBuildSizeInfo);
    printf("TLAS build size: scratch=%llu, AS=%llu\n",
      tlasBuildSizeInfo.buildScratchSize,
      tlasBuildSizeInfo.accelerationStructureSize);

    // TLAS scratch
    VkBuffer tlasScratchBuffer;
    VkDeviceMemory tlasScratchMemory;
    createBuffer(tlasBuildSizeInfo.buildScratchSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
      0,
      tlasScratchBuffer, tlasScratchMemory);

    // TLAS result
    createBuffer(tlasBuildSizeInfo.accelerationStructureSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
      0,
      tlasResultBuffer, tlasResultMemory);

    VkAccelerationStructureCreateInfoKHR tlasCreateInfo{};
    tlasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    tlasCreateInfo.size = tlasBuildSizeInfo.accelerationStructureSize;
    tlasCreateInfo.buffer = tlasResultBuffer;
    tlasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    PFN_vkCreateAccelerationStructureKHR funcCreateAccelerationStructure =
      (PFN_vkCreateAccelerationStructureKHR)vkGetInstanceProcAddr(
        instance, "vkCreateAccelerationStructureKHR");
    if (funcCreateAccelerationStructure(device, &tlasCreateInfo, nullptr, &tlas) != VK_SUCCESS) {
      throw std::runtime_error("Could not create TLAS");
    }

    // Prepare cmd list
    

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.primitiveCount = 2;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.transformOffset = 0;
    buildInstInfo.scratchData.deviceAddress = getBufferDeviceAddress(tlasScratchBuffer);
    buildInstInfo.dstAccelerationStructure = tlas;
    VkAccelerationStructureBuildRangeInfoKHR* const buildRangeInfos[] = { &buildRangeInfo };

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    PFN_vkCmdBuildAccelerationStructuresKHR funcCmdBuildAccelerationStructuresKHR =
      (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetInstanceProcAddr(
        instance, "vkCmdBuildAccelerationStructuresKHR");
    funcCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInstInfo, buildRangeInfos);

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

    vkFreeMemory(device, tlasInstancesMemory, nullptr);
    vkFreeMemory(device, tlasScratchMemory, nullptr);
    vkDestroyBuffer(device, tlasInstancesBuffer, nullptr);
    vkDestroyBuffer(device, tlasScratchBuffer, nullptr);
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

  void createRtOutputImage() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageInfo.extent.width = WIDTH;
    imageInfo.extent.height = HEIGHT;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
      VK_IMAGE_USAGE_STORAGE_BIT;
    QueueFamilyIndices qfi = findQueueFamilies(physicalDevice);
    imageInfo.queueFamilyIndexCount = 0;
    imageInfo.pQueueFamilyIndices = nullptr;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      if (vkCreateImage(device, &imageInfo, nullptr, &(rtOutputImages[i])) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create RT output image");
      }
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device, rtOutputImages[0], &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocInfo.pNext = &allocFlagsInfo;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      if (vkAllocateMemory(device, &allocInfo, nullptr, &(rtOutputImageMemories[i])) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate memory for RT output image");
      }
      vkBindImageMemory(device, rtOutputImages[i], rtOutputImageMemories[i], 0);
    }

    VkImageViewCreateInfo imageViewInfo{};
    imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewInfo.format = imageInfo.format;
    imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewInfo.subresourceRange.baseMipLevel = 0;
    imageViewInfo.subresourceRange.levelCount = 1;
    imageViewInfo.subresourceRange.baseArrayLayer = 0;
    imageViewInfo.subresourceRange.layerCount = 1;
    imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      imageViewInfo.image = rtOutputImages[i];
      if (vkCreateImageView(device, &imageViewInfo, nullptr, &(rtOutputImageViews[i])) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create output image view");
      }
    }

    // Format change Cmd List
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);

    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("Failed to begin command buffer");
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      transitionImageLayout(rtOutputImages[i],
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    }

    vkEndCommandBuffer(commandBuffer);

    // Submit CMD List
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.pWaitSemaphores = nullptr;
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR };
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = nullptr;
    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence) != VK_SUCCESS) {
      throw std::runtime_error("Failed to submit image barriers to Q");
    }

    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
  }

  void createRtDescriptorSetLayout() {
    // Everything except set2
    VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[5]{};
    descriptorSetLayoutBindings[0].binding = 0;
    descriptorSetLayoutBindings[0].descriptorCount = 1;
    descriptorSetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    descriptorSetLayoutBindings[0].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    descriptorSetLayoutBindings[1].binding = 1;
    descriptorSetLayoutBindings[1].descriptorCount = 1;
    descriptorSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorSetLayoutBindings[1].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    descriptorSetLayoutBindings[2].binding = 2;
    descriptorSetLayoutBindings[2].descriptorCount = 1;
    descriptorSetLayoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorSetLayoutBindings[2].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[2].stageFlags =
      VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    descriptorSetLayoutBindings[3].binding = 3;
    descriptorSetLayoutBindings[3].descriptorCount = 1;
    descriptorSetLayoutBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorSetLayoutBindings[3].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[3].stageFlags =
      VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    descriptorSetLayoutBindings[4].binding = 4;
    descriptorSetLayoutBindings[4].descriptorCount = 1;
    descriptorSetLayoutBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBindings[4].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[4].stageFlags =
      VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = _countof(descriptorSetLayoutBindings);
    descriptorSetLayoutInfo.pBindings = descriptorSetLayoutBindings;
    if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, nullptr, &rtDescriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create RT descriptor set layout");
    }

    VkDescriptorSetLayoutBinding textureBinding{};
    textureBinding.binding = 0;
    textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureBinding.descriptorCount = 128;
    textureBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    descriptorSetLayoutInfo.bindingCount = 1;
    descriptorSetLayoutInfo.pBindings = &textureBinding;
    if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, nullptr, &rtDescriptorSet1Layout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create RT descriptor layout for Set#2");
    }
  }

  void createRtDescriptorPool() {
    VkDescriptorPoolSize poolSizes[5]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[3].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[4].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = _countof(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &rtDescriptorPool) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create RT descriptor pool");
    }

    // Set2
    VkDescriptorPoolSize poolSizeSet2 = {};
    poolSizeSet2.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizeSet2.descriptorCount = 128;
    
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSizeSet2;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &rtDescriptorSet1Pool) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create RT Set1 descriptor pool");
    }
  }

  void createRtDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = rtDescriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, rtDescriptorSetLayout);
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &allocInfo, rtDescriptorSets) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate RT descriptor sets");
    }

    // Update TLAS to descriptor set
    VkWriteDescriptorSetAccelerationStructureKHR writeDescAS{};
    writeDescAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    writeDescAS.accelerationStructureCount = 1;
    writeDescAS.pAccelerationStructures = &tlas;

    VkWriteDescriptorSet writeDesc[5 * MAX_FRAMES_IN_FLIGHT]{};
    writeDesc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDesc[0].dstSet = rtDescriptorSets[0];
    writeDesc[0].dstBinding = 0;
    writeDesc[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writeDesc[0].descriptorCount = 1;
    writeDesc[0].pNext = &writeDescAS;
    for (uint32_t i = 1; i < MAX_FRAMES_IN_FLIGHT; i++) {
      writeDesc[i] = writeDesc[0];
      writeDesc[i].dstSet = rtDescriptorSets[i];
    }

    // Update image view to descriptor set
    VkDescriptorImageInfo imageInfo[MAX_FRAMES_IN_FLIGHT]{};
    imageInfo[0].imageView = rtOutputImageViews[0];
    imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo[1].imageView = rtOutputImageViews[1];
    imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo[2].imageView = rtOutputImageViews[2];
    imageInfo[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    // Update material texture
    VkDescriptorImageInfo texImageInfo{};
    texImageInfo.imageView = diffMapImageView[0];
    texImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    texImageInfo.sampler = textureSampler;

    VkDescriptorImageInfo maskImageInfo{};
    maskImageInfo.imageView = alphaMapImageView[0];
    maskImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    maskImageInfo.sampler = textureSampler;

    VkDescriptorBufferInfo bi{};
    bi.buffer = vertexBuffer;
    bi.offset = 0;
    bi.range = sizeof(Vertex) * g_vertices.size();

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      uint32_t idx = i + MAX_FRAMES_IN_FLIGHT;
      writeDesc[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDesc[idx].dstSet = rtDescriptorSets[i];
      writeDesc[idx].dstBinding = 1;
      writeDesc[idx].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      writeDesc[idx].descriptorCount = 1;
      writeDesc[idx].pImageInfo = &(imageInfo[i]);

      idx = i + MAX_FRAMES_IN_FLIGHT * 2;
      writeDesc[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDesc[idx].dstSet = rtDescriptorSets[i];
      writeDesc[idx].dstBinding = 2;
      writeDesc[idx].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writeDesc[idx].descriptorCount = 1;
      writeDesc[idx].pImageInfo = &(texImageInfo);

      idx = i + MAX_FRAMES_IN_FLIGHT * 3;
      writeDesc[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDesc[idx].dstSet = rtDescriptorSets[i];
      writeDesc[idx].dstBinding = 3;
      writeDesc[idx].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writeDesc[idx].descriptorCount = 1;
      writeDesc[idx].pImageInfo = &(maskImageInfo);

      idx = i + MAX_FRAMES_IN_FLIGHT * 4;
      writeDesc[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDesc[idx].dstSet = rtDescriptorSets[i];
      writeDesc[idx].dstBinding = 4;
      writeDesc[idx].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writeDesc[idx].descriptorCount = 1;
      writeDesc[idx].pBufferInfo = &bi;
    }

    vkUpdateDescriptorSets(device, _countof(writeDesc), writeDesc, 0, nullptr);

    // Allocate Set2 descriptor set
    allocInfo.descriptorPool = rtDescriptorSet1Pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &rtDescriptorSet1Layout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &rtDescriptorSet1Set) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate RT descriptor Set1 set");
    }

    // Update Set2 descriptor set
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = rtDescriptorSet1Set;
    write.dstBinding = 0;
    write.descriptorCount = 128;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    VkDescriptorImageInfo imageInfos[128]{};
    imageInfos[0].imageView = diffMapImageView[0];
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].sampler = textureSampler;

    imageInfos[1].imageView = alphaMapImageView[0];
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].sampler = textureSampler;

    for (int i = 2; i < 128; i++) {
      imageInfos[i] = imageInfos[0];
    }
    write.pImageInfo = imageInfos;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
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

  VkDeviceAddress getBufferDeviceAddress(VkBuffer& buf) {
    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.buffer = buf;
    addrInfo.pNext = nullptr;
    return vkGetBufferDeviceAddress(device, &addrInfo);
  }

  void createRtPipeline() {
    // Layout
    VkPipelineLayoutCreateInfo rtPipelineLayoutInfo{};
    rtPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    rtPipelineLayoutInfo.setLayoutCount = 2;
    const VkDescriptorSetLayout rtDsLayouts[] = {
      rtDescriptorSetLayout,
      rtDescriptorSet1Layout
    };
    rtPipelineLayoutInfo.pSetLayouts = &(rtDsLayouts[0]);
    rtPipelineLayoutInfo.pushConstantRangeCount = 0;
    rtPipelineLayoutInfo.pPushConstantRanges = nullptr;
    if (vkCreatePipelineLayout(device, &rtPipelineLayoutInfo, nullptr, &rtPipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("Could not create rt pipeline layout");
    }

    VkPipelineShaderStageCreateInfo stages[4]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].pName = "main";
    stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[2].pName = "main";
    stages[3].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[3].pName = "main";

    // RayGen
    std::vector<char> raygenShaderCode = readFile("shaders/rgen.spv");
    VkShaderModule raygenShaderModule = createShaderModule(raygenShaderCode);
    stages[0].module = raygenShaderModule;
    stages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    std::vector<char> rchitShaderCode = readFile("shaders/rchit.spv");
    VkShaderModule rchitShaderModule = createShaderModule(rchitShaderCode);
    stages[1].module = rchitShaderModule;
    stages[1].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    std::vector<char> rmissShaderCode = readFile("shaders/rmiss.spv");
    VkShaderModule rmissShaderModule = createShaderModule(rmissShaderCode);
    stages[2].module = rmissShaderModule;
    stages[2].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    std::vector<char> rahitShaderCode = readFile("shaders/rahit.spv");
    VkShaderModule rahitShaderModule = createShaderModule(rahitShaderCode);
    stages[3].module = rahitShaderModule;
    stages[3].stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;

    // Shader Group / HitGroup?
    VkRayTracingShaderGroupCreateInfoKHR shaderGroupInfos[3]{};
    shaderGroupInfos[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroupInfos[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroupInfos[0].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[0].closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[0].generalShader = 0;  // Raygen
    shaderGroupInfos[0].intersectionShader = VK_SHADER_UNUSED_KHR;

    shaderGroupInfos[1] = shaderGroupInfos[0];
    shaderGroupInfos[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    shaderGroupInfos[1].generalShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[1].closestHitShader = 1;
    shaderGroupInfos[1].anyHitShader = 3;

    shaderGroupInfos[2] = shaderGroupInfos[1];
    shaderGroupInfos[2].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupInfos[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroupInfos[2].generalShader = 2;  // Miss
    shaderGroupInfos[2].closestHitShader = VK_SHADER_UNUSED_KHR;

    VkRayTracingPipelineCreateInfoKHR rtPipelineCreateInfo{};
    rtPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rtPipelineCreateInfo.flags = 0;
    rtPipelineCreateInfo.stageCount = _countof(stages);
    rtPipelineCreateInfo.pStages = stages;
    rtPipelineCreateInfo.groupCount = _countof(shaderGroupInfos);
    rtPipelineCreateInfo.pGroups = shaderGroupInfos;
    rtPipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
    rtPipelineCreateInfo.pLibraryInfo = nullptr;
    rtPipelineCreateInfo.pLibraryInterface = nullptr;
    rtPipelineCreateInfo.pDynamicState = nullptr;
    rtPipelineCreateInfo.layout = rtPipelineLayout;

    PFN_vkCreateRayTracingPipelinesKHR funcCreateRayTracingPipelines =
      (PFN_vkCreateRayTracingPipelinesKHR)vkGetInstanceProcAddr(
        instance, "vkCreateRayTracingPipelinesKHR");
    assert(funcCreateRayTracingPipelines);
    if (funcCreateRayTracingPipelines(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rtPipelineCreateInfo, nullptr, &rtPipeline) != VK_SUCCESS) {
      throw std::runtime_error("Could not create RT pipeline");
    }

    vkDestroyShaderModule(device, raygenShaderModule, nullptr);
    vkDestroyShaderModule(device, rmissShaderModule, nullptr);
    vkDestroyShaderModule(device, rchitShaderModule, nullptr);
    vkDestroyShaderModule(device, rahitShaderModule, nullptr);
  }

  void createRtSBT() {
    const size_t sbtSize = 32;  // arbitrarily chosen
    const size_t sbtAlignment = 64;
    const size_t numSBTs = 3;   // Rgen, closest-hit, miss

    VkBufferCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.size = sbtAlignment * numSBTs;
    createInfo.usage =
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
      | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(device, &createInfo, nullptr, &sbtBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create SBT buffer");
    }

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(device, sbtBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = std::max(memReq.size, sbtSize);
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocInfo.pNext = &allocFlagsInfo;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &sbtMemory) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate memory for SBT");
    }
    vkBindBufferMemory(device, sbtBuffer, sbtMemory, 0);

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
    memcpy(mapped + sbtAlignment, shaderGroupHandle + sbtSize, sbtSize);  // hit
    memcpy(mapped + sbtAlignment * 2, shaderGroupHandle + sbtSize * 2, sbtSize);  // miss
    vkUnmapMemory(device, sbtMemory);

    // Stolen from ChatGPT
    rtRgenRegion.deviceAddress = sbtDeviceAddress;
    rtRgenRegion.size = sbtSize;
    rtRgenRegion.stride = sbtSize;

    rtHitRegion.deviceAddress = sbtDeviceAddress + sbtAlignment;
    rtHitRegion.size = sbtSize;
    rtHitRegion.stride = sbtSize;

    rtMissRegion.deviceAddress = sbtDeviceAddress + sbtAlignment * 2;
    rtMissRegion.size = sbtSize;
    rtMissRegion.stride = sbtSize;
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

  void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
  }

  void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = 0;
    region.imageOffset.y = 0;
    region.imageOffset.z = 0;
    region.imageExtent.width = width;
    region.imageExtent.height = height;
    region.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    endSingleTimeCommands(commandBuffer);
  }

  void createTextureImage() {
    for (uint32_t ty = 0; ty < 2; ty++) {
      std::vector<std::string>* file_names_list[] = { &g_diff_maps, &g_alpha_maps };
      std::vector<std::string>* file_names = file_names_list[ty];
      const char* types[] = { "diffuse", "alpha" };
      for (uint32_t i = 0; i < file_names->size(); i++) {
        VkImage* img = (ty == 0) ? &(diffMapImage[i]) : &(alphaMapImage[i]);
        VkImageView* image_view = (ty == 0) ? &(diffMapImageView[i]) : &(alphaMapImageView[i]);
        VkDeviceMemory* image_memory = (ty == 0) ? &(diffMapImageMemory[i]) : &(alphaMapImageMemory[i]);
        std::string fn = file_names->at(i);
        if (fn.empty()) continue;

        printf("Creating tex of type %s [%u], file name %s\n",
          types[ty], i, fn.c_str());

        int texHeight, texWidth, texChannels;
        if (fn.empty()) continue;
        stbi_uc* pixels = stbi_load(fn.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        printf("%s: width=%d height=%d channels=%d\n",
          fn.c_str(), texWidth, texHeight, texChannels);
        VkDeviceSize imageSize = texHeight * texWidth * 4;

        VkBuffer stagingBuffer{};
        VkDeviceMemory stagingBufferMemory;

        VkBufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        createInfo.size = imageSize;
        createInfo.usage =
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (vkCreateBuffer(device, &createInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
          throw std::runtime_error("Failed to create texture staging buffer");
        }

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, stagingBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = std::max(memReq.size, imageSize);
        allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
          | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
          throw std::runtime_error("Failed to allocate memory for staging buffer");
        }

        void* mapped{};
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &mapped);
        memcpy(mapped, pixels, imageSize);
        vkUnmapMemory(device, stagingBufferMemory);
        vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);

        stbi_image_free(pixels);

        createImage(texWidth, texHeight,
          VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          *img, *image_memory);

        transitionImageLayout(*img, VK_FORMAT_R8G8B8A8_SRGB,
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, *img, texWidth, texHeight);
        transitionImageLayout(*img, VK_FORMAT_R8G8B8A8_SRGB,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
      }
    }
  }

  VkImageView createImageView(VkImage image, VkFormat format) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
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

  void createTextureImageView() {
    for (uint32_t i = 0; i < 128; i++) {
      if (g_diff_maps[i].empty() == false) {
        diffMapImageView[i] = createImageView(diffMapImage[i], VK_FORMAT_R8G8B8A8_SRGB);
      }
      if (g_alpha_maps[i].empty() == false) {
        alphaMapImageView[i] = createImageView(alphaMapImage[i], VK_FORMAT_R8G8B8A8_SRGB);
      }
    }
  }

  void createTextureSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
      throw std::runtime_error("failed to create texture sampler!");
    }
  }

  void createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[3]{};
    descriptorSetLayoutBindings[0].binding = 0;
    descriptorSetLayoutBindings[0].descriptorCount = 1;
    descriptorSetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorSetLayoutBindings[0].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    descriptorSetLayoutBindings[1].binding = 1;
    descriptorSetLayoutBindings[1].descriptorCount = 1;
    descriptorSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorSetLayoutBindings[1].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    descriptorSetLayoutBindings[2].binding = 2;
    descriptorSetLayoutBindings[2].descriptorCount = 1;
    descriptorSetLayoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
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

  void createDescriptorPool() {
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 2;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 1;
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

    // Update combined image and sampler to descriptor set
    VkDescriptorImageInfo imageInfo[2]{};
    imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo[0].imageView = diffMapImageView[0];
    imageInfo[0].sampler = textureSampler;
    imageInfo[1] = imageInfo[0];
    imageInfo[1].imageView = alphaMapImageView[0];

    VkWriteDescriptorSet writeDesc[3]{};
    writeDesc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDesc[0].dstSet = descriptorSets[0];
    writeDesc[0].dstBinding = 0;
    writeDesc[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDesc[0].descriptorCount = 1;
    writeDesc[0].pImageInfo = &imageInfo[0];
    writeDesc[1] = writeDesc[0];
    writeDesc[1].dstBinding = 1;
    writeDesc[1].pImageInfo = &imageInfo[1];

    VkDescriptorBufferInfo dbi{};
    dbi.buffer = vertexBuffer;
    dbi.offset = 0;
    dbi.range = sizeof(Vertex) * g_vertices.size();
    writeDesc[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDesc[2].dstSet = descriptorSets[0];
    writeDesc[2].dstBinding = 2;
    writeDesc[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writeDesc[2].descriptorCount = 1;
    writeDesc[2].pBufferInfo = &dbi;

    vkUpdateDescriptorSets(device, _countof(writeDesc), writeDesc, 0, nullptr);
  }

private:
  VkInstance instance;
  VkDebugUtilsMessengerEXT debugMessenger;
  VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
  VkDevice device;
  VkQueue graphicsQueue, presentQueue;
  VkSurfaceKHR surface;
  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;
  std::vector<VkImageView> swapChainImageViews;
  VkPipelineLayout pipelineLayout;
  VkRenderPass renderPass;
  VkPipeline graphicsPipeline;
  std::vector<VkFramebuffer> swapChainFramebuffers;
  VkCommandPool commandPool;
  VkCommandBuffer commandBuffer;
  VkSemaphore imageAvailableSemaphore;
  VkSemaphore renderFinishedSemaphore;
  VkFence inFlightFence;
  VkBuffer vertexBuffer;
  VkDeviceMemory vertexBufferMemory;
  VkBuffer indexBuffer;
  VkDeviceMemory indexBufferMemory;
  VkBuffer uvBuffer;
  VkDeviceMemory uvBufferMemory;
  VkImage rtOutputImages[MAX_FRAMES_IN_FLIGHT];
  VkDeviceMemory rtOutputImageMemories[MAX_FRAMES_IN_FLIGHT];
  VkImageView rtOutputImageViews[MAX_FRAMES_IN_FLIGHT];
  VkAccelerationStructureKHR blas;
  VkBuffer blasResultBuffer;
  VkDeviceMemory blasResultMemory;

  VkAccelerationStructureKHR blas0, blas1;
  VkBuffer blasResultBuffer0, blasResultBuffer1;
  VkDeviceMemory blasResultMemory0, blasResultMemory1;
  std::vector<VkBuffer> asResultBuffers;
  std::vector<VkDeviceMemory> asResultMemories;

  VkAccelerationStructureKHR tlas;
  VkBuffer tlasResultBuffer;
  VkDeviceMemory tlasResultMemory;
  VkDescriptorSetLayout rtDescriptorSetLayout;
  VkDescriptorSetLayout rtDescriptorSet1Layout;
  VkPipelineLayout rtPipelineLayout;
  VkPipeline rtPipeline;
  VkDescriptorPool rtDescriptorPool;
  VkDescriptorPool rtDescriptorSet1Pool;
  VkDescriptorSet rtDescriptorSets[MAX_FRAMES_IN_FLIGHT]{};
  VkDescriptorSet rtDescriptorSet1Set;
  VkBuffer sbtBuffer;
  VkDeviceMemory sbtMemory;
  VkStridedDeviceAddressRegionKHR rtRgenRegion{}, rtMissRegion{}, rtHitRegion{}, rtCallRegion{};

  VkImage textureImage[2];
  VkDeviceMemory textureImageMemory[2];
  VkImageView textureImageView[2];

  VkImage diffMapImage[128];
  VkDeviceMemory diffMapImageMemory[128];
  VkImageView diffMapImageView[128];
  VkImage alphaMapImage[128];
  VkDeviceMemory alphaMapImageMemory[128];
  VkImageView alphaMapImageView[128];

  VkSampler textureSampler;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorPool descriptorPool;
  VkDescriptorSet descriptorSets[1]{};

  std::vector<VkBuffer> ommBuffersToDelete;
  std::vector<VkDeviceMemory> ommMemoriesToFree;

  omm::GpuBakeNvrhi::Buffers gpu_omm_output;
  VkDescriptorPool imguiDescriptorPool;

  uint32_t graphicsQueueFamilyIndex{};
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
    default:
      break;
    }
  }
}

static void Log(omm::MessageSeverity severity, const char* message, void* userArg)
{
  const char* sev = "";
  switch (severity)
  {
  case omm::MessageSeverity::Info:
    sev = "INFO";
    break;
  case omm::MessageSeverity::Warning:
    sev = "WARNING";
    break;
  case omm::MessageSeverity::PerfWarning:
    sev = "PERF_WARNING";
    break;
  case omm::MessageSeverity::Fatal:
    sev = "FATAL";
    break;
  }

  printf("[omm-sdk] [%s] %s\n", sev, message);
}

void parseOMMBinaryFile(const char* file_name) {
  {
    FILE* f;
    fopen_s(&f, file_name, "rb");
    fseek(f, 0, SEEK_END);
    long ofst = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(ofst);
    fread(data.data(), 1, ofst, f);
    fclose(f);
    omm::Cpu::BlobDesc blobDesc;
    blobDesc.data = (void*)data.data();
    blobDesc.size = data.size();

    // Use official binary reader
    omm::BakerCreationDesc desc{};
    desc.type = omm::BakerType::CPU;
    desc.messageInterface.messageCallback = &Log;

    omm::Baker baker;
    omm::Result res = omm::CreateBaker(desc, &baker);
    assert(res == omm::Result::SUCCESS);
    omm::Cpu::DeserializedResult dr;
    omm::Result err = omm::Cpu::Deserialize(baker, blobDesc, &dr);
    assert(res == omm::Result::SUCCESS);
    const omm::Cpu::DeserializedDesc* deserializedDesc = nullptr;
    res = omm::Cpu::GetDeserializedDesc(dr, &deserializedDesc);
    assert(res == omm::Result::SUCCESS);
    printf("%d input descs", deserializedDesc->numInputDescs);
  }

#define Read(x) assert(1==fread(&x, sizeof(x), 1, f));
  FILE* f;
  fopen_s(&f, file_name, "rb");
  printf("Opening OMM binary file %s\n", file_name);
  if (!f) {
    printf("Oh! file %s is not good.\n", file_name);
    exit(0);
  }
  char xxh64_hash[8];
  assert(1 == fread(xxh64_hash, 8, 1, f));
  int major{}, minor{}, patch{};
  assert(1 == fread(&major, 4, 1, f));
  assert(1 == fread(&minor, 4, 1, f));
  assert(1 == fread(&patch, 4, 1, f));
  int input_desc_version{};
  assert(1 == fread(&input_desc_version, 4, 1, f));
  printf("OMM ver: %d.%d.%d, input desc ver %d\n", major, minor, patch, input_desc_version);


  int flags{};
  assert(1 == fread(&flags, 4, 1, f));
  if (flags & int(omm::Cpu::SerializeFlags::Compress)) {
    printf("Oh! Don't know how to deal with compressed OMM binary.\n");
    exit(0);
  }

  if (input_desc_version >= 2) {
    int decompressed_size{};
    Read(decompressed_size);
    printf("decompressed size: %d\n", decompressed_size);
  }

  int num_input_descs{};
  Read(num_input_descs);
  printf("%d input descs\n", num_input_descs);
  for (int i = 0; i < num_input_descs; i++) {
    int bakeFlags;
    Read(bakeFlags);

    // start texture
    int num_mips = 0;
    Read(num_mips);
    printf("%d mips.\n", num_mips);

    for (int i = 0; i < num_mips; i++) {
      /*
          int2 size;
          int2 sizeLog2;
          float2 sizef;
          bool sizeIsPow2;
          float2 rcpSize;
          int2 sizeMinusOne;
          uintptr_t dataOffset;
          size_t numElements;
          uintptr_t dataOffsetSAT;
      */
      int size_x{}, size_y{}; float rcpSize_x{}, rcpSize_y{};
      uint32_t dataOffset{};
      size_t numElements{};
      uint32_t dataOffsetSAT{};
      Read(size_x); Read(size_y);
      Read(rcpSize_x); Read(rcpSize_y);
      Read(dataOffset);
      Read(numElements);
      Read(dataOffsetSAT);

      printf("mip[%d]: %dx%d (rcp:%gx%g)\n", i, size_x, size_y, rcpSize_x, rcpSize_y);
    }

    int tiling_mode{};
    Read(tiling_mode);
    printf("Tiling mode: %d\n", tiling_mode);

    int texture_flags{}; float alpha_cutoff{};
    if (input_desc_version >= 3) {
      Read(texture_flags);
      Read(alpha_cutoff);
    }

    int texture_format{};
    Read(texture_format);

    size_t data_size{};
    Read(data_size);

    std::vector<uint8_t> data(data_size);
    assert(data_size == fread(data.data(), 1, data_size, f));

    size_t data_sat_size{};
    Read(data_sat_size);
    if (data_sat_size > 0) {
      std::vector<uint8_t> data_sat(data_sat_size);
      assert(data_sat_size == fread(data_sat.data(), 1, data_sat_size, f));
    }
    // end texture

    int addressing_mode, filter, alpha_mode;
    float border_alpha;
    Read(addressing_mode);
    Read(filter);
    Read(border_alpha);
    Read(alpha_mode);
    
    int texcoord_format;
    Read(texcoord_format);

    size_t texcoord_size{};
    Read(texcoord_size);
    if (texcoord_size != 0) {
      std::vector<uint8_t> texcoords(texcoord_size, 16);
      assert(texcoord_size == fread(texcoords.data(), 1, texcoord_size, f));
      printf("texcoords:");
      for (uint8_t tc : texcoords) {
        printf(" %d", int(tc));
      }
    }

    assert(0 && "unimplemented");
  }
 
  int num_result_descs{};
  Read(num_result_descs);
  printf("%d result descs\n", num_result_descs);

  for (int i = 0; i < num_result_descs; i++) {
    uint32_t element_count{};
    Read(element_count);
    printf("%zu\n", ftell(f));
    printf("OMM array: %u bytes\n", element_count);
  }

  fclose(f);
#undef Read
}

int main(int argc, char** argv) {
  // Test NVRHI OMM
  #define STR2(x) #x
  #define STR(x) STR2(x)
  printf("VK_HEADER_VERSION = %s\n", STR(VK_HEADER_VERSION));
  #undef STR
  #undef STR2


  if (argc > 1) {
    printf("Will read OMM dump file.\n");
    parseOMMBinaryFile(argv[1]);
    return 0;
  }
  g_app = new HelloTriangleApplication();

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