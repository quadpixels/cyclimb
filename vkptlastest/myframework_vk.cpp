#include "myframework_vk.h"

#include <glm/glm.hpp>

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

#include <assert.h>
#include <algorithm>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

#ifdef max
#undef max
#endif

static std::vector<const char*> validationLayers = {
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
  VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME,
};

std::vector<const char*> ommExtensions = {
  VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME,
};

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

void MyFrameworkVk::InitWindow(const char* appName, uint32_t width, uint32_t height, GLFWkeyfun keyCallback) {
  this->width = width;
  this->height = height;
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  window = glfwCreateWindow(800, 600, "Vulkan window", nullptr, nullptr);
  if (keyCallback != nullptr) {
    glfwSetKeyCallback(window, keyCallback);
  }
  this->appName = std::string(appName);
}

void MyFrameworkVk::InitDeviceAndCommandQ() {
  createInstance();
  if (enableValidationLayers) {
    setupDebugMessenger();
  }
  createSurface();
  pickPhysicalDevice();
  createLogicalDevice();
  createCommandPool();
  createCommandBuffer();
}

void MyFrameworkVk::createInstance() {
  if (enableValidationLayers) {
    bool ok = checkValidationLayerSupport();
    printf("Validation layers enabled, check result=%d\n", ok);
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = appName.c_str();
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "No engine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_4;

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

bool MyFrameworkVk::checkValidationLayerSupport() {
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

void MyFrameworkVk::setupDebugMessenger() {
  if (!enableValidationLayers) return;
  VkDebugUtilsMessengerCreateInfoEXT createInfo{};
  populateDebugMessengerCreateInfo(createInfo);
  if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
    throw std::runtime_error("Failed to set up debug messenger");
  }
}

void MyFrameworkVk::createSurface() {
  if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create surface");
  }
}

void MyFrameworkVk::pickPhysicalDevice() {
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

bool MyFrameworkVk::isDeviceSuitable(VkPhysicalDevice device) {
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
      hasOMM = true;
    }
    else {
      printf("Device does not support OMM.\n");
    }
    return qfi.isComplete() && extensionSupported && swapChainSupport.presentModes.size() > 0;
  }
  return false;
}

