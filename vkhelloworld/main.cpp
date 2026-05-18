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

#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "omm-gpu-nvrhi/omm-gpu-nvrhi.h"
#include "nvrhi/vulkan.h"

// ImGuI
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

// MyFramework
#include "myframework_vk.h"
MyFrameworkVk* g_framework{};

// OMM handling
bool g_use_omm{ false };
int g_omm_subdiv_levels[2] = { 8, 3 };
bool g_use_gpubaker_results{ false };  // Use pre-existing results or bake on the spot?
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
bool g_should_recreate_as = false;
bool g_omm_use_gpubaker_results = false;
const uint32_t MAX_FRAMES_IN_FLIGHT = 3;
class HelloTriangleApplication;
HelloTriangleApplication* g_app;

static std::vector<const char*> deviceExtensions = {
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

static std::vector<const char*> ommExtensions = {
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

static std::vector<const char*> getRequiredExtensions() {
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

static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
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

static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
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
    //initWindow();
    g_framework->InitWindow("VK HelloWorld", WIDTH, HEIGHT, KeyCallback);
    window = g_framework->window;

    initVulkan();
    initNvrhiOnce();
    initImGui();
    initOMMBaker();
    mainLoop();
    cleanup();
  }

  bool should_exit{ false };

private:
  void initVulkan() {
    g_framework->InitDeviceAndCommandQ();
    surface = g_framework->surface;
    instance = g_framework->instance;
    if (enableValidationLayers) {
      setupDebugMessenger();
    }
    g_use_omm = g_framework->hasOMM;
    physicalDevice = g_framework->physicalDevice;
    device = g_framework->device;
    graphicsQueue = g_framework->graphicsQueue;
    presentQueue = g_framework->presentQueue;
    g_framework->InitSwapchain();
    swapChain = g_framework->swapChain;
    swapChainImages = g_framework->swapChainImages;
    swapChainImageFormat = g_framework->swapChainImageFormat;
    swapChainExtent = g_framework->swapChainExtent;
    swapChainImageViews = g_framework->swapChainImageViews;

    g_framework->InitRenderPassAndFramebuffers();
    swapChainFramebuffers = g_framework->swapChainFramebuffers;
    renderPass = g_framework->renderPass;

    g_framework->InitImGuiRenderPass();
    imguiRenderPass = g_framework->imguiRenderPass;
    commandPool = g_framework->commandPool;
    commandBuffer = g_framework->commandBuffer;
    readOBJ();
    createTextureImage();
    textureSampler = g_framework->CreateTextureSampler();
    createVertexBuffer();
    createUVBuffer();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets();
    createGraphicsPipeline();
    g_framework->CreateImGuiFramebuffers();
    imguiFramebuffers = g_framework->imguiFramebuffers;
    createSyncObjects();
    createAS();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      g_framework->CreateRtOutputResource(WIDTH, HEIGHT, rtOutputImages[i], rtOutputImageMemories[i], rtOutputImageViews[i]);
    }
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
    init_info.QueueFamily = g_framework->queueFamilyIndices.graphicsFamily.value();
    init_info.Queue = graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = imguiDescriptorPool;
    init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
    init_info.ImageCount = swapChainImages.size();
    init_info.UseDynamicRendering = false;
    init_info.PipelineInfoMain.RenderPass = imguiRenderPass;
    init_info.PipelineInfoMain.Subpass = 0;

    // 初始化 Vulkan 后端
    bool ret = ImGui_ImplVulkan_Init(&init_info);      // 这里的 renderPass 是你现有的渲染通道
    printf("ImGui_ImplVulkan_Init returned %d\n", ret);
  }

  struct BakedTriangleOmm
  {
    nvrhi::BufferHandle ommArrayBuffer;
    nvrhi::BufferHandle ommDescBuffer;
    nvrhi::BufferHandle ommIndexBuffer;
    nvrhi::BufferHandle ommDescArrayHistogramBuffer;
    nvrhi::BufferHandle ommIndexHistogramBuffer;
    nvrhi::BufferHandle ommPostDispatchInfoBuffer;

    uint32_t ommArrayBufferSize = 0;
    uint32_t ommDescBufferSize = 0;
    uint32_t ommIndexBufferSize = 0;
    uint32_t ommIndexCount = 0;
    uint32_t ommDescArrayHistogramBufferSize = 0;
    uint32_t ommIndexHistogramBufferSize = 0;
    nvrhi::Format ommIndexFormat = nvrhi::Format::UNKNOWN;

    std::vector<omm::Cpu::OpacityMicromapUsageCount> ommDescArrayHistogram;
    std::vector<omm::Cpu::OpacityMicromapUsageCount> ommIndexHistogram;
  };

  BakedTriangleOmm BakeOneTriangleOMM(
    omm::GpuBakeNvrhi& baker,
    nvrhi::DeviceHandle nvrhiDevice,
    nvrhi::CommandListHandle commandList,
    nvrhi::TextureHandle alphaTexture,
    nvrhi::BufferHandle uvBuffer,
    nvrhi::BufferHandle triangleIndexBuffer,
    uint32_t subdivisionLevel)
  {
    omm::GpuBakeNvrhi::Input input{};
    input.operation = omm::GpuBakeNvrhi::Operation::SetupAndBake;

    input.alphaTexture = alphaTexture;
    input.alphaTextureChannel = 0;
    input.alphaCutoff = 0.5f;
    input.alphaCutoffLessEqual = omm::OpacityState::Transparent;
    input.alphaCutoffGreater = omm::OpacityState::Opaque;

    input.texCoordBuffer = uvBuffer;
    input.texCoordFormat = nvrhi::Format::R32_FLOAT;
    input.texCoordStrideInBytes = sizeof(glm::vec2);

    input.indexBuffer = triangleIndexBuffer;
    input.numIndices = 3;

    input.maxSubdivisionLevel = subdivisionLevel;
    input.format = nvrhi::rt::OpacityMicromapFormat::OC1_4_State;

    input.dynamicSubdivisionScale = 1.0f;
    input.enableStats = true;
    input.enableSpecialIndices = false;
    input.force32BitIndices = false;
    input.enableTexCoordDeduplication = true;
    input.computeOnly = false;
    input.maxOutOmmArraySize = 0xFFFFFFFF;
    input.bilinearFilter = true;

    omm::GpuBakeNvrhi::PreDispatchInfo info{};
    baker.GetPreDispatchInfo(input, info);

    auto createRawUavBuffer = [&](size_t byteSize, const char* name)
      {
        nvrhi::BufferDesc desc{};
        desc.byteSize = std::max<size_t>(byteSize, 4);
        desc.debugName = name;
        desc.canHaveRawViews = true;
        desc.canHaveUAVs = true;
        desc.canHaveTypedViews = true;
        desc.initialState = nvrhi::ResourceStates::Common;
        desc.keepInitialState = true;
        return nvrhiDevice->createBuffer(desc);
      };

    omm::GpuBakeNvrhi::Buffers output{};
    output.ommArrayBuffer = createRawUavBuffer(info.ommArrayBufferSize, "Triangle OMM Array Buffer");
    output.ommDescBuffer = createRawUavBuffer(info.ommDescBufferSize, "Triangle OMM Desc Buffer");
    output.ommIndexBuffer = createRawUavBuffer(info.ommIndexBufferSize, "Triangle OMM Index Buffer");
    output.ommDescArrayHistogramBuffer = createRawUavBuffer(info.ommDescArrayHistogramSize, "Triangle OMM Desc Histogram Buffer");
    output.ommIndexHistogramBuffer = createRawUavBuffer(info.ommIndexHistogramSize, "Triangle OMM Index Histogram Buffer");
    output.ommPostDispatchInfoBuffer = createRawUavBuffer(info.ommPostDispatchInfoBufferSize, "Triangle OMM Post Info Buffer");

    commandList->beginTrackingBufferState(output.ommArrayBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(output.ommDescBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(output.ommIndexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(output.ommDescArrayHistogramBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(output.ommIndexHistogramBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(output.ommPostDispatchInfoBuffer, nvrhi::ResourceStates::Common);

    baker.Dispatch(commandList, input, output);

    BakedTriangleOmm result{};
    result.ommArrayBuffer = output.ommArrayBuffer;
    result.ommDescBuffer = output.ommDescBuffer;
    result.ommIndexBuffer = output.ommIndexBuffer;
    result.ommDescArrayHistogramBuffer = output.ommDescArrayHistogramBuffer;
    result.ommIndexHistogramBuffer = output.ommIndexHistogramBuffer;
    result.ommPostDispatchInfoBuffer = output.ommPostDispatchInfoBuffer;

    result.ommArrayBufferSize = info.ommArrayBufferSize;
    result.ommDescBufferSize = info.ommDescBufferSize;
    result.ommIndexBufferSize = info.ommIndexBufferSize;
    result.ommIndexCount = info.ommIndexCount;
    result.ommIndexFormat = info.ommIndexFormat;

    result.ommDescArrayHistogramBufferSize = info.ommDescArrayHistogramSize;  // in bytes
    result.ommIndexHistogramBufferSize = info.ommIndexHistogramSize;

    return result;
  }

  nvrhi::BufferHandle CreateTriangleIndexBufferNvrhi(
    nvrhi::DeviceHandle nvrhiDevice,
    nvrhi::CommandListHandle commandList,
    const uint32_t triIndices[3])
  {
    nvrhi::BufferDesc desc{};
    desc.byteSize = sizeof(uint32_t) * 3;
    desc.debugName = "PerTriangleIndexBuffer";
    desc.format = nvrhi::Format::R32_UINT;
    desc.canHaveRawViews = true;
    desc.canHaveTypedViews = true;
    desc.canHaveUAVs = true;
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    desc.keepInitialState = true;

    nvrhi::BufferHandle buffer = nvrhiDevice->createBuffer(desc);

    commandList->beginTrackingBufferState(buffer, nvrhi::ResourceStates::Common);
    commandList->writeBuffer(buffer, triIndices, sizeof(uint32_t) * 3);
    commandList->setPermanentBufferState(buffer, nvrhi::ResourceStates::ShaderResource);

    return buffer;
  }

  void initOMMBaker() {
    initNvrhiOnce();


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
    nvrhi::DeviceHandle nvrhiDevice = m_nvrhiDevice;
    auto commandList = m_nvrhiCommandList;

    {  // commandList opens
      commandList->open();
      omm::GpuBakeNvrhi baker(nvrhiDevice, commandList, false);
      printf("[initOMMBaker] Successfully created a GPU Baker.\n");

      uint32_t tri0[3] = { 0, 1, 2 };
      uint32_t tri1[3] = { 3, 4, 5 };

      auto tri0IndexBuffer = CreateTriangleIndexBufferNvrhi(nvrhiDevice, commandList, tri0);
      auto tri1IndexBuffer = CreateTriangleIndexBufferNvrhi(nvrhiDevice, commandList, tri1);
      printf("Executed OMM buliding commandlist\n");

      // Alpha Texture
      nvrhi::TextureDesc texDesc{};
      texDesc.width = 256;
      texDesc.height = 256;
      texDesc.depth = 1;
      texDesc.arraySize = 1;
      texDesc.mipLevels = 1;
      texDesc.dimension = nvrhi::TextureDimension::Texture2D;
      texDesc.format = nvrhi::Format::RGBA8_UNORM;
      texDesc.debugName = "AlphaTexture";
      texDesc.isShaderResource = true;
      texDesc.initialState = nvrhi::ResourceStates::Unknown;
      texDesc.keepInitialState = false;

      // UV Buffer
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
        nvrhi::ObjectTypes::VK_Buffer, uvBuffer, bufDesc);

      nvrhi::TextureHandle alphaTextureNvrhi =
        nvrhiDevice->createHandleForNativeTexture(
          nvrhi::ObjectTypes::VK_Image,
          alphaMapImage[0],
          texDesc
        );

      tri0Omm_gpu = BakeOneTriangleOMM(
        baker,
        nvrhiDevice,
        commandList,
        alphaTextureNvrhi,
        uvBufferHandle,
        tri0IndexBuffer,
        g_omm_subdiv_levels[0]
      );

      tri1Omm_gpu = BakeOneTriangleOMM(
        baker,
        nvrhiDevice,
        commandList,
        alphaTextureNvrhi,
        uvBufferHandle,
        tri1IndexBuffer,
        g_omm_subdiv_levels[1]
      );
      commandList->close();
      nvrhiDevice->executeCommandList(commandList);
      nvrhiDevice->waitForIdle();

      alphaTextureNvrhi.Detach();
      uvBufferHandle.Detach();
    }  // commandList closes

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

    // Print out
    auto readBuffer = [&](nvrhi::BufferHandle buffer, size_t size = SIZE_MAX)
      {
        std::vector<uint8_t> data;
        const size_t byteSize = (size == SIZE_MAX) ? buffer->getDesc().byteSize
                                                   : std::min<size_t>(size, buffer->getDesc().byteSize);
        if (byteSize == 0) return data;
        void* mapped = nvrhiDevice->mapBuffer(buffer, nvrhi::CpuAccessMode::Read);
        assert(mapped);
        data.resize(byteSize);
        memcpy(data.data(), mapped, byteSize);
        nvrhiDevice->unmapBuffer(buffer);
        return data;
      };

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

    auto PrintOneOMMStats = [&](const char* tag, BakedTriangleOmm& omm) {
      printf("%s:\n", tag);
      printf("array buf size = %u\n", omm.ommArrayBufferSize);
      printf("desc buf size  = %u\n", omm.ommDescBufferSize);
      printf("idx buf size   = %u\n", omm.ommIndexBufferSize);
      printf("idx count      = %u, format = %u\n", omm.ommIndexCount, omm.ommIndexFormat);
      
      nvrhi::BufferHandle ommArrayBuffer = omm.ommArrayBuffer;
      nvrhi::BufferHandle ommArrayReadback =
        createReadbackBuffer(ommArrayBuffer, "OMM Array Readback");

      commandList->open();
      commandList->beginTrackingBufferState(ommArrayReadback, nvrhi::ResourceStates::Common);
      commandList->copyBuffer(ommArrayReadback, 0, ommArrayBuffer, 0, ommArrayBuffer->getDesc().byteSize);
      commandList->close();
      nvrhiDevice->executeCommandList(commandList);
      nvrhiDevice->waitForIdle();

      std::vector<uint8_t> ommArrayData = readBuffer(ommArrayReadback, omm.ommArrayBufferSize);
      dumpBytes("Array Data", ommArrayData);
    };

    auto PopulateCPUSideOMMHistograms = [&](BakedTriangleOmm& omm) {
      auto descArrayHistogramReadback = createReadbackBuffer(omm.ommDescArrayHistogramBuffer, "DAHB");
      auto indexHistogramReadback = createReadbackBuffer(omm.ommIndexHistogramBuffer, "IHB");

      commandList->open();
      commandList->beginTrackingBufferState(descArrayHistogramReadback, nvrhi::ResourceStates::Common);
      commandList->copyBuffer(descArrayHistogramReadback, 0, omm.ommDescArrayHistogramBuffer, 0, omm.ommDescArrayHistogramBuffer->getDesc().byteSize);
      commandList->beginTrackingBufferState(indexHistogramReadback, nvrhi::ResourceStates::Common);
      commandList->copyBuffer(indexHistogramReadback, 0, omm.ommIndexHistogramBuffer, 0, omm.ommIndexHistogramBuffer->getDesc().byteSize);
      commandList->close();
      nvrhiDevice->executeCommandList(commandList);
      nvrhiDevice->waitForIdle();

      uint32_t sz_omuc = sizeof(omm::Cpu::OpacityMicromapUsageCount);
      printf("DAHB size %u, sizeof(OpacityMicromapUsageCount) is %zu\n",
        omm.ommDescArrayHistogramBufferSize, sz_omuc);
      std::vector<uint8_t> buf = readBuffer(descArrayHistogramReadback, omm.ommDescArrayHistogramBufferSize);
      omm.ommDescArrayHistogram.resize(buf.size() / sz_omuc);
      memcpy(omm.ommDescArrayHistogram.data(), buf.data(), sz_omuc* omm.ommDescArrayHistogram.size());
      for (uint32_t i = 0; i < omm.ommDescArrayHistogram.size(); i++) {
        omm::Cpu::OpacityMicromapUsageCount* omuc = &(omm.ommDescArrayHistogram[i]);
        printf(" [%u]: count=%u, format=%u, level=%u\n",
          i,
          omuc->count, omuc->format, omuc->subdivisionLevel);
      }

      printf("IHB size %u\n", omm.ommIndexHistogramBufferSize);
      buf = readBuffer(indexHistogramReadback, omm.ommIndexHistogramBufferSize);
      omm.ommIndexHistogram.resize(buf.size() / sz_omuc);
      memcpy(omm.ommIndexHistogram.data(), buf.data(), sz_omuc* omm.ommIndexHistogram.size());
      for (uint32_t i = 0; i < omm.ommIndexHistogram.size(); i++) {
        omm::Cpu::OpacityMicromapUsageCount* omuc = &(omm.ommIndexHistogram[i]);
        printf(" [%u]: count=%u, format=%u, level=%u\n",
          i,
          omuc->count, omuc->format, omuc->subdivisionLevel);
      }
    };

    PrintOneOMMStats("Tri0 OMM", tri0Omm_gpu);
    PrintOneOMMStats("Tri1 OMM", tri1Omm_gpu);

    PopulateCPUSideOMMHistograms(tri0Omm_gpu);
    PopulateCPUSideOMMHistograms(tri1Omm_gpu);
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window) && !should_exit) {
      glfwPollEvents();
      drawFrame();
      if (g_should_recreate_as) {
        g_should_recreate_as = false;
        vkDeviceWaitIdle(device);
        if (g_omm_use_gpubaker_results) {
          initOMMBaker();
        }
        g_app->createAS();
        g_app->updateRtDescriptorSets();
        vkDeviceWaitIdle(device);
      }
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
    vkDestroyBuffer(device, indexBuffer0, nullptr);
    vkDestroyBuffer(device, indexBuffer1, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);
    vkFreeMemory(device, indexBuffer0Memory, nullptr);
    vkFreeMemory(device, indexBuffer1Memory, nullptr);
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

    renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = imguiRenderPass;
    renderPassInfo.framebuffer = imguiFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = swapChainExtent;
    renderPassInfo.clearValueCount = 0;
    renderPassInfo.pClearValues = nullptr;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    renderImGuiAndEndImGuiForFrame();
    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not end command buffer");
    }
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
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
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
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
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

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = imguiRenderPass;
    renderPassInfo.framebuffer = imguiFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = swapChainExtent;
    renderPassInfo.clearValueCount = 0;
    renderPassInfo.pClearValues = nullptr;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    renderImGuiAndEndImGuiForFrame();
    vkCmdEndRenderPass(commandBuffer);

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
    ImGui::SetNextWindowSize(ImVec2(240, 240), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(32, 32), ImGuiCond_Once);
    ImGui::Begin("VK OMM Test.");
    ImGui::Checkbox("RT", &g_is_rt);
    {
      if (ImGui::Button("Recreate AS")) {
        g_should_recreate_as = true;
      }
      ImGui::SliderInt2("SubDiv", g_omm_subdiv_levels, 1, 8, "%d");
      ImGui::Checkbox("Use GPU Baker Results", &g_omm_use_gpubaker_results);
    }
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
    setObjectName((uint64_t)imageAvailableSemaphore, VK_OBJECT_TYPE_SEMAPHORE, "Image Available Semaphore");

    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create semaphore");
    }
    setObjectName((uint64_t)renderFinishedSemaphore, VK_OBJECT_TYPE_SEMAPHORE, "Render Finished Semaphore");

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
    createInfo.size = sizeof(uint32_t) * 3;
    if (vkCreateBuffer(device, &createInfo, nullptr, &indexBuffer0) != VK_SUCCESS) {
      throw std::runtime_error("Could not create index buffer 0");
    }
    if (vkCreateBuffer(device, &createInfo, nullptr, &indexBuffer1) != VK_SUCCESS) {
      throw std::runtime_error("Could not create index buffer 0");
    }
    vkGetBufferMemoryRequirements(device, indexBuffer0, &memReq);
    allocInfo.allocationSize = memReq.size;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &indexBuffer0Memory) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate memory for index buffer");
    }
    if (vkAllocateMemory(device, &allocInfo, nullptr, &indexBuffer1Memory) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate memory for index buffer");
    }
    vkBindBufferMemory(device, indexBuffer0, indexBuffer0Memory, 0);
    vkBindBufferMemory(device, indexBuffer1, indexBuffer1Memory, 0);
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < 3; i++) {
      indices.push_back(i);
    }
    vkMapMemory(device, indexBuffer0Memory, 0, sizeof(uint32_t) * 3, 0, &data);
    memcpy(data, indices.data(), sizeof(uint32_t) * indices.size());
    vkUnmapMemory(device, indexBuffer0Memory);

    indices.clear();
    for (uint32_t i = 3; i < 6; i++) {
      indices.push_back(i);
    }
    vkMapMemory(device, indexBuffer1Memory, 0, sizeof(uint32_t) * 3, 0, &data);
    memcpy(data, indices.data(), sizeof(uint32_t) * indices.size());
    vkUnmapMemory(device, indexBuffer1Memory);
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

  public:
  void createAS() {
    if (blas0) {
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
      vkDestroyBuffer(device, tlasResultBuffer, nullptr);
    }

    // Copy OMM data to the GPU
    MyFrameworkVk::MyOmmAttachmentInfo ommas[2]{}, * p_ommas[2]{};
    if (g_use_omm) {
      if (g_omm_use_gpubaker_results) {
        for (uint32_t pidx = 0; pidx < 2; pidx++) {
          BakedTriangleOmm* baked = (pidx == 0) ? &tri0Omm_gpu : &tri1Omm_gpu;
          MyFrameworkVk::MyOmmAttachmentInfo* p_omma = &ommas[pidx];
          uint32_t usageCountsCount = baked->ommDescArrayHistogram.size();
          for (uint32_t i = 0; i < usageCountsCount; i++) {
            auto usage0 = baked->ommDescArrayHistogram[i];
            VkMicromapUsageEXT usage{};
            usage.count = usage0.count;
            usage.format = usage0.format;
            usage.subdivisionLevel = usage0.subdivisionLevel;
            p_omma->usageCounts.push_back(usage);
          }

          uint32_t indexHistogramsCount = tri0Omm_gpu.ommIndexHistogram.size();
          for (uint32_t i = 0; i < indexHistogramsCount; i++) {
            auto usage0 = tri0Omm_gpu.ommIndexHistogram[i];
            VkMicromapUsageEXT usage{};
            usage.count = usage0.count;
            usage.format = usage0.format;
            usage.subdivisionLevel = usage0.subdivisionLevel;
            p_omma->indexHistograms.push_back(usage);
          }

          p_omma->arrayBufferAddress = baked->ommArrayBuffer->getGpuVirtualAddress();
          p_omma->arrayDescsAddress = baked->ommDescBuffer->getGpuVirtualAddress();
          p_omma->arrayDataSize = baked->ommArrayBufferSize;
          p_omma->indexBufferAddress = baked->ommIndexBuffer->getGpuVirtualAddress();

          p_ommas[pidx] = p_omma;
        }
      }
      else {  // Bake on CPU
        for (uint32_t pidx = 0; pidx < 2; pidx++) {
          const omm::Cpu::BakeResultDesc* res_desc = bakeOmmForMask(pidx, g_omm_subdiv_levels[pidx]);
          MyFrameworkVk::MyOmmAttachmentInfo* p_omma = &ommas[pidx];
          
          // Copy to GPU
          // OMM Array Data
          VkBuffer ommArrayBuffer{};
          VkDeviceMemory ommArrayMemory{};
          g_framework->CreateBuffer(res_desc->arrayDataSize,
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
          uint32_t descArraySize = res_desc->descArrayCount * sizeof(VkMicromapTriangleEXT);
          g_framework->CreateBuffer(descArraySize,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
            | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            ommDescArrayBuffer, ommDescArrayMemory);
          vkMapMemory(device, ommDescArrayMemory, 0, descArraySize, 0, &data);
          memcpy(data, res_desc->descArray, descArraySize);
          vkUnmapMemory(device, ommDescArrayMemory);

          VkBuffer ommIndexBuffer{};
          VkDeviceMemory ommIndexMemory{};
          assert(res_desc->indexFormat == omm::IndexFormat::UINT_32);
          uint32_t indexSize = sizeof(uint32_t) * res_desc->indexCount;
          g_framework->CreateBuffer(indexSize,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
            | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            ommIndexBuffer, ommIndexMemory);
          vkMapMemory(device, ommIndexMemory, 0, indexSize, 0, &data);
          memcpy(data, res_desc->indexBuffer, indexSize);
          vkUnmapMemory(device, ommIndexMemory);

          uint32_t usageCountsCount = res_desc->descArrayHistogramCount;
          for (uint32_t i = 0; i < usageCountsCount; i++) {
            auto usage0 = res_desc->descArrayHistogram[i];
            VkMicromapUsageEXT usage{};
            usage.count = usage0.count;
            usage.format = usage0.format;
            usage.subdivisionLevel = usage0.subdivisionLevel;
            p_omma->usageCounts.push_back(usage);
          }

          uint32_t indexHistogramsCount = res_desc->indexHistogramCount;
          for (uint32_t i = 0; i < indexHistogramsCount; i++) {
            auto usage0 = res_desc->indexHistogram[i];
            VkMicromapUsageEXT usage{};
            usage.count = usage0.count;
            usage.format = usage0.format;
            usage.subdivisionLevel = usage0.subdivisionLevel;
            p_omma->indexHistograms.push_back(usage);
          }

          p_omma->arrayDataSize = res_desc->arrayDataSize;
          p_omma->indexBufferAddress = getBufferDeviceAddress(ommIndexBuffer);
          p_omma->arrayBufferAddress = getBufferDeviceAddress(ommArrayBuffer);
          p_omma->arrayDescsAddress = getBufferDeviceAddress(ommDescArrayBuffer);
          

          ommBuffersToDelete.push_back(ommArrayBuffer);
          ommBuffersToDelete.push_back(ommDescArrayBuffer);
          ommBuffersToDelete.push_back(ommIndexBuffer);
          ommMemoriesToFree.push_back(ommArrayMemory);
          ommMemoriesToFree.push_back(ommDescArrayMemory);
          ommMemoriesToFree.push_back(ommIndexMemory);

          p_ommas[pidx] = p_omma;
        }
      }
    }

    g_framework->BuildBLAS(blas0, blasResultBuffer0, blasResultMemory0,
      vertexBuffer, indexBuffer0, 3, sizeof(Vertex), p_ommas[0]);
    g_framework->BuildBLAS(blas1, blasResultBuffer1, blasResultMemory1,
      vertexBuffer, indexBuffer1, 3, sizeof(Vertex), p_ommas[1]);

    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.pNext = nullptr;

    // Build TLAS
    std::vector<VkAccelerationStructureKHR> blases = { blas0, blas1 };
    std::vector<VkBuffer> blas_buffers = { blasResultBuffer0, blasResultBuffer1 };
    g_framework->BuildTLAS(tlas, blases, blas_buffers, tlasResultBuffer, tlasResultMemory);
  }

  private:

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

  public:
  void updateRtDescriptorSets() {
    VkWriteDescriptorSet writeDesc[5 * MAX_FRAMES_IN_FLIGHT]{};

    // Update TLAS to descriptor set
    VkWriteDescriptorSetAccelerationStructureKHR writeDescAS{};
    writeDescAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    writeDescAS.accelerationStructureCount = 1;
    writeDescAS.pAccelerationStructures = &tlas;

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
  }

  private:
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

    updateRtDescriptorSets();

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
    rtPipelineCreateInfo.flags = VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;

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
        g_framework->LoadImageFromFile(fn.c_str(), *img, *image_view, *image_memory);
      }
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

  void initNvrhiOnce()
  {
    if (m_nvrhiDevice)
      return;

    auto& dldi = vk::detail::defaultDispatchLoaderDynamic;
    dldi.init(vkGetInstanceProcAddr);
    dldi.init(instance, vkGetInstanceProcAddr);

    nvrhi::vulkan::DeviceDesc desc{};
    desc.instance = instance;
    desc.physicalDevice = physicalDevice;
    desc.device = device;
    desc.graphicsQueue = graphicsQueue;
    desc.graphicsQueueIndex = graphicsQueueFamilyIndex;
    desc.deviceExtensions = deviceExtensions.data();
    desc.numDeviceExtensions = static_cast<uint32_t>(deviceExtensions.size());

    m_nvrhiDevice = nvrhi::vulkan::createDevice(desc);
    m_nvrhiCommandList = m_nvrhiDevice->createCommandList();
  }

  void setObjectName(uint64_t handle, VkObjectType type, const char* name)
  {
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;

    auto fpSetDebugUtilsObjectNameEXT =
      reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));

    if (fpSetDebugUtilsObjectNameEXT)
      fpSetDebugUtilsObjectNameEXT(device, &info);
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
  VkBuffer indexBuffer0;
  VkDeviceMemory indexBuffer0Memory;
  VkBuffer indexBuffer1;
  VkDeviceMemory indexBuffer1Memory;
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

  nvrhi::DeviceHandle m_nvrhiDevice;
  nvrhi::CommandListHandle m_nvrhiCommandList;
  BakedTriangleOmm tri0Omm_gpu, tri1Omm_gpu;
  VkDescriptorPool imguiDescriptorPool;
  VkRenderPass imguiRenderPass{};
  std::vector<VkFramebuffer> imguiFramebuffers;

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

int main(int argc, char** argv) {
  // Test NVRHI OMM
  #define STR2(x) #x
  #define STR(x) STR2(x)
  printf("VK_HEADER_VERSION = %s\n", STR(VK_HEADER_VERSION));
  #undef STR
  #undef STR2

  g_framework = new MyFrameworkVk();

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