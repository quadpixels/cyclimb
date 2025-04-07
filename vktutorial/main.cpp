#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <array>
#include <optional>
#include <set>

#include <omm.hpp>

bool g_is_nv = false;
bool g_is_rt = false;
bool g_use_omm = true;

const omm::Cpu::BakeResultDesc* bakeOmmForMask();

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

const int MAX_FRAMES_IN_FLIGHT = 2;

const char* tex_fns[] = {
      "textures/Paris_ivy_leaf_a_diff.png",
      "textures/Paris_ivy_leaf_a_mask.png"
};

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    VK_KHR_SPIRV_1_4_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
    VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME,
    VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME,
};

std::vector<const char*> ommExtensions = {
  VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME,
  VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME,
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  }
  else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

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

struct Vertex {
  alignas(16) glm::vec2 pos;
  alignas(16) glm::vec3 color;
  alignas(16) glm::vec2 texCoord;

  static VkVertexInputBindingDescription getBindingDescription() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    return bindingDescription;
  }

  static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

    return attributeDescriptions;
  }
};

struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

std::vector<Vertex> vertices = {
    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}
};

std::vector<uint32_t> indices = {
    0, 1, 2, 2, 3, 0
};

bool g_should_exit = false;

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  if (action == GLFW_PRESS) {
    switch (key) {
    case GLFW_KEY_ESCAPE: {
      g_should_exit = true;
      break;
    }
    case GLFW_KEY_SPACE: {
      g_is_rt = !g_is_rt;
      break;
    }
    default:
      break;
    }
  }
}

class HelloTriangleApplication {
public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  GLFWwindow* window;

  VkInstance instance;
  VkDebugUtilsMessengerEXT debugMessenger;
  VkSurfaceKHR surface;

  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device;

  VkQueue graphicsQueue;
  VkQueue presentQueue;

  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;
  std::vector<VkImageView> swapChainImageViews;
  std::vector<VkFramebuffer> swapChainFramebuffers;

  VkRenderPass renderPass;
  VkDescriptorSetLayout descriptorSetLayout;
  VkPipelineLayout pipelineLayout;
  VkPipeline graphicsPipeline;

  VkCommandPool commandPool;

  VkImage textureImage;
  VkDeviceMemory textureImageMemory;
  VkImageView textureImageView;
  VkSampler textureSampler;

  VkBuffer vertexBuffer;
  VkDeviceMemory vertexBufferMemory;
  VkBuffer indexBuffer;
  VkDeviceMemory indexBufferMemory;

  std::vector<VkBuffer> uniformBuffers;
  std::vector<VkDeviceMemory> uniformBuffersMemory;
  std::vector<void*> uniformBuffersMapped;

  VkDescriptorPool descriptorPool;
  std::vector<VkDescriptorSet> descriptorSets;

  std::vector<VkCommandBuffer> commandBuffers;

  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;
  uint32_t currentFrame = 0;

  // RT
  VkBuffer blasResultBuffer{};
  VkDeviceMemory blasResultMemory{};
  VkAccelerationStructureKHR blas;
  VkBuffer tlasResultBuffer{};
  VkDeviceMemory tlasResultMemory{};
  VkAccelerationStructureKHR tlas;
  std::vector<VkImage> rtOutputImages;
  std::vector<VkDeviceMemory> rtOutputImageMemories;
  std::vector<VkImageView> rtOutputImageViews;
  VkDescriptorSetLayout rtDescriptorSetLayout;
  VkDescriptorPool rtDescriptorPool;
  std::vector<VkDescriptorSet> rtDescriptorSets;
  VkImage rtTextureImage[2];
  VkImageView rtTextureImageView[2];
  VkDeviceMemory rtTextureImageMemory[2];
  VkPipelineLayout rtPipelineLayout;
  VkPipeline rtPipeline;
  VkBuffer rtSbtBuffer;
  VkDeviceMemory rtSbtMemory;
  VkStridedDeviceAddressRegionKHR rtRgenRegion{}, rtMissRegion{}, rtHitRegion{}, rtCallRegion{};
  VkBuffer ommTriangleIndicesBuffer;
  VkDeviceMemory ommTriangleIndicesMemory;
  VkBuffer ommArrayDataBuffer;
  VkDeviceMemory ommArrayDataMemory;
  VkBuffer ommDescArrayBuffer;
  VkDeviceMemory ommDescArrayMemory;
  VkBuffer ommBuffer;
  VkDeviceMemory ommMemory;

  bool framebufferResized = false;