MyFrameworkVk::SwapChainSupportDetails MyFrameworkVk::querySwapChainSupport(VkPhysicalDevice device) {
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

MyFrameworkVk::QueueFamilyIndices MyFrameworkVk::findQueueFamilies(VkPhysicalDevice device) {
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

bool MyFrameworkVk::do_checkDeviceExtensionSupport(VkPhysicalDevice device, std::vector<const char*> devExts) {
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

bool MyFrameworkVk::checkDeviceExtensionSupport(VkPhysicalDevice device) {
  return do_checkDeviceExtensionSupport(device, deviceExtensions);
}

bool MyFrameworkVk::checkOmmExtensionSupport(VkPhysicalDevice device) {
  return do_checkDeviceExtensionSupport(device, ommExtensions);
}

void MyFrameworkVk::createLogicalDevice() {
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
  VkPhysicalDeviceRayTracingValidationFeaturesNV rtValidationFeatures{};
  rtValidationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV;

  // Use RT validation and RT by default
  VkPhysicalDeviceOpacityMicromapFeaturesEXT ommFeatures{};
  ommFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT;
  ommFeatures.pNext = &rtValidationFeatures;

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
  rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
  if (hasOMM) {
    rayTracingPipelineFeatures.pNext = &ommFeatures;
  } else{
    rayTracingPipelineFeatures.pNext = &rtValidationFeatures;
  }

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

VkSurfaceFormatKHR MyFrameworkVk::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
  for (const auto& f : availableFormats) {
    if (f.format == VK_FORMAT_R8G8B8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return f;
    }
  }
  return availableFormats.at(0);
}

VkPresentModeKHR MyFrameworkVk::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
  for (const auto& p : availablePresentModes) {
    if (p == VK_PRESENT_MODE_MAILBOX_KHR) {
      return p;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D MyFrameworkVk::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
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

void MyFrameworkVk::InitSwapchain() {
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

  // createImageViews()
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

  createSyncObjects();
}

void MyFrameworkVk::createCommandPool() {
  QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);
  VkCommandPoolCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  createInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
  createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (vkCreateCommandPool(device, &createInfo, nullptr, &commandPool) != VK_SUCCESS) {
    throw std::runtime_error("Could not create command pool");
  }
}

void MyFrameworkVk::createCommandBuffer() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("Could not allocate command buffer");
  }
}

void MyFrameworkVk::InitRenderPassAndFramebuffers() {
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

  // createFramebuffers() needs a render pass
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

void MyFrameworkVk::createSyncObjects() {
  VkSemaphoreCreateInfo semaphoreInfo{};
  uint32_t N = swapChainImages.size();
  imageAvailableSemaphore.resize(N);
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  for (uint32_t i=0; i<N; i++) {
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create semaphore");
    }
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

std::vector<char> MyFrameworkVk::ReadFile(const std::string& filename) {
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

void MyFrameworkVk::CreateMyRtPipeline(MyRtPipeline* my_rt_pipeline, MyRtShaderListInfo& info) {
  size_t numSBTs = 0;   // Rgen only

  // Pipeline Descriptor Set Layout
  VkDescriptorSetLayoutBinding rtPipeDSLB[2]{};
  rtPipeDSLB[0].binding = 0;
  rtPipeDSLB[0].descriptorCount = 1;
  rtPipeDSLB[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  rtPipeDSLB[0].pImmutableSamplers = nullptr;
  rtPipeDSLB[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

  rtPipeDSLB[1].binding = 1;
  rtPipeDSLB[1].descriptorCount = 1;
  rtPipeDSLB[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  rtPipeDSLB[1].pImmutableSamplers = nullptr;
  rtPipeDSLB[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

  VkDescriptorSetLayoutCreateInfo dslci{};
  dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  dslci.bindingCount = _countof(rtPipeDSLB);
  dslci.pBindings = rtPipeDSLB;
  if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &my_rt_pipeline->rtPipeDSL) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create RT descriptor set layout");
  }

  // Construct shader groups based on shaders provided
  std::vector<VkPipelineShaderStageCreateInfo> stages;
  std::vector<VkShaderModule> modules;
  std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
  uint32_t num_sbts{ 0 };

  if (info.raygen_shader) {
    VkPipelineShaderStageCreateInfo ssci{};
    std::vector<char> raygenShaderCode = MyFrameworkVk::ReadFile(info.raygen_shader);
    modules.push_back(CreateShaderModule(raygenShaderCode));

    ssci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ssci.pName = "main";
    ssci.module = modules.back();
    ssci.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages.push_back(ssci);

    VkRayTracingShaderGroupCreateInfoKHR sgci{};
    sgci.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    sgci.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    sgci.anyHitShader = VK_SHADER_UNUSED_KHR;
    sgci.closestHitShader = VK_SHADER_UNUSED_KHR;
    sgci.generalShader = stages.size() - 1;  // Raygen, mapped by index here, but export in DX12
    sgci.intersectionShader = VK_SHADER_UNUSED_KHR;
    groups.push_back(sgci);

    numSBTs++;
  }

  if (info.closest_hit_shader) {
    VkPipelineShaderStageCreateInfo ssci{};
    std::vector<char> shaderCode = MyFrameworkVk::ReadFile(info.closest_hit_shader);
    modules.push_back(CreateShaderModule(shaderCode));

    ssci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ssci.pName = "main";
    ssci.module = modules.back();
    ssci.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    stages.push_back(ssci);

    VkRayTracingShaderGroupCreateInfoKHR sgci{};
    sgci.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    sgci.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    sgci.anyHitShader = VK_SHADER_UNUSED_KHR;
    sgci.closestHitShader = stages.size() - 1;
    sgci.generalShader = VK_SHADER_UNUSED_KHR;
    sgci.intersectionShader = VK_SHADER_UNUSED_KHR;
    groups.push_back(sgci);

    numSBTs++;
  }

  if (info.miss_shader) {
    VkPipelineShaderStageCreateInfo ssci{};
    std::vector<char> shaderCode = MyFrameworkVk::ReadFile(info.miss_shader);
    modules.push_back(CreateShaderModule(shaderCode));

    ssci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ssci.pName = "main";
    ssci.module = modules.back();
    ssci.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages.push_back(ssci);

    VkRayTracingShaderGroupCreateInfoKHR sgci{};
    sgci.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    sgci.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    sgci.anyHitShader = VK_SHADER_UNUSED_KHR;
    sgci.closestHitShader = VK_SHADER_UNUSED_KHR;
    sgci.generalShader = stages.size() - 1;  // Raygen, mapped by index here, but export in DX12
    sgci.intersectionShader = VK_SHADER_UNUSED_KHR;
    groups.push_back(sgci);

    numSBTs++;
  }

  // Pipeline Layout
  VkPipelineLayoutCreateInfo rtPipelineLayoutInfo{ };
  rtPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  rtPipelineLayoutInfo.setLayoutCount = 1;
  rtPipelineLayoutInfo.pSetLayouts = &my_rt_pipeline->rtPipeDSL;
  rtPipelineLayoutInfo.pushConstantRangeCount = 0;
  rtPipelineLayoutInfo.pPushConstantRanges = nullptr;
  if (vkCreatePipelineLayout(device, &rtPipelineLayoutInfo, nullptr, &my_rt_pipeline->rtPipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Could not create rt pipeline layout");
  }

  // Pipeline stages are contained in info
  // Shader Group / HitGroup are contained in info

  VkRayTracingPipelineCreateInfoKHR rtPipelineCreateInfo{};
  rtPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
  rtPipelineCreateInfo.flags = 0;
  rtPipelineCreateInfo.stageCount = stages.size();
  rtPipelineCreateInfo.pStages = stages.data();
  rtPipelineCreateInfo.groupCount = groups.size();
  rtPipelineCreateInfo.pGroups = groups.data();
  rtPipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
  rtPipelineCreateInfo.pLibraryInfo = nullptr;
  rtPipelineCreateInfo.pLibraryInterface = nullptr;
  rtPipelineCreateInfo.pDynamicState = nullptr;
  rtPipelineCreateInfo.layout = my_rt_pipeline->rtPipelineLayout;

  PFN_vkCreateRayTracingPipelinesKHR funcCreateRayTracingPipelines =
    (PFN_vkCreateRayTracingPipelinesKHR)vkGetInstanceProcAddr(
      instance, "vkCreateRayTracingPipelinesKHR");
  assert(funcCreateRayTracingPipelines);
  if (funcCreateRayTracingPipelines(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rtPipelineCreateInfo, nullptr, &my_rt_pipeline->rtPipeline) != VK_SUCCESS) {
    throw std::runtime_error("Could not create RT pipeline");
  }

  // Create RT SBT
  const size_t sbtSize = 32;
  const size_t sbtAlignment = 64;
  const size_t sbtMemorySize = numSBTs * sbtAlignment;

  createBuffer(sbtMemorySize,
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
    | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    my_rt_pipeline->sbtBuffer,
    my_rt_pipeline->sbtMemory
  );

  std::vector<char> shaderGroupHandle(sbtSize * numSBTs);
  PFN_vkGetRayTracingShaderGroupHandlesKHR funcGetRayTracingShaderGroupHandlesKHR =
    (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetInstanceProcAddr(
      instance, "vkGetRayTracingShaderGroupHandlesKHR");
  if (funcGetRayTracingShaderGroupHandlesKHR(device, my_rt_pipeline->rtPipeline, 0, numSBTs, sbtSize * numSBTs, shaderGroupHandle.data()) != VK_SUCCESS) {
    throw std::runtime_error("Could not get RT shader group handles");
  }

  uint8_t* mapped{};
  vkMapMemory(device, my_rt_pipeline->sbtMemory, 0, sbtSize * numSBTs, 0, (void**)&mapped);
  for (uint32_t i = 0; i < numSBTs; i++) {
    memcpy(mapped + i * sbtAlignment, shaderGroupHandle.data() + i * sbtSize, sbtSize);
  }
  vkUnmapMemory(device, my_rt_pipeline->sbtMemory);

  VkDeviceAddress sbtDeviceAddress = getBufferDeviceAddress(my_rt_pipeline->sbtBuffer);

  if (info.raygen_shader) {
    my_rt_pipeline->rtRgenRegion.deviceAddress = sbtDeviceAddress;
    my_rt_pipeline->rtRgenRegion.size = sbtSize;
    my_rt_pipeline->rtRgenRegion.stride = sbtSize;
    sbtDeviceAddress += sbtAlignment;
  }
  if (info.closest_hit_shader) {
    my_rt_pipeline->rtHitRegion.deviceAddress = sbtDeviceAddress;
    my_rt_pipeline->rtHitRegion.size = sbtSize;
    my_rt_pipeline->rtHitRegion.stride = sbtSize;
    sbtDeviceAddress += sbtAlignment;
  }
  if (info.miss_shader) {
    my_rt_pipeline->rtMissRegion.deviceAddress = sbtDeviceAddress;
    my_rt_pipeline->rtMissRegion.size = sbtSize;
    my_rt_pipeline->rtMissRegion.stride = sbtSize;
    sbtDeviceAddress += sbtAlignment;
  }
}

VkShaderModule MyFrameworkVk::CreateShaderModule(const std::vector<char>& code) {
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

void MyFrameworkVk::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
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

VkDeviceAddress MyFrameworkVk::getBufferDeviceAddress(const VkBuffer& buf) {
  VkBufferDeviceAddressInfo addrInfo{};
  addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
  addrInfo.buffer = buf;
  addrInfo.pNext = nullptr;
  return vkGetBufferDeviceAddress(device, &addrInfo);
}

uint32_t MyFrameworkVk::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
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

// CreateUAVTexture2D
void MyFrameworkVk::CreateUAVTexture2D(VkImageView iv, VkDescriptorSet dstSet, uint32_t dstBinding) {
  VkDescriptorImageInfo ii{};
  ii.imageView = iv;
  ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  
  VkWriteDescriptorSet wds{};
  wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  wds.dstSet = dstSet;
  wds.dstBinding = dstBinding;
  wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  wds.descriptorCount = 1;
  wds.pImageInfo = &ii;

  vkUpdateDescriptorSets(device, 1, &wds, 0, nullptr);
}

void MyFrameworkVk::CreateSRVAccelerationStructure(VkAccelerationStructureKHR as, VkDescriptorSet dstSet, uint32_t binding) {
  VkWriteDescriptorSetAccelerationStructureKHR writeDescAS{};
  writeDescAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
  writeDescAS.accelerationStructureCount = 1;
  writeDescAS.pAccelerationStructures = &as;

  VkWriteDescriptorSet wds{};
  wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  wds.dstSet = dstSet;
  wds.dstBinding = binding;
  wds.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  wds.descriptorCount = 1;
  wds.pNext = &writeDescAS;

  vkUpdateDescriptorSets(device, 1, &wds, 0, nullptr);
}

// CreateCBVSRVUAVHeap = VkDescriptorSet + VkDescriptorPool
VkDescriptorSet MyFrameworkVk::CreateDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorPool pool) {
  VkDescriptorSetAllocateInfo dsai{};
  dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dsai.descriptorPool = pool;
  dsai.descriptorSetCount = 1;
  dsai.pSetLayouts = &layout;
  VkDescriptorSet ret{};
  if (vkAllocateDescriptorSets(device, &dsai, &ret) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate RT descriptor sets");
  }
  return ret;
}

VkDescriptorPool MyFrameworkVk::CreateCBVSRVUAVPool(std::vector<std::pair<VkDescriptorType, uint32_t>> sizes, uint32_t maxSets) {
  VkDescriptorPool ret;
  uint32_t sz = sizes.size();
  std::vector<VkDescriptorPoolSize> poolSizes(sz);
  for (uint32_t i = 0; i < sz; i++) {
    poolSizes[i].type = sizes[i].first;
    poolSizes[i].descriptorCount = sizes[i].second;
  }
  VkDescriptorPoolCreateInfo dpci{};
  dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpci.poolSizeCount = poolSizes.size();
  dpci.pPoolSizes = poolSizes.data();
  dpci.maxSets = maxSets;
  dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  if (vkCreateDescriptorPool(device, &dpci, nullptr, &ret) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create RT descriptor pool");
  }
  return ret;
}

void MyFrameworkVk::CreateRtOutputResource(uint32_t w, uint32_t h, VkImage& image, VkDeviceMemory& memory, VkImageView& imageView) {
  // Image
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  imageInfo.extent.width = w;
  imageInfo.extent.height = h;
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
  if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create RT output image");
  }

  // Memory
  VkMemoryRequirements memReq{};
  vkGetImageMemoryRequirements(device, image, &memReq);
  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkMemoryAllocateFlagsInfo allocFlagsInfo{};
  allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
  allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  allocInfo.pNext = &allocFlagsInfo;
  if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate memory for RT output image");
  }
  vkBindImageMemory(device, image, memory, 0);

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
  imageViewInfo.image = image;
  if (vkCreateImageView(device, &imageViewInfo, nullptr, &imageView) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create output image view");
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

  transitionImageLayout(image,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

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

void MyFrameworkVk::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
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

VkCommandBuffer MyFrameworkVk::beginSingleTimeCommands() {
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

void MyFrameworkVk::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue);

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void MyFrameworkVk::ImageMemoryBarrier(VkCommandBuffer commandBuffer,
  VkImage image,
  VkImageLayout oldLayout, VkImageLayout newLayout,
  VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
  VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.image = image;
  barrier.srcAccessMask = srcAccessMask;
  barrier.dstAccessMask = dstAccessMask;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  vkCmdPipelineBarrier(commandBuffer,
    srcStageMask,
    dstStageMask,
    0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// Vert: vec3
// Idx:  uint32
void MyFrameworkVk::BuildBLAS(VkAccelerationStructureKHR& as, VkBuffer& outBlasResultBuffer, VkDeviceMemory& outBlasResultMemory,
  VkBuffer vb, VkBuffer ib, uint32_t maxVertex) {
  VkDeviceAddress vbDeviceAddr = getBufferDeviceAddress(vb);
  VkDeviceAddress ibDeviceAddr = getBufferDeviceAddress(ib);

  VkAccelerationStructureGeometryTrianglesDataKHR asgtd{};
  asgtd.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
  asgtd.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
  asgtd.vertexData.deviceAddress = vbDeviceAddr;
  asgtd.vertexStride = sizeof(glm::vec3);
  asgtd.indexType = VK_INDEX_TYPE_UINT32;
  asgtd.indexData.deviceAddress = ibDeviceAddr;
  asgtd.maxVertex = maxVertex - 1;

  VkAccelerationStructureGeometryKHR asg{};
  asg.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
  asg.flags = 0;
  asg.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  asg.geometry.triangles = asgtd;

  VkAccelerationStructureBuildRangeInfoKHR asbri{};
  asbri.firstVertex = 0;
  asbri.primitiveCount = maxVertex / 3;
  asbri.primitiveOffset = 0;
  asbri.transformOffset = 0;

  VkAccelerationStructureBuildGeometryInfoKHR asbgi{};
  asbgi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
  asbgi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  asbgi.flags = 0;
  asbgi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  asbgi.srcAccelerationStructure = VK_NULL_HANDLE;
  asbgi.dstAccelerationStructure = VK_NULL_HANDLE;
  asbgi.geometryCount = 1;
  asbgi.pGeometries = &asg;
  asbgi.ppGeometries = nullptr;
  asbgi.scratchData.deviceAddress = 0;

  uint32_t primCount{ 1 };
  VkAccelerationStructureBuildSizesInfoKHR asBuildSizeInfo{};
  asBuildSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
  PFN_vkGetAccelerationStructureBuildSizesKHR funcGetAccelerationStructureBuildSizes =
    (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetInstanceProcAddr(
      instance, "vkGetAccelerationStructureBuildSizesKHR");
  assert(funcGetAccelerationStructureBuildSizes);
  funcGetAccelerationStructureBuildSizes(device,
    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
    &asbgi,
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
    outBlasResultBuffer,
    outBlasResultMemory);

  asbgi.scratchData.deviceAddress = getBufferDeviceAddress(blasScratchBuffer);

  VkAccelerationStructureCreateInfoKHR blasCreateInfo{};
  blasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
  blasCreateInfo.createFlags = 0;
  blasCreateInfo.size = asBuildSizeInfo.accelerationStructureSize;
  blasCreateInfo.buffer = outBlasResultBuffer;
  blasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  PFN_vkCreateAccelerationStructureKHR funcCreateAccelerationStructure =
    (PFN_vkCreateAccelerationStructureKHR)vkGetInstanceProcAddr(
      instance, "vkCreateAccelerationStructureKHR");
  if (funcCreateAccelerationStructure(device, &blasCreateInfo, nullptr, &as) != VK_SUCCESS) {
    throw std::runtime_error("Could not create BLAS");
  }
  asbgi.dstAccelerationStructure = as;

  VkCommandBuffer commandBuffer = beginSingleTimeCommands();
  VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
  buildRangeInfo.firstVertex = 0;
  buildRangeInfo.primitiveCount = maxVertex / 3;
  buildRangeInfo.primitiveOffset = 0;
  buildRangeInfo.transformOffset = 0;
  VkAccelerationStructureBuildRangeInfoKHR* const buildRangeInfos[] = { &buildRangeInfo };
  PFN_vkCmdBuildAccelerationStructuresKHR funcCmdBuildAccelerationStructuresKHR =
    (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetInstanceProcAddr(
      instance, "vkCmdBuildAccelerationStructuresKHR");
  funcCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &asbgi, buildRangeInfos);
  VkMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(commandBuffer,
    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
    0, 1, &barrier,
    0, nullptr,
    0, nullptr);
  endSingleTimeCommands(commandBuffer);

  vkDestroyBuffer(device, blasScratchBuffer, nullptr);
  vkFreeMemory(device, blasScratchMemory, nullptr);
}

void MyFrameworkVk::CreateBufferForCPUSideData(void* data, uint32_t len, VkBuffer& buf, VkDeviceMemory& mem) {
  createBuffer(len,
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
    | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    buf, mem
  );
  void* mapped{};
  vkMapMemory(device, mem, 0, len, 0, &mapped);
  memcpy(mapped, data, len);
  vkUnmapMemory(device, mem);
}

void MyFrameworkVk::BuildTLAS(VkAccelerationStructureKHR& outTlas, const VkAccelerationStructureKHR& blas0, const VkBuffer& blas0Buffer,
  VkBuffer& outTlasResultBuffer, VkDeviceMemory& outTlasResultMemory) {
  VkBuffer tlasInstancesBuffer{};
  VkDeviceMemory tlasInstancesMemory{};

  size_t size = sizeof(VkAccelerationStructureInstanceKHR);
  createBuffer(size, 
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
    tlasInstancesBuffer, tlasInstancesMemory);

  // Get BLAS0's address
  VkDeviceAddress blas0ResultDeviceAddr = getBufferDeviceAddress(blas0Buffer);
  VkDeviceAddress blas0ASAddress{};

  VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo{};
  blasAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
  blasAddrInfo.accelerationStructure = blas0;
  PFN_vkGetAccelerationStructureDeviceAddressKHR funcGetAccelerationStructureDeviceAddressKHR =
    (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetInstanceProcAddr(
      instance, "vkGetAccelerationStructureDeviceAddressKHR");
  blas0ASAddress = funcGetAccelerationStructureDeviceAddressKHR(device, &blasAddrInfo);

  // Instance
  void* data;
  VkAccelerationStructureInstanceKHR asInst{};
  asInst.accelerationStructureReference = blas0ASAddress;
  asInst.transform.matrix[0][0] = 1.0f;
  asInst.transform.matrix[1][1] = 1.0f;
  asInst.transform.matrix[2][2] = 1.0f;
  asInst.instanceCustomIndex = 0;
  asInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  asInst.mask = 0xFF;
  asInst.instanceShaderBindingTableRecordOffset = 0;

  vkMapMemory(device, tlasInstancesMemory, 0, size, 0, &data);
  memcpy(data, &asInst, size);
  vkUnmapMemory(device, tlasInstancesMemory);

  // Geom
  VkAccelerationStructureGeometryKHR instData{};
  instData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
  instData.flags = 0;// VK_GEOMETRY_OPAQUE_BIT_KHR;
  instData.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  instData.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  instData.geometry.instances.data.deviceAddress = getBufferDeviceAddress(tlasInstancesBuffer);

  // TLAS Inst Info
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
    outTlasResultBuffer, outTlasResultMemory);
  
  VkAccelerationStructureCreateInfoKHR tlasCreateInfo{};
  tlasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
  tlasCreateInfo.size = tlasBuildSizeInfo.accelerationStructureSize;
  tlasCreateInfo.buffer = outTlasResultBuffer;
  tlasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  PFN_vkCreateAccelerationStructureKHR funcCreateAccelerationStructure =
    (PFN_vkCreateAccelerationStructureKHR)vkGetInstanceProcAddr(
      instance, "vkCreateAccelerationStructureKHR");
  if (funcCreateAccelerationStructure(device, &tlasCreateInfo, nullptr, &outTlas) != VK_SUCCESS) {
    throw std::runtime_error("Could not create TLAS");
  }

  // Prepare cmd list
  VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
  buildRangeInfo.firstVertex = 0;
  buildRangeInfo.primitiveCount = 1;  // instance count
  buildRangeInfo.primitiveOffset = 0;
  buildRangeInfo.transformOffset = 0;
  buildInstInfo.scratchData.deviceAddress = getBufferDeviceAddress(tlasScratchBuffer);
  buildInstInfo.dstAccelerationStructure = outTlas;
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