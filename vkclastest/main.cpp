#define _CRT_SECURE_NO_WARNINGS

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

std::string g_gltf_filename = "bunny.gltf";

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

class HelloClasApplication;
HelloClasApplication* g_app{};
GLFWwindow* window{};
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
bool g_is_rt{ false };
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

  VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME,
};

void SetWindowTitle() {
  if (!g_is_rt) {
    glfwSetWindowTitle(window, "Vulkan CLAS (rast)");
  }
  else {
    glfwSetWindowTitle(window, "Vulkan CLAS (RT)");
  }
}

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

std::vector<const char*> getRequiredExtensions() {
  uint32_t glfwExtensionsCount = 0;
  const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);
  std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionsCount);
  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  return extensions;
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

class HelloClasApplication {
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
    createDescriptorSetLayout();

    readGLTF();
    createAS();

    createGraphicsPipeline();
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

    vkCmdEndRenderPass(commandBuffer);
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("Could not end command buffer");
    }
  }

  void drawFrame() {
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);
    
    uint32_t imageIndex;
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

  void cleanup() {
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
    VkPhysicalDeviceRayTracingValidationFeaturesNV rtValidationFeatures{};
    rtValidationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV;

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

  void createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[1]{};

    descriptorSetLayoutBindings[0].binding = 0;
    descriptorSetLayoutBindings[0].descriptorCount = 1;
    descriptorSetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorSetLayoutBindings[0].pImmutableSamplers = nullptr;
    descriptorSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = _countof(descriptorSetLayoutBindings);
    descriptorSetLayoutInfo.pBindings = descriptorSetLayoutBindings;
    if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create descriptor set layout");
    }
  }

  void readGLTF() {
    cgltf_options options{};
    cgltf_result  cgltfResult{};
    cgltf_data* rawData{};
    cgltfResult = cgltf_parse_file(&options, g_gltf_filename.c_str(), &rawData);
    if (cgltfResult != cgltf_result_success || rawData == nullptr)
    {
      printf("Oh! failed to load gltf.\n");
      exit(1);
    } else {
      printf("Successfully parsed GLTF file %s \n", g_gltf_filename.c_str());
    }

    cgltfResult = cgltf_validate(rawData);
    if (cgltfResult != cgltf_result_success)
    {
      printf("Oh! failed to validate.\n");
      exit(1);
    }
    else {
      printf("Successfully validated GLTF file %s\n", g_gltf_filename.c_str());
    }

    cgltfResult = cgltf_load_buffers(&options, rawData, g_gltf_filename.c_str());

    printf("[readGLTF]\n");
    printf("material count:     %zu\n", rawData->materials_count);
    printf("meshes count:       %zu\n", rawData->meshes_count);
    printf("nv_micromaps_count: %zu\n", rawData->nv_micromaps_count);
    printf("accessors count:    %zu\n", rawData->accessors_count);  // names are all NULL

    assert(rawData->meshes_count > 0);
    cgltf_mesh& mesh0 = rawData->meshes[0];
    printf("mesh0 has %zu prims\n", mesh0.primitives_count);
    for (uint32_t pidx = 0; pidx < mesh0.primitives_count; pidx++) {
      cgltf_primitive& prim = mesh0.primitives[pidx];
      printf("mesh0's prim[%u] has %zu indices, type=%d, component_type=%d\n",
        pidx, prim.indices->count, prim.indices->type, prim.indices->component_type);

      assert(prim.indices->component_type == cgltf_component_type_r_32u);
      cgltf_accessor* pos_accessor = nullptr;
      cgltf_accessor* norm_accessor = nullptr;
      cgltf_accessor* index_accessor = prim.indices;

      for (uint32_t aidx = 0; aidx < prim.attributes_count; aidx++)
      {
        const cgltf_attribute& attr = prim.attributes[aidx];
        switch (attr.type)
        {
        case cgltf_attribute_type_position:
          pos_accessor = attr.data;
          printf("Found position accessor.\n");
          break;
        case cgltf_attribute_type_normal:
          norm_accessor = attr.data;
          printf("Found normals accessor.\n");
          break;
        }
      }

      assert(pos_accessor);

      uint32_t* idxes = (uint32_t*)((uint8_t*)index_accessor->buffer_view->buffer->data + index_accessor->buffer_view->offset);
      glm::vec3* positions = (glm::vec3*)((uint8_t*)pos_accessor->buffer_view->buffer->data + pos_accessor->buffer_view->offset);

      const uint32_t NT = prim.indices->count;
      for (uint32_t iidx = 0; iidx < NT; iidx += 3)
      {
        uint32_t i0 = idxes[iidx], i1 = idxes[iidx + 1], i2 = idxes[iidx + 2];
        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);
      }

      for (uint32_t vidx = 0; vidx < pos_accessor->count; vidx++) {
        vertices.push_back(positions[vidx]);
      }

    }

    // 34817, 208890
    printf("%zu verts and %zu indices loaded.\n", vertices.size(), indices.size());
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

  void createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);
    VkCommandPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
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
    vertexBindingDesc.stride = sizeof(glm::vec3);
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

  void createAS() {

  }

  std::vector<glm::vec3> vertices;
  std::vector<uint32_t> indices;

  VkInstance instance;
  VkDebugUtilsMessengerEXT debugMessenger;
  VkSurfaceKHR surface;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  VkQueue graphicsQueue, presentQueue;
  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;
  std::vector<VkImageView> swapChainImageViews;
  VkRenderPass renderPass;
  VkCommandPool commandPool;
  VkCommandBuffer commandBuffer;
  VkPipeline graphicsPipeline;
  VkPipelineLayout pipelineLayout;
  VkDescriptorSetLayout descriptorSetLayout;
  std::vector<VkFramebuffer> swapChainFramebuffers;
  VkSemaphore imageAvailableSemaphore;
  VkSemaphore renderFinishedSemaphore;
  VkFence inFlightFence;
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

int main() {
  printf("Hey.\n");

  g_app = new HelloClasApplication();
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