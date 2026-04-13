#include "myframework_vk.h"

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

#include <assert.h>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <set>
#include <string>

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