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

GLFWwindow* window{};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

const uint32_t MAX_FRAMES_IN_FLIGHT = 3;
const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

std::vector<const char*> validationLayers = {
  "VK_LAYER_KHRONOS_validation"
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
  //VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME,
  VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME,

  VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME
};

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

void initWindow();

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

struct QueueFamilyIndices {
  std::optional<uint32_t> graphicsAndComputeFamily;
  std::optional<uint32_t> presentFamily;
  bool isComplete() {
    return graphicsAndComputeFamily.has_value() && presentFamily.has_value();
  }
};

struct RtPerSceneUniformBuffer {
  glm::mat4 inv_view, inv_proj;
};

class HelloLSSApplication {
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
    createSwapChain();
    createImageViews();
    createRenderPass();
    createCommandPool();
    createCommandBuffer();
    createAS();

    createRtUniformBuffer();
    createRtOutputImages();
    createRtDescriptorSetLayout();
    createRtDescriptorPool();
    createRtDescriptorSets();

    createRtPipeline();
    createRtSBT();

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

  void cleanup() {
    vkFreeMemory(device, lssIndexMemory, nullptr);
    vkFreeMemory(device, lssRadiusMemory, nullptr);
    vkFreeMemory(device, lssVertexMemory, nullptr);

    PFN_vkDestroyAccelerationStructureKHR funcDestroyAccelerationStructureKHR =
      (PFN_vkDestroyAccelerationStructureKHR)vkGetInstanceProcAddr(
        instance, "vkDestroyAccelerationStructureKHR");
    assert(funcDestroyAccelerationStructureKHR);
    funcDestroyAccelerationStructureKHR(device, blas, nullptr);
    funcDestroyAccelerationStructureKHR(device, tlas, nullptr);
  }

  void createInstance() {
    if (enableValidationLayers) {
      bool ok = checkValidationLayerSupport();
      printf("Validation layers enabled, check result=%d\n", ok);
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello LSS";
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
    VkPhysicalDeviceRayTracingPropertiesNV rtProps{};
    rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;
    rtPipelineProps.pNext = &rtProps;
    vkGetPhysicalDeviceProperties2(physicalDevice, &prop2);

    uint32_t extensionCount{};
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());
    printf("%u device extensions\n", extensionCount);

    printf("Ray tracing properties:\n");
    printf("shaderGroupHandleSize = %u\n", rtProps.shaderGroupHandleSize);
    printf("maxRecursionDepth     = %u\n", rtProps.maxRecursionDepth);
    printf("maxShaderGroupStride  = %u\n", rtProps.maxShaderGroupStride);
    printf("shaderGroupBaseAlignment = %u\n", rtProps.shaderGroupBaseAlignment);
    printf("maxGeometryCount      = %u\n", rtProps.maxGeometryCount);
    printf("maxInstanceCount      = %u\n", rtProps.maxInstanceCount);
    printf("maxTriangleCount      = %u\n", rtProps.maxTriangleCount);
    printf("maxDescriptorSetAccelerationStructures = %u\n", rtProps.maxDescriptorSetAccelerationStructures);
  }