  void initWindow() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetKeyCallback(window, keyCallback);
  }

  static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto app = reinterpret_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
  }

  void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
    createTextureImage();
    createTextureImageView();
    createTextureSampler();
    createVertexBuffer();
    createIndexBuffer();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();
    createAS();
    createRtOutputImage();
    createRtTextureImage();
    createRtTextureImageView();
    createRtDescriptorSetLayout();
    createRtDescriptorPool();
    createRtDescriptorSets();
    createRtPipeline();
    createRtSBT();
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window) && !g_should_exit) {
      glfwPollEvents();
      drawFrame();
    }

    vkDeviceWaitIdle(device);
  }

  void cleanupSwapChain() {
    for (auto framebuffer : swapChainFramebuffers) {
      vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    for (auto imageView : swapChainImageViews) {
      vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(device, swapChain, nullptr);
  }

  void cleanup() {
    vkDestroyPipeline(device, rtPipeline, nullptr);
    vkDestroyPipelineLayout(device, rtPipelineLayout, nullptr);
    vkFreeMemory(device, rtSbtMemory, nullptr);
    vkDestroyBuffer(device, rtSbtBuffer, nullptr);
    for (uint32_t i = 0; i < 2; i++) {
      vkDestroyImage(device, rtTextureImage[i], nullptr);
      vkDestroyImageView(device, rtTextureImageView[i], nullptr);
      vkFreeMemory(device, rtTextureImageMemory[i], nullptr);
    }
    vkDestroyDescriptorSetLayout(device, rtDescriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(device, rtDescriptorPool, nullptr);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vkDestroyImage(device, rtOutputImages[i], nullptr);
      vkDestroyImageView(device, rtOutputImageViews[i], nullptr);
      vkFreeMemory(device, rtOutputImageMemories[i], nullptr);
    }
    vkDestroyBuffer(device, blasResultBuffer, nullptr);
    vkFreeMemory(device, blasResultMemory, nullptr);
    PFN_vkDestroyAccelerationStructureKHR funcDestroyAccelerationStructureKHR =
      (PFN_vkDestroyAccelerationStructureKHR)vkGetInstanceProcAddr(
        instance, "vkDestroyAccelerationStructureKHR");
    assert(funcDestroyAccelerationStructureKHR);
    funcDestroyAccelerationStructureKHR(device, blas, nullptr);
    vkDestroyBuffer(device, tlasResultBuffer, nullptr);
    vkFreeMemory(device, tlasResultMemory, nullptr);
    funcDestroyAccelerationStructureKHR(device, tlas, nullptr);

    cleanupSwapChain();

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vkDestroyBuffer(device, uniformBuffers[i], nullptr);
      vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
    }

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);

    vkDestroySampler(device, textureSampler, nullptr);
    vkDestroyImageView(device, textureImageView, nullptr);

    vkDestroyImage(device, textureImage, nullptr);
    vkFreeMemory(device, textureImageMemory, nullptr);

    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    vkDestroyBuffer(device, indexBuffer, nullptr);
    vkFreeMemory(device, indexBufferMemory, nullptr);

    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
      vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
      vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyDevice(device, nullptr);

    if (enableValidationLayers) {
      DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }

    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);

    glfwTerminate();
  }

  void recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
      glfwGetFramebufferSize(window, &width, &height);
      glfwWaitEvents();
    }

    vkDeviceWaitIdle(device);

    cleanupSwapChain();

    createSwapChain();
    createImageViews();
    createFramebuffers();
  }

  void createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
      throw std::runtime_error("validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
      createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
      createInfo.ppEnabledLayerNames = validationLayers.data();

      populateDebugMessengerCreateInfo(debugCreateInfo);
      createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else {
      createInfo.enabledLayerCount = 0;

      createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
      throw std::runtime_error("failed to create instance!");
    }
  }

  void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
      VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
  }

  void setupDebugMessenger() {
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
      throw std::runtime_error("failed to set up debug messenger!");
    }
  }

  void createSurface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
      throw std::runtime_error("failed to create window surface!");
    }
  }

  void pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
      throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
      if (isDeviceSuitable(device)) {
        physicalDevice = device;
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        std::string name = props.deviceName;
        if (name.find("NVIDIA") != std::string::npos) {
          printf("Is NV GPU.\n");
          g_is_nv = true;
          deviceExtensions.push_back(VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME);
        }
        else {
          printf("Not NV GPU.\n");
        }
        break;
      }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
      throw std::runtime_error("failed to find a suitable GPU!");
    }
  }

  void createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

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

    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    std::vector<const char*> extensionNames = deviceExtensions;
    if (g_use_omm) {
      for (const char* x : ommExtensions) {
        extensionNames.push_back(x);
      }
    }
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensionNames.size());
    createInfo.ppEnabledExtensionNames = extensionNames.data();

    if (enableValidationLayers) {
      createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
      createInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else {
      createInfo.enabledLayerCount = 0;
    }

    VkPhysicalDeviceOpacityMicromapFeaturesEXT ommFeatures{};
    ommFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{};
    rtPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    if (g_use_omm) {
      rtPipelineFeatures.pNext = &ommFeatures;
    }

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
    accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelerationStructureFeatures.pNext = &rtPipelineFeatures;

    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR bufferDeviceAddressFeatures{};
    bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
    bufferDeviceAddressFeatures.pNext = &accelerationStructureFeatures;

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.features.samplerAnisotropy = true;

    VkPhysicalDeviceRayTracingValidationFeaturesNV rtValidationFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV };
    if (g_is_nv) {
      deviceFeatures2.pNext = &rtValidationFeatures;
      rtValidationFeatures.pNext = &bufferDeviceAddressFeatures;
      printf("Requesting RT validation features.\n");
    }
    else {
      deviceFeatures2.pNext = &bufferDeviceAddressFeatures;
    }

    vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures2);
    assert(bufferDeviceAddressFeatures.bufferDeviceAddress == true);
    if (g_is_nv) {
      assert(rtValidationFeatures.rayTracingValidation == true);
      printf("rtValidationFeatures.rayTracingValidation is true.\n");
    }

    createInfo.pNext = &deviceFeatures2;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
      throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
  }

  void createSwapChain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
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
      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    if (indices.graphicsFamily != indices.presentFamily) {
      createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      createInfo.queueFamilyIndexCount = 2;
      createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else {
      createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
      throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
  }

  void createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());

    for (size_t i = 0; i < swapChainImages.size(); i++) {
      swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat);
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

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
      throw std::runtime_error("failed to create render pass!");
    }
  }

  void createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create descriptor set layout!");
    }
  }

  void createGraphicsPipeline() {
    auto vertShaderCode = readFile("shaders/vert.spv");
    auto fragShaderCode = readFile("shaders/frag.spv");

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

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create pipeline layout!");
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

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
      throw std::runtime_error("failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
  }

  void createFramebuffers() {
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
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
        throw std::runtime_error("failed to create framebuffer!");
      }
    }
  }

  void createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
      throw std::runtime_error("failed to create graphics command pool!");
    }
  }

  void createTextureImage() {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load("textures/texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    if (!pixels) {
      throw std::runtime_error("failed to load texture image!");
    }

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);

    stbi_image_free(pixels);

    createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);

    transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
  }

  void createRtTextureImage() {
    for (uint32_t j = 0; j < 2; j++) {
      int texWidth, texHeight, texChannels;
      stbi_uc* pixels = stbi_load(tex_fns[j], &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
      VkDeviceSize imageSize = texWidth * texHeight * 4;

      if (!pixels) {
        throw std::runtime_error("failed to load texture image!");
      }

      VkBuffer stagingBuffer;
      VkDeviceMemory stagingBufferMemory;
      createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

      void* data;
      vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
      memcpy(data, pixels, static_cast<size_t>(imageSize));
      vkUnmapMemory(device, stagingBufferMemory);

      stbi_image_free(pixels);

      createImage(texWidth, texHeight,
        VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        rtTextureImage[j], rtTextureImageMemory[j]);

      transitionImageLayout(rtTextureImage[j], VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      copyBufferToImage(stagingBuffer, rtTextureImage[j], static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
      transitionImageLayout(rtTextureImage[j], VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      vkDestroyBuffer(device, stagingBuffer, nullptr);
      vkFreeMemory(device, stagingBufferMemory, nullptr);
    }
  }

  void createTextureImageView() {
    textureImageView = createImageView(textureImage, VK_FORMAT_R8G8B8A8_SRGB);
  }

  void createRtTextureImageView() {
    for (uint32_t j = 0; j < 2; j++) {
      rtTextureImageView[j] = createImageView(rtTextureImage[j], VK_FORMAT_R8G8B8A8_SRGB);
    }
  }

  void createTextureSampler() {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
      throw std::runtime_error("failed to create texture sampler!");
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

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

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

    vkCmdPipelineBarrier(
      commandBuffer,
      sourceStage, destinationStage,
      0,
      0, nullptr,
      0, nullptr,
      1, &barrier
    );

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
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = {
        width,
        height,
        1
    };

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    endSingleTimeCommands(commandBuffer);
  }

  void createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    createBuffer(bufferSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT |
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      vertexBuffer, vertexBufferMemory);

    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
  }

  void createIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    createBuffer(bufferSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT |
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);

    copyBuffer(stagingBuffer, indexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
  }

  void createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffers[i], uniformBuffersMemory[i]);

      vkMapMemory(device, uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
    }
  }

  void createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
      throw std::runtime_error("failed to create descriptor pool!");
    }
  }

  void createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      VkDescriptorBufferInfo bufferInfo{};
      bufferInfo.buffer = uniformBuffers[i];
      bufferInfo.offset = 0;
      bufferInfo.range = sizeof(UniformBufferObject);

      VkDescriptorImageInfo imageInfo{};
      imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageInfo.imageView = textureImageView;
      imageInfo.sampler = textureSampler;

      std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

      descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[0].dstSet = descriptorSets[i];
      descriptorWrites[0].dstBinding = 0;
      descriptorWrites[0].dstArrayElement = 0;
      descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorWrites[0].descriptorCount = 1;
      descriptorWrites[0].pBufferInfo = &bufferInfo;

      descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[1].dstSet = descriptorSets[i];
      descriptorWrites[1].dstBinding = 1;
      descriptorWrites[1].dstArrayElement = 0;
      descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      descriptorWrites[1].descriptorCount = 1;
      descriptorWrites[1].pImageInfo = &imageInfo;

      vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
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

  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
      if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
        return i;
      }
    }

    throw std::runtime_error("failed to find suitable memory type!");
  }

  void createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate command buffers!");
    }
  }

  void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = swapChainExtent;

    VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapChainExtent.width;
    viewport.height = (float)swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);

    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("failed to record command buffer!");
    }
  }

  void recordRtCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("Failed to begin command buffer");
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipelineLayout, 0,
      1, &(rtDescriptorSets[currentFrame]), 0, nullptr);
    PFN_vkCmdTraceRaysKHR funcCmdTraceRaysKHR =
      (PFN_vkCmdTraceRaysKHR)vkGetInstanceProcAddr(
        instance, "vkCmdTraceRaysKHR");
    funcCmdTraceRaysKHR(commandBuffer, &rtRgenRegion, &rtMissRegion, &rtHitRegion, &rtCallRegion, WIDTH, HEIGHT, 1);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = rtOutputImages[currentFrame];
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
      rtOutputImages[currentFrame], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
    barrier.image = rtOutputImages[currentFrame];
    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not end RT command buffer");
    }
  }

  void createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
        vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
        throw std::runtime_error("failed to create synchronization objects for a frame!");
      }
    }
  }

  void updateUniformBuffer(uint32_t currentImage) {
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    UniformBufferObject ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(glm::radians(45.0f), swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 10.0f);
    ubo.proj[1][1] *= -1;

    memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
  }

  void drawFrame() {
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      recreateSwapChain();
      return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      throw std::runtime_error("failed to acquire swap chain image!");
    }

    updateUniformBuffer(currentFrame);

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    vkResetCommandBuffer(commandBuffers[currentFrame], /*VkCommandBufferResetFlagBits*/ 0);

    if (!g_is_rt) {
      recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
    }
    else {
      recordRtCommandBuffer(commandBuffers[currentFrame], imageIndex);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
      throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = { swapChain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;

    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
      framebufferResized = false;
      recreateSwapChain();
    }
    else if (result != VK_SUCCESS) {
      throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
  }

  VkDeviceAddress getBufferDeviceAddress(VkBuffer& buf) {
    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.buffer = buf;
    addrInfo.pNext = nullptr;
    return vkGetBufferDeviceAddress(device, &addrInfo);
  }

  void createAS() {
    VkAccelerationStructureGeometryKHR geomData{};
    geomData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geomData.flags = 0;
    geomData.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;

    VkAccelerationStructureGeometryTrianglesDataKHR& triASData = geomData.geometry.triangles;
    triASData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triASData.vertexFormat = VK_FORMAT_R32G32_SFLOAT;
    triASData.vertexData.deviceAddress = getBufferDeviceAddress(vertexBuffer);
    triASData.vertexStride = sizeof(Vertex);
    triASData.indexType = VK_INDEX_TYPE_UINT32;
    triASData.indexData.deviceAddress = getBufferDeviceAddress(indexBuffer);
    triASData.maxVertex = indices.size() - 1;
    triASData.transformData.hostAddress = nullptr;

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = indices.size() / 3;

    VkAccelerationStructureBuildGeometryInfoKHR buildGeomInfo{};
    buildGeomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildGeomInfo.flags = 0;
    buildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildGeomInfo.srcAccelerationStructure = NULL;
    buildGeomInfo.dstAccelerationStructure = NULL;
    buildGeomInfo.geometryCount = 1;
    buildGeomInfo.pGeometries = &geomData;

    VkMicromapEXT omm{};
    VkAccelerationStructureTrianglesOpacityMicromapEXT ommBlasDesc{};
    VkMicromapUsageEXT ommUsage{};
    const omm::Cpu::BakeResultDesc* res_desc{};

    // OMM
    if (g_use_omm) {
      res_desc = bakeOmmForMask();

      // OMM Buffers
      // 1. Triangle indices buffer
      size_t size = res_desc->indexCount * sizeof(uint32_t);
      createBuffer(size,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ommTriangleIndicesBuffer,
        ommTriangleIndicesMemory);
      void* data;
      vkMapMemory(device, ommTriangleIndicesMemory, 0, size, 0, &data);
      memcpy(data, res_desc->indexBuffer, size);
      vkUnmapMemory(device, ommTriangleIndicesMemory);
      printf("Triangle indices size: %u, addr=%p\n", size, (void*)getBufferDeviceAddress(ommTriangleIndicesBuffer));

      // 2. OMM array buffer
      size = res_desc->arrayDataSize;
      createBuffer(size,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ommArrayDataBuffer,
        ommArrayDataMemory);
      vkMapMemory(device, ommArrayDataMemory, 0, size, 0, &data);
      memcpy(data, res_desc->arrayData, size);
      vkUnmapMemory(device, ommArrayDataMemory);
      printf("OMM array size: %u, addr=%p\n", size, (void*)getBufferDeviceAddress(ommArrayDataBuffer));

      // 3. OMM descriptor array buffer
      size = res_desc->descArrayCount * sizeof(VkMicromapTriangleEXT);
      createBuffer(size,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ommDescArrayBuffer,
        ommDescArrayMemory);
      vkMapMemory(device, ommDescArrayMemory, 0, size, 0, &data);
      memcpy(data, res_desc->descArray, size);
      vkUnmapMemory(device, ommDescArrayMemory);
      printf("OMM descriptor array size: %u, addr=%p\n", size, (void*)getBufferDeviceAddress(ommDescArrayBuffer));

      // 4. OMM build info
      VkMicromapBuildInfoEXT ommBuildInfo{};
      ommBuildInfo.sType = VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT;
      ommBuildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
      ommBuildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
      ommBuildInfo.dstMicromap = NULL;
      ommBuildInfo.usageCountsCount = res_desc->indexHistogramCount;
      ommBuildInfo.pUsageCounts = (VkMicromapUsageEXT*)(res_desc->indexHistogram);
      ommBuildInfo.data.deviceAddress = getBufferDeviceAddress(ommArrayDataBuffer);
      ommBuildInfo.scratchData.deviceAddress = NULL;
      ommBuildInfo.triangleArray.deviceAddress = getBufferDeviceAddress(ommDescArrayBuffer);
      ommBuildInfo.triangleArrayStride = sizeof(VkMicromapTriangleEXT);
      PFN_vkGetMicromapBuildSizesEXT funcGetMicromapBuildSizes =
        (PFN_vkGetMicromapBuildSizesEXT)vkGetInstanceProcAddr(
          instance, "vkGetMicromapBuildSizesEXT");
      VkMicromapBuildSizesInfoEXT ommBuildSizes{};
      ommBuildSizes.sType = VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT;
      funcGetMicromapBuildSizes(device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &ommBuildInfo, &ommBuildSizes);

      printf("OMM scratch size: %u, micromap size: %u\n",
        ommBuildSizes.buildScratchSize, ommBuildSizes.micromapSize);

      VkBuffer ommScratchBuffer{};
      VkDeviceMemory ommScratchMemory{};

      // 5. OMM buffer and scratch buffer
      createBuffer(ommBuildSizes.buildScratchSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ommScratchBuffer,
        ommScratchMemory);
      createBuffer(ommBuildSizes.micromapSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ommBuffer,
        ommMemory);

      VkMicromapCreateInfoEXT ommCreateInfo{};
      ommCreateInfo.sType = VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT;
      ommCreateInfo.createFlags = 0;
      ommCreateInfo.buffer = ommBuffer;
      ommCreateInfo.offset = 0;
      ommCreateInfo.size = ommBuildSizes.micromapSize;
      ommCreateInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
      ommCreateInfo.deviceAddress = 0;
      auto func_vkCreateMicromap = (PFN_vkCreateMicromapEXT)vkGetInstanceProcAddr(instance, "vkCreateMicromapEXT");
      if (func_vkCreateMicromap(device, &ommCreateInfo, nullptr, &omm) != VK_SUCCESS) {
        printf("Failed to create VkMicromap\n");
      }

      ommBuildInfo.dstMicromap = omm;
      ommBuildInfo.data.deviceAddress = getBufferDeviceAddress(ommArrayDataBuffer);
      ommBuildInfo.scratchData.deviceAddress = getBufferDeviceAddress(ommScratchBuffer);
      ommBuildInfo.triangleArray.deviceAddress = getBufferDeviceAddress(ommDescArrayBuffer);

      VkCommandBuffer commandBuffer = beginSingleTimeCommands();
      PFN_vkCmdBuildMicromapsEXT funcCmdBuildMicromaps =
        (PFN_vkCmdBuildMicromapsEXT)vkGetInstanceProcAddr(
          instance, "vkCmdBuildMicromapsEXT");
      funcCmdBuildMicromaps(commandBuffer, 1, &ommBuildInfo);

      VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      barrier.srcQueueFamilyIndex = (~0U);
      barrier.dstQueueFamilyIndex = (~0U);
      barrier.buffer = ommScratchBuffer;
      barrier.offset = 0;
      barrier.size = ommBuildSizes.buildScratchSize;
      vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 1, &barrier, 0, nullptr);
      endSingleTimeCommands(commandBuffer);


      // 6. AS Triangles OMM
      ommBlasDesc.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT;
      ommBlasDesc.indexType = VK_INDEX_TYPE_UINT32;
      ommBlasDesc.indexBuffer.deviceAddress = getBufferDeviceAddress(ommTriangleIndicesBuffer);
      ommBlasDesc.indexStride = sizeof(uint32_t);
      ommBlasDesc.baseTriangle = 0;
      ommBlasDesc.usageCountsCount = res_desc->indexHistogramCount;
      ommBlasDesc.pUsageCounts = (VkMicromapUsageEXT*)(res_desc->indexHistogram);
      ommBlasDesc.micromap = omm;
      
      triASData.pNext = &ommBlasDesc;

      vkDestroyBuffer(device, ommScratchBuffer, nullptr);
      vkFreeMemory(device, ommScratchMemory, nullptr);
    }

    VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo{};
    buildSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR funcGetAccelerationStructureBuildSizes =
      (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetInstanceProcAddr(
        instance, "vkGetAccelerationStructureBuildSizesKHR");
    assert(funcGetAccelerationStructureBuildSizes);
    uint32_t primCount = indices.size() / 3;
    funcGetAccelerationStructureBuildSizes(device,
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &buildGeomInfo,
      &primCount,
      &buildSizeInfo);
    printf("BLAS build size: scratch=%u, AS=%u, upd=%u\n",
      buildSizeInfo.buildScratchSize,
      buildSizeInfo.accelerationStructureSize,
      buildSizeInfo.updateScratchSize);

    // BLAS scratch
    VkBuffer blasScratchBuffer{};
    VkDeviceMemory blasScratchMemory{};
    createBuffer(buildSizeInfo.buildScratchSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      blasScratchBuffer, blasScratchMemory);

    // BLAS result
    createBuffer(buildSizeInfo.accelerationStructureSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      blasResultBuffer, blasResultMemory);

    VkAccelerationStructureCreateInfoKHR blasCreateInfo{};
    blasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    blasCreateInfo.size = buildSizeInfo.accelerationStructureSize;
    blasCreateInfo.buffer = blasResultBuffer;
    blasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    PFN_vkCreateAccelerationStructureKHR funcCreateAccelerationStructure =
      (PFN_vkCreateAccelerationStructureKHR)vkGetInstanceProcAddr(
        instance, "vkCreateAccelerationStructureKHR");
    if (funcCreateAccelerationStructure(device, &blasCreateInfo, nullptr, &blas) != VK_SUCCESS) {
      throw std::runtime_error("Could not create BLAS");
    }

    buildGeomInfo.dstAccelerationStructure = blas;
    buildGeomInfo.scratchData.deviceAddress = getBufferDeviceAddress(blasScratchBuffer);

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

    // TLAS
    VkBuffer tlasInstancesBuffer{};
    VkDeviceMemory tlasInstancesMemory{};
    size_t instBufSize = sizeof(VkAccelerationStructureInstanceKHR) * 1;
    createBuffer(instBufSize,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      tlasInstancesBuffer,
      tlasInstancesMemory);
    
    VkDeviceAddress blasASAddress{};
    VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo{};
    blasAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    blasAddrInfo.accelerationStructure = blas;
    PFN_vkGetAccelerationStructureDeviceAddressKHR funcGetAccelerationStructureDeviceAddressKHR =
      (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetInstanceProcAddr(
        instance, "vkGetAccelerationStructureDeviceAddressKHR");
    blasASAddress = funcGetAccelerationStructureDeviceAddressKHR(device, &blasAddrInfo);

    printf("blasASAddress=%p blasResultDeviceAddr=%p\n",
      (void*)blasASAddress,
      (void*)getBufferDeviceAddress(blasResultBuffer));

    void* data;
    VkAccelerationStructureInstanceKHR instance0{};
    instance0.accelerationStructureReference = blasASAddress;
    instance0.transform.matrix[0][0] = 1.0f;
    instance0.transform.matrix[1][1] = 1.0f;
    instance0.transform.matrix[2][2] = 1.0f;
    instance0.instanceCustomIndex = 0;
    instance0.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance0.mask = 0xFF;
    instance0.instanceShaderBindingTableRecordOffset = 0;

    vkMapMemory(device, tlasInstancesMemory, 0, instBufSize, 0, &data);
    memcpy(data, &instance0, sizeof(instance0));
    vkUnmapMemory(device, tlasInstancesMemory);

    VkAccelerationStructureGeometryKHR instData{};
    instData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    instData.flags = 0;// VK_GEOMETRY_OPAQUE_BIT_KHR;
    instData.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    instData.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instData.geometry.instances.data.deviceAddress = getBufferDeviceAddress(tlasInstancesBuffer);

    // TLAS inst info
    VkAccelerationStructureBuildGeometryInfoKHR buildInstInfo{};
    buildInstInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInstInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInstInfo.flags = 0;
    buildInstInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInstInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    buildInstInfo.dstAccelerationStructure = VK_NULL_HANDLE;
    buildInstInfo.geometryCount = 1;
    buildInstInfo.pGeometries = &instData;
    buildInstInfo.ppGeometries = nullptr;
    buildInstInfo.scratchData.deviceAddress = 0;

    uint32_t instCount = 1;
    VkAccelerationStructureBuildSizesInfoKHR tlasBuildSizeInfo{};
    tlasBuildSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    funcGetAccelerationStructureBuildSizes(device,
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &buildInstInfo,
      &instCount,
      &tlasBuildSizeInfo);
    printf("TLAS build size: scratch=%llu, AS=%llu\n",
      tlasBuildSizeInfo.buildScratchSize,
      tlasBuildSizeInfo.accelerationStructureSize);

    // TLAS scratch
    VkBuffer tlasScratchBuffer{};
    VkDeviceMemory tlasScratchMemory{};
    createBuffer(tlasBuildSizeInfo.buildScratchSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      tlasScratchBuffer,
      tlasScratchMemory);
 
    // TLAS result
    createBuffer(tlasBuildSizeInfo.accelerationStructureSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      tlasResultBuffer,
      tlasResultMemory);

    VkAccelerationStructureCreateInfoKHR tlasCreateInfo{};
    tlasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    tlasCreateInfo.size = tlasBuildSizeInfo.accelerationStructureSize;
    tlasCreateInfo.buffer = tlasResultBuffer;
    tlasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (funcCreateAccelerationStructure(device, &tlasCreateInfo, nullptr, &tlas) != VK_SUCCESS) {
      throw std::runtime_error("Could not create TLAS");
    }

    buildRangeInfo.primitiveCount = 1;
    buildInstInfo.dstAccelerationStructure = tlas;
    buildInstInfo.scratchData.deviceAddress = getBufferDeviceAddress(tlasScratchBuffer);

    commandBuffer = beginSingleTimeCommands();
    funcCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInstInfo, buildRangeInfos);
    vkCmdPipelineBarrier(commandBuffer,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      0, 1, &barrier,
      0, nullptr,
      0, nullptr);
    endSingleTimeCommands(commandBuffer);

    vkDestroyBuffer(device, blasScratchBuffer, nullptr);
    vkFreeMemory(device, blasScratchMemory, nullptr);
    vkDestroyBuffer(device, tlasInstancesBuffer, nullptr);
    vkFreeMemory(device, tlasInstancesMemory, nullptr);
    vkDestroyBuffer(device, tlasScratchBuffer, nullptr);
    vkFreeMemory(device, tlasScratchMemory, nullptr);
  }

  void createRtOutputImage() {
    size_t N = swapChainImages.size();

    rtOutputImages.resize(N);
    rtOutputImageMemories.resize(N);
    rtOutputImageViews.resize(N);

    for (uint32_t i = 0; i < N; i++) {
      createImage(WIDTH, HEIGHT,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT,
        0, rtOutputImages[i], rtOutputImageMemories[i]);
      rtOutputImageViews[i] = createImageView(rtOutputImages[i], VK_FORMAT_R32G32B32A32_SFLOAT);
      transitionImageLayout(rtOutputImages[i],
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    }
  }

  void createRtDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[6]{};
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

    descriptorSetLayoutBindings[5].binding = 5;
    descriptorSetLayoutBindings[5].descriptorCount = 1;
    descriptorSetLayoutBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBindings[5].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[5].stageFlags =
      VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = _countof(descriptorSetLayoutBindings);
    descriptorSetLayoutInfo.pBindings = descriptorSetLayoutBindings;
    if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, nullptr, &rtDescriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create RT descriptor set layout");
    }
  }

  void createRtDescriptorPool() {
    const unsigned N = swapChainImages.size();
    VkDescriptorPoolSize poolSizes[6]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[0].descriptorCount = N;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = N;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = N;
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[3].descriptorCount = N;
    poolSizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[4].descriptorCount = N;
    poolSizes[5].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[5].descriptorCount = N;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = _countof(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = N;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &rtDescriptorPool) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create RT descriptor pool");
    }
  }

  void createRtDescriptorSets() {
    const uint32_t N = MAX_FRAMES_IN_FLIGHT;
    rtDescriptorSets.resize(N);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = rtDescriptorPool;
    allocInfo.descriptorSetCount = N;
    std::vector<VkDescriptorSetLayout> layouts(N, rtDescriptorSetLayout);
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &allocInfo, rtDescriptorSets.data()) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate RT descriptor sets");
    }

    VkWriteDescriptorSet writeDesc[6 * N]{};
    VkWriteDescriptorSetAccelerationStructureKHR writeDescAS{};
    // Update TLAS to descriptor set
    writeDescAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    writeDescAS.accelerationStructureCount = 1;
    writeDescAS.pAccelerationStructures = &tlas;

    VkDescriptorImageInfo outImageInfo[N]{};
    for (uint32_t f = 0; f < N; f++) {
      outImageInfo[f].imageView = rtOutputImageViews[f];
      outImageInfo[f].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    VkDescriptorImageInfo texImageInfo{};
    texImageInfo.imageView = rtTextureImageView[0];
    texImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    texImageInfo.sampler = textureSampler;

    VkDescriptorImageInfo maskImageInfo{};
    maskImageInfo.imageView = rtTextureImageView[1];
    maskImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    maskImageInfo.sampler = textureSampler;

    VkDescriptorBufferInfo bi{};
    bi.buffer = vertexBuffer;
    bi.offset = 0;
    bi.range = sizeof(Vertex) * vertices.size();

    VkDescriptorBufferInfo ibi{};
    ibi.buffer = indexBuffer;
    ibi.offset = 0;
    ibi.range = sizeof(indices[0]) * indices.size();

    for (uint32_t f = 0; f < N; f++) {
      uint32_t idx = f * 6;
      // TLAS
      writeDesc[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDesc[idx].dstSet = rtDescriptorSets[f];
      writeDesc[idx].dstBinding = 0;
      writeDesc[idx].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
      writeDesc[idx].descriptorCount = 1;
      writeDesc[idx].pNext = &writeDescAS;

      // Output images
      idx++;
      writeDesc[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDesc[idx].dstSet = rtDescriptorSets[f];
      writeDesc[idx].dstBinding = 1;
      writeDesc[idx].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      writeDesc[idx].descriptorCount = 1;
      writeDesc[idx].pImageInfo = &(outImageInfo[f]);

      idx++;
      writeDesc[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDesc[idx].dstSet = rtDescriptorSets[f];
      writeDesc[idx].dstBinding = 2;
      writeDesc[idx].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writeDesc[idx].descriptorCount = 1;
      writeDesc[idx].pImageInfo = &(texImageInfo);

      idx++;
      writeDesc[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDesc[idx].dstSet = rtDescriptorSets[f];
      writeDesc[idx].dstBinding = 3;
      writeDesc[idx].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writeDesc[idx].descriptorCount = 1;
      writeDesc[idx].pImageInfo = &(maskImageInfo);

      idx++;
      writeDesc[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDesc[idx].dstSet = rtDescriptorSets[f];
      writeDesc[idx].dstBinding = 4;
      writeDesc[idx].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writeDesc[idx].descriptorCount = 1;
      writeDesc[idx].pBufferInfo = &bi;

      idx++;
      writeDesc[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDesc[idx].dstSet = rtDescriptorSets[f];
      writeDesc[idx].dstBinding = 5;
      writeDesc[idx].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writeDesc[idx].descriptorCount = 1;
      writeDesc[idx].pBufferInfo = &ibi;
    }

    vkUpdateDescriptorSets(device, _countof(writeDesc), writeDesc, 0, nullptr);
  }

  void createRtPipeline() {
    // Layout
    VkPipelineLayoutCreateInfo rtPipelineLayoutInfo{};
    rtPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    rtPipelineLayoutInfo.setLayoutCount = 1;
    rtPipelineLayoutInfo.pSetLayouts = &rtDescriptorSetLayout;
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
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtprops{};
    rtprops.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    props2.pNext = &rtprops;

    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);
    printf("SBT shaderGroupBaseAlignment   : %u\n", rtprops.shaderGroupBaseAlignment);   // 64
    printf("SBT shaderGroupHandleAlignment : %u\n", rtprops.shaderGroupHandleAlignment); // 32
    printf("SBT shaderGroupHandleSize      : %u\n", rtprops.shaderGroupHandleSize);      // 32

    const size_t sbtSize = 32;  // arbitrarily chosen
    const size_t sbtAlignment = 64;
    const size_t numSBTs = 3;   // Rgen, closest-hit, miss

    createBuffer(sbtAlignment * numSBTs,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
      | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      rtSbtBuffer, rtSbtMemory);

    char shaderGroupHandle[sbtSize * numSBTs]{};
    PFN_vkGetRayTracingShaderGroupHandlesKHR funcGetRayTracingShaderGroupHandlesKHR =
      (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetInstanceProcAddr(
        instance, "vkGetRayTracingShaderGroupHandlesKHR");
    if (funcGetRayTracingShaderGroupHandlesKHR(device, rtPipeline, 0, numSBTs, sbtSize * numSBTs, shaderGroupHandle) != VK_SUCCESS) {
      throw std::runtime_error("Could not get RT shader group handles");
    }

    const char* groupTypes[] = { "RGen", "Hit", "Miss" };
    for (uint32_t i = 0; i < 3; i++) {
      printf("Handle of %5s:", groupTypes[i]);
      for (uint32_t j = 0; j < sbtSize; j++) {
        printf(" %02x", (0xFF & shaderGroupHandle[i * sbtSize + j]));
      }
      printf("\n");
    }

    uint8_t* mapped{};
    vkMapMemory(device, rtSbtMemory, 0, sbtAlignment * numSBTs, 0, (void**)&mapped);
    memcpy(mapped, shaderGroupHandle, sbtSize);  // rgen
    memcpy(mapped + sbtAlignment, shaderGroupHandle + sbtSize, sbtSize);  // hit
    memcpy(mapped + sbtAlignment * 2, shaderGroupHandle + sbtSize * 2, sbtSize);  // miss
    vkUnmapMemory(device, rtSbtMemory);

    VkDeviceAddress rtSbtDeviceAddress = getBufferDeviceAddress(rtSbtBuffer);

    // Stolen from ChatGPT
    rtRgenRegion.deviceAddress = rtSbtDeviceAddress;
    rtRgenRegion.size = sbtSize;
    rtRgenRegion.stride = sbtSize;

    rtHitRegion.deviceAddress = rtSbtDeviceAddress + sbtAlignment;
    rtHitRegion.size = sbtSize;
    rtHitRegion.stride = sbtSize;

    rtMissRegion.deviceAddress = rtSbtDeviceAddress + sbtAlignment * 2;
    rtMissRegion.size = sbtSize;
    rtMissRegion.stride = sbtSize;
  }

  VkShaderModule createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
      throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
  }

  VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
      if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return availableFormat;
      }
    }

    return availableFormats[0];
  }

  VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
      if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return availablePresentMode;
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

      VkExtent2D actualExtent = {
          static_cast<uint32_t>(width),
          static_cast<uint32_t>(height)
      };

      actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
      actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

      return actualExtent;
    }
  }

  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0) {
      details.formats.resize(formatCount);
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
      details.presentModes.resize(presentModeCount);
      vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
  }

  bool isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);

    bool extensionsSupported = checkDeviceExtensionSupport(device);
    bool omm = checkOmmExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
      SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
      swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    if (omm) {
      g_use_omm = true;
      printf("Device supports OMM.\n");
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
  }

  bool do_checkDeviceExtensionSupport(VkPhysicalDevice device, std::vector<const char*> devExts) {
    uint32_t extensionCount;
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

  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
      if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        indices.graphicsFamily = i;
      }

      VkBool32 presentSupport = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

      if (presentSupport) {
        indices.presentFamily = i;
      }

      if (indices.isComplete()) {
        break;
      }

      i++;
    }

    return indices;
  }

  std::vector<const char*> getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers) {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
  }

  bool checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
      bool layerFound = false;

      for (const auto& layerProperties : availableLayers) {
        if (strcmp(layerName, layerProperties.layerName) == 0) {
          layerFound = true;
          break;
        }
      }

      if (!layerFound) {
        return false;
      }
    }

    return true;
  }

  static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
      throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;
  }

  static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
  }
};

int main() {
  HelloTriangleApplication app;

  try {
    app.run();
  }
  catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