  bool isDeviceSuitable(VkPhysicalDevice device) {
    return true;
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
      if ((qp.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
          (qp.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        indices.graphicsAndComputeFamily = idx;
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
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsAndComputeFamily.value(), indices.presentFamily.value() };

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
    ommFeatures.pNext = nullptr;// &rtValidationFeatures;

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
    //assert(rtValidationFeatures.rayTracingValidation);

    // RT and buffer device address
    createInfo.pNext = &deviceFeatures2;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create logical device");
    }

    vkGetDeviceQueue(device, indices.graphicsAndComputeFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
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
    uint32_t queueFamilyIndices[] = { indices.graphicsAndComputeFamily.value(), indices.presentFamily.value() };
    if (indices.graphicsAndComputeFamily != indices.presentFamily) {
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

  void createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);
    VkCommandPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.queueFamilyIndex = queueFamilyIndices.graphicsAndComputeFamily.value();
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

  VkAccelerationStructureKHR buildBLAS(VkBuffer& outBlasResultBuffer, VkDeviceMemory& outBlasResultMemory) {
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

  VkDeviceAddress getBufferDeviceAddress(VkBuffer& buf) {
    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.buffer = buf;
    addrInfo.pNext = nullptr;
    return vkGetBufferDeviceAddress(device, &addrInfo);
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

  void createAS() {
    glm::vec3 lssVerts[] = {
      glm::vec3(-0.5, 0, 0),
      glm::vec3(0.5, 0, 0),
      glm::vec3(0, 0.5, 0)
    };
    size_t lssVertsSize = sizeof(lssVerts);
    createBuffer(lssVertsSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      lssVertexBuffer, lssVertexMemory);
    uint8_t* mapped{};
    vkMapMemory(device, lssVertexMemory, 0, lssVertsSize, 0, (void**)&mapped);
    memcpy(mapped, lssVerts, lssVertsSize);
    vkUnmapMemory(device, lssVertexMemory);

    float lssRadii[] = {
      0.2f, 0.15f, 0.10f
    };
    size_t lssRadiiSize = sizeof(lssRadii);
    createBuffer(lssRadiiSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      lssRadiusBuffer, lssRadiusMemory);
    vkMapMemory(device, lssRadiusMemory, 0, lssRadiiSize, 0, (void**)&mapped);
    memcpy(mapped, lssRadii, lssRadiiSize);
    vkUnmapMemory(device, lssRadiusMemory);

    uint32_t lssIdxes[] = { 0, 1 };
    size_t lssIdxesSize = sizeof(lssIdxes);
    createBuffer(lssIdxesSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      lssIndexBuffer, lssIndexMemory);
    vkMapMemory(device, lssIndexMemory, 0, lssIdxesSize, 0, (void**)&mapped);
    memcpy(mapped, lssIdxes, lssIdxesSize);
    vkUnmapMemory(device, lssIndexMemory);

    VkAccelerationStructureGeometryLinearSweptSpheresDataNV lssASData{};
    lssASData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_LINEAR_SWEPT_SPHERES_DATA_NV;
    lssASData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    lssASData.vertexData.deviceAddress = getBufferDeviceAddress(lssVertexBuffer);
    lssASData.vertexStride = sizeof(glm::vec3);
    lssASData.radiusFormat = VK_FORMAT_R32_SFLOAT;
    lssASData.radiusData.deviceAddress = getBufferDeviceAddress(lssRadiusBuffer);
    lssASData.radiusStride = sizeof(float);
    lssASData.indexType = VK_INDEX_TYPE_UINT32;
    lssASData.indexData.deviceAddress = getBufferDeviceAddress(lssIndexBuffer);
    lssASData.indexStride = sizeof(uint32_t);
    lssASData.indexingMode = VK_RAY_TRACING_LSS_INDEXING_MODE_SUCCESSIVE_NV;
    lssASData.endCapsMode = VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_NONE_NV;

    VkAccelerationStructureGeometryKHR geomData{};
    geomData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geomData.geometryType = VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV;
    geomData.pNext = &lssASData;
    geomData.flags = 0;

    uint32_t primCount = _countof(lssIdxes);

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.primitiveCount = primCount;
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

    createBuffer(asBuildSizeInfo.accelerationStructureSize,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
      0,
      blasResultBuffer,
      blasResultMemory);

    buildGeomInfo.scratchData.deviceAddress = getBufferDeviceAddress(blasScratchBuffer);

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

    // Requires the VK_NV_ray_tracing_linear_swept_spheres extension to build
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

    vkFreeMemory(device, blasScratchMemory, nullptr);
    vkDestroyBuffer(device, blasScratchBuffer, nullptr);

    // tlas
    size_t size = sizeof(VkAccelerationStructureInstanceKHR) * 2;
    createBuffer(size,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      tlasInstancesBuffer, tlasInstancesMemory);

    VkDeviceAddress blas0ResultDeviceAddr = getBufferDeviceAddress(blasResultBuffer);

    VkDeviceAddress blas0ASAddress{};
    VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo{};
    blasAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    blasAddrInfo.accelerationStructure = blas;
    PFN_vkGetAccelerationStructureDeviceAddressKHR funcGetAccelerationStructureDeviceAddressKHR =
      (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetInstanceProcAddr(
        instance, "vkGetAccelerationStructureDeviceAddressKHR");
    blas0ASAddress = funcGetAccelerationStructureDeviceAddressKHR(device, &blasAddrInfo);
    
    void* data;
    VkAccelerationStructureInstanceKHR instances[1]{};
    instances[0].accelerationStructureReference = blas0ASAddress;
    instances[0].transform.matrix[0][0] = 1.0f;
    instances[0].transform.matrix[1][1] = 1.0f;
    instances[0].transform.matrix[2][2] = 1.0f;
    instances[0].instanceCustomIndex = 0;
    instances[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instances[0].mask = 0xFF;
    instances[0].instanceShaderBindingTableRecordOffset = 0;

    vkMapMemory(device, tlasInstancesMemory, 0, size, 0, &data);
    memcpy(data, &instances[0], size);
    vkUnmapMemory(device, tlasInstancesMemory);

    VkAccelerationStructureGeometryKHR instData[1]{};
    instData[0].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    instData[0].flags = 0;// VK_GEOMETRY_OPAQUE_BIT_KHR;
    instData[0].geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    instData[0].geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instData[0].geometry.instances.data.deviceAddress = getBufferDeviceAddress(tlasInstancesBuffer);

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
    funcGetAccelerationStructureBuildSizes(device,
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &buildInstInfo,
      &instCount,
      &tlasBuildSizeInfo);
    printf("TLAS build size: scratch=%llu, AS=%llu\n",
      tlasBuildSizeInfo.buildScratchSize,
      tlasBuildSizeInfo.accelerationStructureSize);

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
    if (funcCreateAccelerationStructure(device, &tlasCreateInfo, nullptr, &tlas) != VK_SUCCESS) {
      throw std::runtime_error("Could not create TLAS");
    }

    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.primitiveCount = 1;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.transformOffset = 0;
    buildInstInfo.scratchData.deviceAddress = getBufferDeviceAddress(tlasScratchBuffer);
    buildInstInfo.dstAccelerationStructure = tlas;
    VkAccelerationStructureBuildRangeInfoKHR* const buildRangeInfos1[] = { &buildRangeInfo };

    commandBuffer = beginSingleTimeCommands();
    funcCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInstInfo, buildRangeInfos1);

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

  void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    // Update descriptor
    {
      VkWriteDescriptorSet writes[2]{};
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

      VkWriteDescriptorSetAccelerationStructureKHR writeAS{};
      writeAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
      writeAS.accelerationStructureCount = 1;
      writeAS.pAccelerationStructures = &tlas;
      writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[1].descriptorCount = 1;
      writes[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
      writes[1].dstArrayElement = 0;
      writes[1].dstBinding = 0;
      writes[1].dstSet = rtDescriptorSet;
      writes[1].pNext = &writeAS;

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

    {
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
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not end command buffer");
    }
  }

  void drawFrame() {
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);

    uint32_t imageIndex{};
    vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    vkResetCommandBuffer(commandBuffer, 0);

    recordCommandBuffer(commandBuffer, imageIndex);

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
    VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[2]{};
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

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = _countof(descriptorSetLayoutBindings);
    descriptorSetLayoutInfo.pBindings = descriptorSetLayoutBindings;

    if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, nullptr, &rtDescriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("Could not create RT descriptor set layout");
    }
  }

  void createRtDescriptorPool() {
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

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

  void createRtPipeline() {
    VkPipelineLayoutCreateInfo rtPipelineLayoutInfo{};
    rtPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    rtPipelineLayoutInfo.setLayoutCount = 1;
    rtPipelineLayoutInfo.pSetLayouts = &rtDescriptorSetLayout;
    if (vkCreatePipelineLayout(device, &rtPipelineLayoutInfo, nullptr, &rtPipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("Could not create RT pipeline layout");
    }

    VkPipelineShaderStageCreateInfo stages[3]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].pName = "main";  // entry point name
    VkShaderModule raygenShaderModule = createShaderModule(readFile("shaders/rgen.spv"));
    stages[0].module = raygenShaderModule;
    stages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].pName = "main";  // entry point name
    VkShaderModule rmissShaderModule = createShaderModule(readFile("shaders/rmiss.spv"));
    stages[1].module = rmissShaderModule;
    stages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;

    stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[2].pName = "main";  // entry point name
    VkShaderModule rchitShaderModule = createShaderModule(readFile("shaders/rchit.spv"));
    stages[2].module = rchitShaderModule;
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
    shaderGroupInfos[2].generalShader = VK_SHADER_UNUSED_KHR;  // Miss
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

    PFN_vkCreateRayTracingPipelinesKHR funcCreateRayTracingPipelines =
      (PFN_vkCreateRayTracingPipelinesKHR)vkGetInstanceProcAddr(
        instance, "vkCreateRayTracingPipelinesKHR");
    assert(funcCreateRayTracingPipelines);
    if (funcCreateRayTracingPipelines(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rtPipelineCreateInfo, nullptr, &rtPipeline) != VK_SUCCESS) {
      throw std::runtime_error("Could not create RT pipeline");
    }

    vkDestroyShaderModule(device, raygenShaderModule, nullptr);
  }

  void createRtSBT() {
    const size_t sbtSize = 32;
    const size_t sbtAlignment = 64;
    const size_t numSBTs = 3;

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

  void createFramebuffers() {
    swapChainFramebuffers.resize(swapChainImages.size());
    for (uint32_t i = 0; i < swapChainImages.size(); i++) {
      VkImageView attachments[] = {
        swapChainImageViews[i],
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

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create fence");
    }
  }

  VkInstance instance;
  VkDebugUtilsMessengerEXT debugMessenger;
  VkSurfaceKHR surface;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  VkQueue graphicsQueue, presentQueue;
  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  std::vector<VkImageView> swapChainImageViews;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;
  VkRenderPass renderPass;
  VkCommandPool commandPool;
  VkCommandBuffer commandBuffer;
  std::vector<VkFramebuffer> swapChainFramebuffers;
  VkSemaphore imageAvailableSemaphore, renderFinishedSemaphore;
  VkFence inFlightFence;

  VkBuffer perSceneRtUniformBuffer;
  VkDeviceMemory perSceneRtUniformBufferMemory;
  VkImage rtOutputImages[MAX_FRAMES_IN_FLIGHT];
  VkDeviceMemory rtOutputImageMemories[MAX_FRAMES_IN_FLIGHT];
  VkImageView rtOutputImageViews[MAX_FRAMES_IN_FLIGHT];
  VkDescriptorSetLayout rtDescriptorSetLayout;
  VkDescriptorPool rtDescriptorPool;
  VkDescriptorSet rtDescriptorSet;
  VkPipelineLayout rtPipelineLayout;
  VkPipeline rtPipeline;
  VkBuffer sbtBuffer;
  VkDeviceMemory sbtMemory;
  VkStridedDeviceAddressRegionKHR rtRGenRegion{}, rtMissRegion{}, rtHitRegion{}, rtCallRegion{};

  VkAccelerationStructureKHR blas, tlas;

  // lss
  VkBuffer lssVertexBuffer, lssRadiusBuffer, lssIndexBuffer;
  VkDeviceMemory lssVertexMemory, lssRadiusMemory, lssIndexMemory;
  // blas
  VkBuffer blasResultBuffer;
  VkDeviceMemory blasResultMemory;
  // tlas
  VkBuffer tlasResultBuffer, tlasInstancesBuffer;
  VkDeviceMemory tlasResultMemory, tlasInstancesMemory;
};


std::unique_ptr<HelloLSSApplication> g_app;

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  if (action == GLFW_PRESS) {
    switch (key) {
    case GLFW_KEY_ESCAPE: {
      g_app->should_exit = true;
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

void SetWindowTitle() {
  glfwSetWindowTitle(window, "Vulkan LSS");
}

void initWindow() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  window = glfwCreateWindow(800, 600, "Vulkan window", nullptr, nullptr);
  glfwSetKeyCallback(window, KeyCallback);
  SetWindowTitle();
}

int main(int argc, char** argv) {
  g_app = std::make_unique<HelloLSSApplication>();

  printf("sizeof(VkAccelerationStructureGeometryLinearSweptSpheresDataNV)=%zu\n",
    sizeof(VkAccelerationStructureGeometryLinearSweptSpheresDataNV));

  try {
    g_app->run();
  }
  catch (std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}