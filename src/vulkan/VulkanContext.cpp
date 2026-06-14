#include "vulkan/VulkanContext.hpp"
#include "core/Preset.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "vulkan/Pipeline.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "stb_image_write.h"
#pragma GCC diagnostic pop
#include <glm/gtc/constants.hpp>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace {
const std::vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
bool enableValidationLayers = false;
#else
bool enableValidationLayers = true;
#endif

const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};

bool checkValidationLayerSupport() {
  uint32_t layerCount = 0;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  for (const char *layerName : validationLayers) {
    bool layerFound = false;
    for (const auto &layerProperties : availableLayers) {
      if (std::string_view(layerProperties.layerName) == layerName) {
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

std::vector<const char *> getRequiredExtensions() {
  uint32_t glfwExtensionCount = 0;
  const char **glfwExtensions =
      glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector<const char *> extensions(glfwExtensions,
                                       glfwExtensions + glfwExtensionCount);

  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  return extensions;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void *pUserData) {
  (void)messageSeverity;
  (void)messageType;
  (void)pUserData;
  std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
  return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger) {
  auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks *pAllocator) {
  auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
std::string getExecutableDir() {
  char path[MAX_PATH];
  GetModuleFileNameA(NULL, path, MAX_PATH);
  std::string pathStr(path);
  return pathStr.substr(0, pathStr.find_last_of("\\/"));
}
#else
#include <limits.h>
#include <unistd.h>
std::string getExecutableDir() {
  char path[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
  std::string pathStr = (count > 0) ? std::string(path, count) : ".";
  return pathStr.substr(0, pathStr.find_last_of("/"));
}
#endif

std::string getShaderPath(const std::string &shaderName) {
  return getExecutableDir() + "/shaders/" + shaderName;
}

struct OctreeBuilder {
  std::vector<BHNode> nodes;
  int rootIndex = -1;
  int nodeCount = 0;

  void reset(const glm::vec3 &center, float size) {
    if (nodes.empty()) {
      nodes.resize(131072 * 2);
    }
    nodeCount = 0;
    rootIndex = createNode(center, size);
  }

  int createNode(const glm::vec3 &center, float size) {
    int idx = nodeCount++;
    nodes[idx].centerOfMass = glm::vec4(0.0f);
    nodes[idx].bounds = glm::vec4(center, size);
    for (int i = 0; i < 8; ++i) {
      nodes[idx].children[i] = -1;
    }
    return idx;
  }

  void insert(int nodeIdx, const glm::vec4 &pPos, int depth = 0) {
    float pMass = pPos.w;
    if (pMass <= 0.0f)
      return;

    if (depth > 24 || nodeCount >= 131072 * 2 - 10) {
      float totalMass = nodes[nodeIdx].centerOfMass.w + pMass;
      if (totalMass > 0.0f) {
        glm::vec3 com = (glm::vec3(nodes[nodeIdx].centerOfMass) *
                             nodes[nodeIdx].centerOfMass.w +
                         glm::vec3(pPos) * pMass) /
                        totalMass;
        nodes[nodeIdx].centerOfMass = glm::vec4(com, totalMass);
      }
      return;
    }

    if (nodes[nodeIdx].centerOfMass.w == 0.0f) {
      nodes[nodeIdx].centerOfMass = pPos;
      return;
    }

    bool isLeaf = true;
    for (int i = 0; i < 8; ++i) {
      if (nodes[nodeIdx].children[i] != -1) {
        isLeaf = false;
        break;
      }
    }

    glm::vec3 center = glm::vec3(nodes[nodeIdx].bounds);
    float size = nodes[nodeIdx].bounds.w;

    if (isLeaf) {
      glm::vec4 existingPos = nodes[nodeIdx].centerOfMass;
      glm::vec3 diff = glm::vec3(existingPos) - glm::vec3(pPos);
      if (glm::dot(diff, diff) < 1e-6f) {
        float totalMass = existingPos.w + pMass;
        glm::vec3 com =
            (glm::vec3(existingPos) * existingPos.w + glm::vec3(pPos) * pMass) /
            totalMass;
        nodes[nodeIdx].centerOfMass = glm::vec4(com, totalMass);
        return;
      }

      float childSize = size * 0.5f;
      int children[8];
      for (int i = 0; i < 8; ++i) {
        glm::vec3 childCenter = center;
        childCenter.x += ((i & 1) ? 1.0f : -1.0f) * childSize * 0.5f;
        childCenter.y += ((i & 2) ? 1.0f : -1.0f) * childSize * 0.5f;
        childCenter.z += ((i & 4) ? 1.0f : -1.0f) * childSize * 0.5f;
        children[i] = createNode(childCenter, childSize);
      }

      for (int i = 0; i < 8; ++i) {
        nodes[nodeIdx].children[i] = children[i];
      }

      int oct1 = getOctant(center, glm::vec3(existingPos));
      insert(nodes[nodeIdx].children[oct1], existingPos, depth + 1);

      int oct2 = getOctant(center, glm::vec3(pPos));
      insert(nodes[nodeIdx].children[oct2], pPos, depth + 1);

      float totalMass = existingPos.w + pMass;
      glm::vec3 com =
          (glm::vec3(existingPos) * existingPos.w + glm::vec3(pPos) * pMass) /
          totalMass;
      nodes[nodeIdx].centerOfMass = glm::vec4(com, totalMass);
    } else {
      float totalMass = nodes[nodeIdx].centerOfMass.w + pMass;
      glm::vec3 com = (glm::vec3(nodes[nodeIdx].centerOfMass) *
                           nodes[nodeIdx].centerOfMass.w +
                       glm::vec3(pPos) * pMass) /
                      totalMass;
      nodes[nodeIdx].centerOfMass = glm::vec4(com, totalMass);

      int oct = getOctant(center, glm::vec3(pPos));
      insert(nodes[nodeIdx].children[oct], pPos, depth + 1);
    }
  }

  int getOctant(const glm::vec3 &center, const glm::vec3 &pos) {
    int oct = 0;
    if (pos.x >= center.x)
      oct |= 1;
    if (pos.y >= center.y)
      oct |= 2;
    if (pos.z >= center.z)
      oct |= 4;
    return oct;
  }

  void computeSkipLinks(int nodeIdx, int skipIdx) {
    std::vector<int> validChildren;
    for (int i = 0; i < 8; ++i) {
      int child = nodes[nodeIdx].children[i];
      if (child != -1) {
        validChildren.push_back(child);
      }
    }

    if (validChildren.empty()) {
      nodes[nodeIdx].children[0] = skipIdx;
      nodes[nodeIdx].children[1] = skipIdx;
    } else {
      nodes[nodeIdx].children[0] = validChildren[0];
      nodes[nodeIdx].children[1] = skipIdx;
      for (size_t i = 0; i < validChildren.size(); ++i) {
        int childSkip =
            (i + 1 < validChildren.size()) ? validChildren[i + 1] : skipIdx;
        computeSkipLinks(validChildren[i], childSkip);
      }
    }
  }
};
} // namespace

// Image / memory helpers
VkDeviceMemory VulkanContext::allocateImageMemory(VkImage image,
                                                  VkMemoryPropertyFlags props) {
  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(m_device, image, &memReqs);

  VkPhysicalDeviceMemoryProperties memProps;
  vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

  uint32_t typeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
    if ((memReqs.memoryTypeBits & (1u << i)) &&
        (memProps.memoryTypes[i].propertyFlags & props) == props) {
      typeIndex = i;
      break;
    }
  }
  if (typeIndex == UINT32_MAX)
    throw std::runtime_error("Failed to find suitable memory type for image");

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = typeIndex;

  VkDeviceMemory memory = VK_NULL_HANDLE;
  if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
    throw std::runtime_error("Failed to allocate image memory");
  vkBindImageMemory(m_device, image, memory, 0);
  return memory;
}

VkCommandBuffer VulkanContext::beginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);
  return cmd;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer cmd) {
  vkEndCommandBuffer(cmd);
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(m_graphicsQueue);
  vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

// init / cleanup

void VulkanContext::init(GLFWwindow *window) {
  createInstance();
  setupDebugMessenger();
  createSurface(window);
  pickPhysicalDevice();
  createLogicalDevice();
  createSwapChain(window);
  createImageViews();
  createCommandPool();
  createHDRImage();
  createBloomImages();
  createParticleRenderPass();
  createCompositeRenderPass();
  createDescriptorSetLayouts();
  createPipelines();
  createFramebuffers();
  createShaderStorageBuffers(8192);
  createUniformBuffers();
  createDescriptorPool();
  createDescriptorSets();
  createBloomDescriptorSets();
  createCommandBuffers();
  createSyncPrimitives();
  createTimestampPool();
  initImGui(window);
  createFrameExportStagingBuffer();
  startFrameWriterThread();
}

void VulkanContext::cleanupSwapChainDependents() {
  if (m_device == VK_NULL_HANDLE)
    return;
  vkDeviceWaitIdle(m_device);
  destroyFrameExportStagingBuffer();
  shutdownImGui();
  if (m_timestampPool != VK_NULL_HANDLE) {
    vkDestroyQueryPool(m_device, m_timestampPool, nullptr);
    m_timestampPool = VK_NULL_HANDLE;
  }
  if (m_hdrFramebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(m_device, m_hdrFramebuffer, nullptr);
    m_hdrFramebuffer = VK_NULL_HANDLE;
  }
  for (auto fb : m_swapChainFramebuffers) {
    vkDestroyFramebuffer(m_device, fb, nullptr);
  }
  m_swapChainFramebuffers.clear();
  auto destroyPipeline = [&](VkPipeline &p) {
    if (p != VK_NULL_HANDLE) {
      vkDestroyPipeline(m_device, p, nullptr);
      p = VK_NULL_HANDLE;
    }
  };
  auto destroyLayout = [&](VkPipelineLayout &l) {
    if (l != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(m_device, l, nullptr);
      l = VK_NULL_HANDLE;
    }
  };
  destroyPipeline(m_bloomThresholdPipeline);
  destroyPipeline(m_bloomBlurPipeline);
  destroyLayout(m_bloomComputeLayout);
  destroyPipeline(m_compositePipeline);
  destroyLayout(m_compositeLayout);
  destroyPipeline(m_trailPipeline);
  destroyLayout(m_trailLayout);
  destroyPipeline(m_graphicsPipeline);
  destroyLayout(m_graphicsPipelineLayout);
  destroyPipeline(m_computePipeline);
  destroyPipeline(m_bhComputePipeline);
  destroyLayout(m_computePipelineLayout);
  if (m_descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    m_descriptorPool = VK_NULL_HANDLE;
  }
  if (m_hdrSampler != VK_NULL_HANDLE) {
    vkDestroySampler(m_device, m_hdrSampler, nullptr);
    m_hdrSampler = VK_NULL_HANDLE;
  }
  if (m_hdrImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(m_device, m_hdrImageView, nullptr);
    m_hdrImageView = VK_NULL_HANDLE;
  }
  if (m_hdrImage != VK_NULL_HANDLE) {
    vkDestroyImage(m_device, m_hdrImage, nullptr);
    m_hdrImage = VK_NULL_HANDLE;
  }
  if (m_hdrMemory != VK_NULL_HANDLE) {
    vkFreeMemory(m_device, m_hdrMemory, nullptr);
    m_hdrMemory = VK_NULL_HANDLE;
  }
  if (m_bloomSamplerA != VK_NULL_HANDLE) {
    vkDestroySampler(m_device, m_bloomSamplerA, nullptr);
    m_bloomSamplerA = VK_NULL_HANDLE;
  }
  if (m_bloomImageViewA != VK_NULL_HANDLE) {
    vkDestroyImageView(m_device, m_bloomImageViewA, nullptr);
    m_bloomImageViewA = VK_NULL_HANDLE;
  }
  if (m_bloomImageA != VK_NULL_HANDLE) {
    vkDestroyImage(m_device, m_bloomImageA, nullptr);
    m_bloomImageA = VK_NULL_HANDLE;
  }
  if (m_bloomMemoryA != VK_NULL_HANDLE) {
    vkFreeMemory(m_device, m_bloomMemoryA, nullptr);
    m_bloomMemoryA = VK_NULL_HANDLE;
  }
  if (m_bloomSamplerB != VK_NULL_HANDLE) {
    vkDestroySampler(m_device, m_bloomSamplerB, nullptr);
    m_bloomSamplerB = VK_NULL_HANDLE;
  }
  if (m_bloomImageViewB != VK_NULL_HANDLE) {
    vkDestroyImageView(m_device, m_bloomImageViewB, nullptr);
    m_bloomImageViewB = VK_NULL_HANDLE;
  }
  if (m_bloomImageB != VK_NULL_HANDLE) {
    vkDestroyImage(m_device, m_bloomImageB, nullptr);
    m_bloomImageB = VK_NULL_HANDLE;
  }
  if (m_bloomMemoryB != VK_NULL_HANDLE) {
    vkFreeMemory(m_device, m_bloomMemoryB, nullptr);
    m_bloomMemoryB = VK_NULL_HANDLE;
  }
  if (m_particleRenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(m_device, m_particleRenderPass, nullptr);
    m_particleRenderPass = VK_NULL_HANDLE;
  }
  if (m_compositeRenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(m_device, m_compositeRenderPass, nullptr);
    m_compositeRenderPass = VK_NULL_HANDLE;
  }
  for (auto imageView : m_swapChainImageViews) {
    vkDestroyImageView(m_device, imageView, nullptr);
  }
  m_swapChainImageViews.clear();
  if (m_swapChain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
    m_swapChain = VK_NULL_HANDLE;
  }
}

void VulkanContext::recreateSwapChain(GLFWwindow *window) {
  int width = 0, height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(window, &width, &height);
    glfwWaitEvents();
  }
  vkDeviceWaitIdle(m_device);
  cleanupSwapChainDependents();
  createSwapChain(window);
  createImageViews();
  createHDRImage();
  createBloomImages();
  createParticleRenderPass();
  createCompositeRenderPass();
  createPipelines();
  createFramebuffers();
  createDescriptorPool();
  createDescriptorSets();
  createBloomDescriptorSets();
  createTimestampPool();
  initImGui(window);
  createFrameExportStagingBuffer();
}

void VulkanContext::cleanup() {
  if (m_device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_device);
  }

  stopFrameWriterThread();
  destroyFrameExportStagingBuffer();
  shutdownImGui();

  // Timestamp pool
  if (m_timestampPool != VK_NULL_HANDLE) {
    vkDestroyQueryPool(m_device, m_timestampPool, nullptr);
    m_timestampPool = VK_NULL_HANDLE;
  }

  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    if (m_device != VK_NULL_HANDLE) {
      vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
      vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
      vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
      vkDestroySemaphore(m_device, m_computeFinishedSemaphores[i], nullptr);
      vkDestroyFence(m_device, m_computeInFlightFences[i], nullptr);
    }
  }

  if (m_commandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
  }
  if (m_computeCommandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(m_device, m_computeCommandPool, nullptr);
  }

  // Framebuffers
  if (m_hdrFramebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(m_device, m_hdrFramebuffer, nullptr);
  }
  for (auto fb : m_swapChainFramebuffers) {
    vkDestroyFramebuffer(m_device, fb, nullptr);
  }

  // Post-process pipelines
  auto destroyPipeline = [&](VkPipeline &p) {
    if (p != VK_NULL_HANDLE) {
      vkDestroyPipeline(m_device, p, nullptr);
      p = VK_NULL_HANDLE;
    }
  };
  auto destroyLayout = [&](VkPipelineLayout &l) {
    if (l != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(m_device, l, nullptr);
      l = VK_NULL_HANDLE;
    }
  };
  destroyPipeline(m_bloomThresholdPipeline);
  destroyPipeline(m_bloomBlurPipeline);
  destroyLayout(m_bloomComputeLayout);
  destroyPipeline(m_compositePipeline);
  destroyLayout(m_compositeLayout);
  destroyPipeline(m_trailPipeline);
  destroyLayout(m_trailLayout);
  destroyPipeline(m_graphicsPipeline);
  destroyLayout(m_graphicsPipelineLayout);
  destroyPipeline(m_computePipeline);
  destroyPipeline(m_bhComputePipeline);
  destroyLayout(m_computePipelineLayout);

  // Descriptor pools
  if (m_descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
  }
  if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
  }

  // Descriptor set layouts
  auto destroyDSL = [&](VkDescriptorSetLayout &l) {
    if (l != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(m_device, l, nullptr);
      l = VK_NULL_HANDLE;
    }
  };
  destroyDSL(m_graphicsDescriptorSetLayout);
  destroyDSL(m_computeDescriptorSetLayout);
  destroyDSL(m_bloomDescriptorSetLayout);
  destroyDSL(m_trailDescriptorSetLayout);

  // HDR image
  if (m_hdrSampler != VK_NULL_HANDLE)
    vkDestroySampler(m_device, m_hdrSampler, nullptr);
  if (m_hdrImageView != VK_NULL_HANDLE)
    vkDestroyImageView(m_device, m_hdrImageView, nullptr);
  if (m_hdrImage != VK_NULL_HANDLE)
    vkDestroyImage(m_device, m_hdrImage, nullptr);
  if (m_hdrMemory != VK_NULL_HANDLE)
    vkFreeMemory(m_device, m_hdrMemory, nullptr);

  // Bloom images
  if (m_bloomSamplerA != VK_NULL_HANDLE)
    vkDestroySampler(m_device, m_bloomSamplerA, nullptr);
  if (m_bloomImageViewA != VK_NULL_HANDLE)
    vkDestroyImageView(m_device, m_bloomImageViewA, nullptr);
  if (m_bloomImageA != VK_NULL_HANDLE)
    vkDestroyImage(m_device, m_bloomImageA, nullptr);
  if (m_bloomMemoryA != VK_NULL_HANDLE)
    vkFreeMemory(m_device, m_bloomMemoryA, nullptr);
  if (m_bloomSamplerB != VK_NULL_HANDLE)
    vkDestroySampler(m_device, m_bloomSamplerB, nullptr);
  if (m_bloomImageViewB != VK_NULL_HANDLE)
    vkDestroyImageView(m_device, m_bloomImageViewB, nullptr);
  if (m_bloomImageB != VK_NULL_HANDLE)
    vkDestroyImage(m_device, m_bloomImageB, nullptr);
  if (m_bloomMemoryB != VK_NULL_HANDLE)
    vkFreeMemory(m_device, m_bloomMemoryB, nullptr);

  m_particleBuffers.clear();
  m_cameraUBOs.clear();
  m_bhTreeBuffers.clear();

  if (m_particleRenderPass != VK_NULL_HANDLE)
    vkDestroyRenderPass(m_device, m_particleRenderPass, nullptr);
  if (m_compositeRenderPass != VK_NULL_HANDLE)
    vkDestroyRenderPass(m_device, m_compositeRenderPass, nullptr);

  for (auto imageView : m_swapChainImageViews) {
    vkDestroyImageView(m_device, imageView, nullptr);
  }
  m_swapChainImageViews.clear();

  if (m_swapChain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
    m_swapChain = VK_NULL_HANDLE;
  }

  if (m_device != VK_NULL_HANDLE) {
    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;
  }

  if (m_debugMessenger != VK_NULL_HANDLE) {
    DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    m_debugMessenger = VK_NULL_HANDLE;
  }

  if (m_surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
  }

  if (m_instance != VK_NULL_HANDLE) {
    vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
  }
}

// Core
void VulkanContext::createInstance() {
  if (enableValidationLayers && !checkValidationLayerSupport()) {
    std::cerr << "Validation layers requested, but not available. Disabling "
                 "validation layers."
              << std::endl;
    enableValidationLayers = false;
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Nfield Simulator";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "No Engine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_2;

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  auto extensions = getRequiredExtensions();
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  if (enableValidationLayers) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    debugCreateInfo.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debugCallback;
    createInfo.pNext = &debugCreateInfo;
  } else {
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = nullptr;
  }

  if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan instance");
  }
}

void VulkanContext::setupDebugMessenger() {
  if (!enableValidationLayers)
    return;

  VkDebugUtilsMessengerCreateInfoEXT createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;

  if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr,
                                   &m_debugMessenger) != VK_SUCCESS) {
    throw std::runtime_error("Failed to set up debug messenger");
  }
}

void VulkanContext::createSurface(GLFWwindow *window) {
  if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create window surface");
  }
}

void VulkanContext::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
  if (deviceCount == 0) {
    throw std::runtime_error("Failed to find GPUs with Vulkan support");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

  for (const auto &device : devices) {
    if (isDeviceSuitable(device)) {
      m_physicalDevice = device;
      break;
    }
  }

  if (m_physicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("Failed to find a suitable GPU");
  }
}

void VulkanContext::createLogicalDevice() {
  QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(),
                                            indices.computeFamily.value(),
                                            indices.presentFamily.value()};

  float queuePriority = 1.0f;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  VkPhysicalDeviceFeatures deviceFeatures{};

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();
  createInfo.pEnabledFeatures = &deviceFeatures;

  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  if (enableValidationLayers) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
  } else {
    createInfo.enabledLayerCount = 0;
  }

  if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create logical device");
  }

  vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0,
                   &m_graphicsQueue);
  vkGetDeviceQueue(m_device, indices.computeFamily.value(), 0, &m_computeQueue);
  vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0, &m_presentQueue);
}

void VulkanContext::createSwapChain(GLFWwindow *window) {
  SwapChainSupportDetails swapChainSupport =
      querySwapChainSupport(m_physicalDevice);
  VkSurfaceFormatKHR surfaceFormat =
      chooseSwapSurfaceFormat(swapChainSupport.formats);
  VkPresentModeKHR presentMode =
      chooseSwapPresentMode(swapChainSupport.presentModes);
  VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, window);

  uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
  if (swapChainSupport.capabilities.maxImageCount > 0 &&
      imageCount > swapChainSupport.capabilities.maxImageCount) {
    imageCount = swapChainSupport.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = m_surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

  QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);
  uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(),
                                   indices.presentFamily.value()};

  if (indices.graphicsFamily != indices.presentFamily) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create swap chain");
  }

  vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
  m_swapChainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount,
                          m_swapChainImages.data());

  m_swapChainImageFormat = surfaceFormat.format;
  m_swapChainExtent = extent;
}

void VulkanContext::createImageViews() {
  m_swapChainImageViews.resize(m_swapChainImages.size());
  for (size_t i = 0; i < m_swapChainImages.size(); i++) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = m_swapChainImages[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = m_swapChainImageFormat;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &createInfo, nullptr,
                          &m_swapChainImageViews[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create image views");
    }
  }
}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device) {
  QueueFamilyIndices indices = findQueueFamilies(device);

  uint32_t extensionCount = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       nullptr);
  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       availableExtensions.data());

  bool extensionsSupported = true;
  for (const char *extName : deviceExtensions) {
    bool found = false;
    for (const auto &ext : availableExtensions) {
      if (std::string_view(ext.extensionName) == extName) {
        found = true;
        break;
      }
    }
    if (!found) {
      extensionsSupported = false;
      break;
    }
  }

  bool swapChainAdequate = false;
  if (extensionsSupported) {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
    swapChainAdequate = !swapChainSupport.formats.empty() &&
                        !swapChainSupport.presentModes.empty();
  }

  return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device) {
  QueueFamilyIndices indices;

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                           queueFamilies.data());

  for (uint32_t i = 0; i < queueFamilyCount; ++i) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphicsFamily = i;
    }
    if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
      indices.computeFamily = i;
    }
    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
    if (presentSupport) {
      indices.presentFamily = i;
    }
  }

  return indices;
}

SwapChainSupportDetails
VulkanContext::querySwapChainSupport(VkPhysicalDevice device) {
  SwapChainSupportDetails details;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface,
                                            &details.capabilities);

  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount,
                                       nullptr);
  if (formatCount != 0) {
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount,
                                         details.formats.data());
  }

  uint32_t presentModeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface,
                                            &presentModeCount, nullptr);
  if (presentModeCount != 0) {
    details.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device, m_surface, &presentModeCount, details.presentModes.data());
  }

  return details;
}

VkSurfaceFormatKHR VulkanContext::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR> &availableFormats) {
  for (const auto &availableFormat : availableFormats) {
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM &&
        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return availableFormat;
    }
  }
  return availableFormats[0];
}

VkPresentModeKHR VulkanContext::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR> &availablePresentModes) {
  for (const auto &availablePresentMode : availablePresentModes) {
    if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return availablePresentMode;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D
VulkanContext::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities,
                                GLFWwindow *window) {
  if (capabilities.currentExtent.width !=
      std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  } else {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {static_cast<uint32_t>(width),
                               static_cast<uint32_t>(height)};

    actualExtent.width =
        std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width);
    actualExtent.height =
        std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height);

    return actualExtent;
  }
}

// Helper to create an RGBA16F storage image + image view + sampler
static void createStorageImage(VkDevice device, VkPhysicalDevice physDev,
                               VkExtent2D extent, VkFormat format,
                               VkImage &outImage, VkDeviceMemory &outMemory,
                               VkImageView &outView, VkSampler &outSampler,
                               VkImageUsageFlags usageExtra = 0) {
  VkImageCreateInfo imgInfo{};
  imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imgInfo.imageType = VK_IMAGE_TYPE_2D;
  imgInfo.format = format;
  imgInfo.extent = {extent.width, extent.height, 1};
  imgInfo.mipLevels = 1;
  imgInfo.arrayLayers = 1;
  imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imgInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | usageExtra;
  imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  if (vkCreateImage(device, &imgInfo, nullptr, &outImage) != VK_SUCCESS)
    throw std::runtime_error("Failed to create storage image");

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(device, outImage, &memReqs);

  VkPhysicalDeviceMemoryProperties memProps;
  vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

  uint32_t typeIdx = UINT32_MAX;
  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
    if ((memReqs.memoryTypeBits & (1u << i)) &&
        (memProps.memoryTypes[i].propertyFlags &
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      typeIdx = i;
      break;
    }
  }
  if (typeIdx == UINT32_MAX)
    throw std::runtime_error("No device-local memory for storage image");

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = typeIdx;
  vkAllocateMemory(device, &allocInfo, nullptr, &outMemory);
  vkBindImageMemory(device, outImage, outMemory, 0);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = outImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.layerCount = 1;
  vkCreateImageView(device, &viewInfo, nullptr, &outView);

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(device, &samplerInfo, nullptr, &outSampler);
}

// Transition image layout with a one-shot command
static void transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                  VkImageLayout oldLayout,
                                  VkImageLayout newLayout) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

  if (newLayout == VK_IMAGE_LAYOUT_GENERAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask =
        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
  } else if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  } else {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  }

  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);
}

void VulkanContext::createHDRImage() {
  createStorageImage(m_device, m_physicalDevice, m_swapChainExtent,
                     VK_FORMAT_R16G16B16A16_SFLOAT, m_hdrImage, m_hdrMemory,
                     m_hdrImageView, m_hdrSampler,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT);

  // Transition to COLOR_ATTACHMENT_OPTIMAL so it can be used as a render target
  VkCommandBuffer cmd = beginSingleTimeCommands();
  transitionImageLayout(cmd, m_hdrImage, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  endSingleTimeCommands(cmd);
}

void VulkanContext::createBloomImages() {
  createStorageImage(m_device, m_physicalDevice, m_swapChainExtent,
                     VK_FORMAT_R16G16B16A16_SFLOAT, m_bloomImageA,
                     m_bloomMemoryA, m_bloomImageViewA, m_bloomSamplerA);
  createStorageImage(m_device, m_physicalDevice, m_swapChainExtent,
                     VK_FORMAT_R16G16B16A16_SFLOAT, m_bloomImageB,
                     m_bloomMemoryB, m_bloomImageViewB, m_bloomSamplerB);

  // Transition both to GENERAL for compute access
  VkCommandBuffer cmd = beginSingleTimeCommands();
  transitionImageLayout(cmd, m_bloomImageA, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_GENERAL);
  transitionImageLayout(cmd, m_bloomImageB, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_GENERAL);
  endSingleTimeCommands(cmd);
}

// Render passes

void VulkanContext::createParticleRenderPass() {
  // Renders particles into the HDR RGBA16F offscreen image.
  // First frame clears; subsequent frames load so trail accumulation works.
  VkAttachmentDescription hdrAttachment{};
  hdrAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  hdrAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  hdrAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // preserve trails
  hdrAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  hdrAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  hdrAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  hdrAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  hdrAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorRef{};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;

  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dep.srcAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo rpInfo{};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.attachmentCount = 1;
  rpInfo.pAttachments = &hdrAttachment;
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 1;
  rpInfo.pDependencies = &dep;

  if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_particleRenderPass) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create particle render pass");
}

void VulkanContext::createCompositeRenderPass() {
  // Draws the bloom composite onto the swapchain. Loads existing swapchain
  // content so ImGui can render after without re-clearing.
  VkAttachmentDescription swapAttachment{};
  swapAttachment.format = m_swapChainImageFormat;
  swapAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  swapAttachment.loadOp =
      VK_ATTACHMENT_LOAD_OP_DONT_CARE; // composite overwrites
  swapAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  swapAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  swapAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  swapAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  swapAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorRef{};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;

  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  dep.srcAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo rpInfo{};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.attachmentCount = 1;
  rpInfo.pAttachments = &swapAttachment;
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 1;
  rpInfo.pDependencies = &dep;

  if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_compositeRenderPass) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create composite render pass");
}

void VulkanContext::createFramebuffers() {
  // HDR framebuffer (one, shared across frames)
  {
    VkImageView attachments[] = {m_hdrImageView};
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_particleRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = attachments;
    fbInfo.width = m_swapChainExtent.width;
    fbInfo.height = m_swapChainExtent.height;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_hdrFramebuffer) !=
        VK_SUCCESS)
      throw std::runtime_error("Failed to create HDR framebuffer");
  }

  // Swapchain framebuffers for the composite pass
  m_swapChainFramebuffers.resize(m_swapChainImageViews.size());
  for (size_t i = 0; i < m_swapChainImageViews.size(); i++) {
    VkImageView attachments[] = {m_swapChainImageViews[i]};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_compositeRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = m_swapChainExtent.width;
    framebufferInfo.height = m_swapChainExtent.height;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr,
                            &m_swapChainFramebuffers[i]) != VK_SUCCESS)
      throw std::runtime_error("Failed to create swapchain framebuffer");
  }
}

void VulkanContext::createCommandPool() {
  QueueFamilyIndices queueFamilyIndices = findQueueFamilies(m_physicalDevice);

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

  if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create graphics command pool");
  }

  VkCommandPoolCreateInfo computePoolInfo{};
  computePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  computePoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  computePoolInfo.queueFamilyIndex = queueFamilyIndices.computeFamily.value();

  if (vkCreateCommandPool(m_device, &computePoolInfo, nullptr,
                          &m_computeCommandPool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create compute command pool");
  }
}

void VulkanContext::createCommandBuffers() {
  m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  m_computeCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

  if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate graphics command buffers");
  }

  VkCommandBufferAllocateInfo computeAllocInfo{};
  computeAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  computeAllocInfo.commandPool = m_computeCommandPool;
  computeAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  computeAllocInfo.commandBufferCount =
      static_cast<uint32_t>(m_computeCommandBuffers.size());

  if (vkAllocateCommandBuffers(m_device, &computeAllocInfo,
                               m_computeCommandBuffers.data()) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate compute command buffers");
  }
}

void VulkanContext::createSyncPrimitives() {
  m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
  m_computeFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  m_computeInFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr,
                          &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
        vkCreateSemaphore(m_device, &semaphoreInfo, nullptr,
                          &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
        vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) !=
            VK_SUCCESS ||
        vkCreateSemaphore(m_device, &semaphoreInfo, nullptr,
                          &m_computeFinishedSemaphores[i]) != VK_SUCCESS ||
        vkCreateFence(m_device, &fenceInfo, nullptr,
                      &m_computeInFlightFences[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create synchronization primitives");
    }
  }
}

// Descriptor set layouts

void VulkanContext::createDescriptorSetLayouts() {
  // Graphics: binding 0 = camera UBO
  {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr,
                                    &m_graphicsDescriptorSetLayout) !=
        VK_SUCCESS)
      throw std::runtime_error(
          "Failed to create graphics descriptor set layout");
  }

  // Compute: bindings 0,1,2 = input SSBO, output SSBO, BH tree SSBO
  {
    VkDescriptorSetLayoutBinding computeBindings[3]{};
    for (int i = 0; i < 3; ++i) {
      computeBindings[i].binding = static_cast<uint32_t>(i);
      computeBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      computeBindings[i].descriptorCount = 1;
      computeBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo computeLayoutInfo{};
    computeLayoutInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    computeLayoutInfo.bindingCount = 3;
    computeLayoutInfo.pBindings = computeBindings;

    if (vkCreateDescriptorSetLayout(m_device, &computeLayoutInfo, nullptr,
                                    &m_computeDescriptorSetLayout) !=
        VK_SUCCESS)
      throw std::runtime_error(
          "Failed to create compute descriptor set layout");
  }

  // Bloom compute: bindings 0,1 = storage image (input), storage image (output)
  {
    VkDescriptorSetLayoutBinding bloomBindings[2]{};
    bloomBindings[0].binding = 0;
    bloomBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bloomBindings[0].descriptorCount = 1;
    bloomBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bloomBindings[1].binding = 1;
    bloomBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bloomBindings[1].descriptorCount = 1;
    bloomBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo bloomLayoutInfo{};
    bloomLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    bloomLayoutInfo.bindingCount = 2;
    bloomLayoutInfo.pBindings = bloomBindings;

    if (vkCreateDescriptorSetLayout(m_device, &bloomLayoutInfo, nullptr,
                                    &m_bloomDescriptorSetLayout) != VK_SUCCESS)
      throw std::runtime_error("Failed to create bloom descriptor set layout");
  }

  // Trail/composite graphics: bindings 0,1 = combined image sampler
  {
    VkDescriptorSetLayoutBinding samplerBindings[2]{};
    samplerBindings[0].binding = 0;
    samplerBindings[0].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBindings[0].descriptorCount = 1;
    samplerBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBindings[1].binding = 1;
    samplerBindings[1].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBindings[1].descriptorCount = 1;
    samplerBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo trailLayoutInfo{};
    trailLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    trailLayoutInfo.bindingCount = 2;
    trailLayoutInfo.pBindings = samplerBindings;

    if (vkCreateDescriptorSetLayout(m_device, &trailLayoutInfo, nullptr,
                                    &m_trailDescriptorSetLayout) != VK_SUCCESS)
      throw std::runtime_error(
          "Failed to create trail/composite descriptor set layout");
  }
}

void VulkanContext::createDescriptorPool() {
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
       static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 3)},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 12},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8},
  };

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 4;
  poolInfo.pPoolSizes = poolSizes;
  poolInfo.maxSets = 32;

  if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create descriptor pool");
}

void VulkanContext::createDescriptorSets() {
  // Graphics descriptor sets (camera UBO)
  {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                               m_graphicsDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    m_graphicsDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &allocInfo,
                                 m_graphicsDescriptorSets.data()) != VK_SUCCESS)
      throw std::runtime_error("Failed to allocate graphics descriptor sets");

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      VkDescriptorBufferInfo bufferInfo{};
      bufferInfo.buffer = m_cameraUBOs[i]->getBuffer();
      bufferInfo.offset = 0;
      bufferInfo.range = sizeof(glm::mat4);

      VkWriteDescriptorSet write{};
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = m_graphicsDescriptorSets[i];
      write.dstBinding = 0;
      write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      write.descriptorCount = 1;
      write.pBufferInfo = &bufferInfo;
      vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }
  }

  // Compute descriptor sets (SSBOs)
  {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                               m_computeDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    m_computeDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_device, &allocInfo,
                                 m_computeDescriptorSets.data()) != VK_SUCCESS)
      throw std::runtime_error("Failed to allocate compute descriptor sets");

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      VkDescriptorBufferInfo inputBufferInfo{};
      inputBufferInfo.buffer = m_particleBuffers[i]->getBuffer();
      inputBufferInfo.offset = 0;
      inputBufferInfo.range = m_particleBuffers[i]->getSize();

      VkDescriptorBufferInfo outputBufferInfo{};
      outputBufferInfo.buffer = m_particleBuffers[(i + 1) % 2]->getBuffer();
      outputBufferInfo.offset = 0;
      outputBufferInfo.range = m_particleBuffers[(i + 1) % 2]->getSize();

      VkDescriptorBufferInfo treeBufferInfo{};
      treeBufferInfo.buffer = m_bhTreeBuffers[i]->getBuffer();
      treeBufferInfo.offset = 0;
      treeBufferInfo.range = m_bhTreeBuffers[i]->getSize();

      VkWriteDescriptorSet writes[3]{};
      for (int j = 0; j < 3; ++j) {
        writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[j].dstSet = m_computeDescriptorSets[i];
        writes[j].dstBinding = static_cast<uint32_t>(j);
        writes[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[j].descriptorCount = 1;
      }
      writes[0].pBufferInfo = &inputBufferInfo;
      writes[1].pBufferInfo = &outputBufferInfo;
      writes[2].pBufferInfo = &treeBufferInfo;
      vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
    }
  }
}

void VulkanContext::createBloomDescriptorSets() {
  // Allocate 3 bloom compute descriptor sets: threshold, blur-H, blur-V
  {
    VkDescriptorSetLayout layouts[3] = {
        m_bloomDescriptorSetLayout,
        m_bloomDescriptorSetLayout,
        m_bloomDescriptorSetLayout,
    };
    VkDescriptorSet sets[3]{};
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 3;
    allocInfo.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, sets) != VK_SUCCESS)
      throw std::runtime_error("Failed to allocate bloom descriptor sets");
    m_bloomThresholdDS = sets[0];
    m_bloomBlurHDS = sets[1];
    m_bloomBlurVDS = sets[2];
  }

  // Threshold: HDR → bloomA
  auto writeStorageImage = [&](VkDescriptorSet ds, uint32_t binding,
                               VkImageView view) {
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageView = view;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = ds;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
  };

  // The HDR image needs to be in GENERAL for the bloom threshold compute shader
  // to read it. We transition it here (it's normally COLOR_ATTACHMENT_OPTIMAL
  // during rendering). For simplicity we use GENERAL as the layout for all HDR
  // image accesses in bloom compute.
  writeStorageImage(m_bloomThresholdDS, 0, m_hdrImageView);    // input
  writeStorageImage(m_bloomThresholdDS, 1, m_bloomImageViewA); // output
  writeStorageImage(m_bloomBlurHDS, 0, m_bloomImageViewA);     // input
  writeStorageImage(m_bloomBlurHDS, 1, m_bloomImageViewB);     // output
  writeStorageImage(m_bloomBlurVDS, 0, m_bloomImageViewB);     // input
  writeStorageImage(m_bloomBlurVDS, 1,
                    m_bloomImageViewA); // output (final bloom in A)

  // Composite descriptor set (trail layout, 2 samplers)
  {
    VkDescriptorSet sets[2]{};
    VkDescriptorSetLayout layouts[2] = {
        m_trailDescriptorSetLayout,
        m_trailDescriptorSetLayout,
    };
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, sets) != VK_SUCCESS)
      throw std::runtime_error(
          "Failed to allocate composite/trail descriptor sets");
    m_compositeDS = sets[0];
    m_trailDS = sets[1];
  }

  auto writeSampler = [&](VkDescriptorSet ds, uint32_t binding,
                          VkSampler sampler, VkImageView view,
                          VkImageLayout layout) {
    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = sampler;
    imgInfo.imageView = view;
    imgInfo.imageLayout = layout;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = ds;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
  };

  // Composite: binding0=hdrImage (particle render), binding1=bloomImageA
  // (blurred)
  writeSampler(m_compositeDS, 0, m_hdrSampler, m_hdrImageView,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  writeSampler(m_compositeDS, 1, m_bloomSamplerA, m_bloomImageViewA,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  // Trail decay: uses hdrImage as both source (sampler) and target (render
  // attachment) We only need one sampler for the trail pass (binding 0)
  writeSampler(m_trailDS, 0, m_hdrSampler, m_hdrImageView,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  writeSampler(m_trailDS, 1, m_hdrSampler, m_hdrImageView,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// Pipelines

void VulkanContext::createPipelines() {
  // Graphics (particle) pipeline
  // Push constants: colorMode (4 bytes) for the vertex shader
  VkPushConstantRange gfxPushRange{};
  gfxPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  gfxPushRange.offset = 0;
  gfxPushRange.size = sizeof(uint32_t); // colorMode

  VkPipelineLayoutCreateInfo gfxLayoutInfo{};
  gfxLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  gfxLayoutInfo.setLayoutCount = 1;
  gfxLayoutInfo.pSetLayouts = &m_graphicsDescriptorSetLayout;
  gfxLayoutInfo.pushConstantRangeCount = 1;
  gfxLayoutInfo.pPushConstantRanges = &gfxPushRange;

  if (vkCreatePipelineLayout(m_device, &gfxLayoutInfo, nullptr,
                             &m_graphicsPipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("Failed to create graphics pipeline layout");

  PipelineHelper::GraphicsPipelineConfig gfxConfig{};
  gfxConfig.device = m_device;
  gfxConfig.renderPass = m_particleRenderPass;
  gfxConfig.pipelineLayout = m_graphicsPipelineLayout;
  gfxConfig.vertexShaderPath = getShaderPath("particle.vert.spv");
  gfxConfig.fragmentShaderPath = getShaderPath("particle.frag.spv");
  gfxConfig.extent = m_swapChainExtent;
  m_graphicsPipeline = PipelineHelper::createGraphicsPipeline(gfxConfig);

  // Compute (n-body) pipelines
  // Push constants: dt, G, softeningSqr, numParticles, theta  (5 * 4 = 20
  // bytes)
  VkPushConstantRange compPushRange{};
  compPushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  compPushRange.offset = 0;
  compPushRange.size = sizeof(float) * 4 + sizeof(uint32_t);

  VkPipelineLayoutCreateInfo compLayoutInfo{};
  compLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  compLayoutInfo.setLayoutCount = 1;
  compLayoutInfo.pSetLayouts = &m_computeDescriptorSetLayout;
  compLayoutInfo.pushConstantRangeCount = 1;
  compLayoutInfo.pPushConstantRanges = &compPushRange;

  if (vkCreatePipelineLayout(m_device, &compLayoutInfo, nullptr,
                             &m_computePipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("Failed to create compute pipeline layout");

  {
    PipelineHelper::ComputePipelineConfig cfg{};
    cfg.device = m_device;
    cfg.pipelineLayout = m_computePipelineLayout;
    cfg.computeShaderPath = getShaderPath("integrate_direct.comp.spv");
    m_computePipeline = PipelineHelper::createComputePipeline(cfg);
  }
  {
    PipelineHelper::ComputePipelineConfig cfg{};
    cfg.device = m_device;
    cfg.pipelineLayout = m_computePipelineLayout;
    cfg.computeShaderPath = getShaderPath("integrate_bh.comp.spv");
    m_bhComputePipeline = PipelineHelper::createComputePipeline(cfg);
  }

  // Bloom compute pipelines
  // Threshold push constant: threshold (float)
  {
    VkPushConstantRange bloomPush{};
    bloomPush.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bloomPush.offset = 0;
    bloomPush.size = sizeof(float);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_bloomDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &bloomPush;

    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr,
                               &m_bloomComputeLayout) != VK_SUCCESS)
      throw std::runtime_error(
          "Failed to create bloom compute pipeline layout");

    {
      PipelineHelper::ComputePipelineConfig cfg{};
      cfg.device = m_device;
      cfg.pipelineLayout = m_bloomComputeLayout;
      cfg.computeShaderPath = getShaderPath("bloom_threshold.comp.spv");
      m_bloomThresholdPipeline = PipelineHelper::createComputePipeline(cfg);
    }
    {
      PipelineHelper::ComputePipelineConfig cfg{};
      cfg.device = m_device;
      cfg.pipelineLayout = m_bloomComputeLayout;
      cfg.computeShaderPath = getShaderPath("bloom_blur.comp.spv");
      m_bloomBlurPipeline = PipelineHelper::createComputePipeline(cfg);
    }
  }

  // Composite graphics pipeline
  // Push constant: bloomIntensity (float)
  {
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_trailDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr,
                               &m_compositeLayout) != VK_SUCCESS)
      throw std::runtime_error("Failed to create composite pipeline layout");

    PipelineHelper::FullscreenPipelineConfig cfg{};
    cfg.device = m_device;
    cfg.renderPass = m_compositeRenderPass;
    cfg.pipelineLayout = m_compositeLayout;
    cfg.vertexShaderPath = getShaderPath("fullscreen.vert.spv");
    cfg.fragmentShaderPath = getShaderPath("bloom_composite.frag.spv");
    cfg.extent = m_swapChainExtent;
    cfg.additiveBlend = false;
    m_compositePipeline = PipelineHelper::createFullscreenPipeline(cfg);
  }

  // Trail decay graphics pipeline
  // Push constant: trailDecay (float)
  {
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float);

    // Trail decay uses a single-sampler descriptor set layout (same binding[0])
    // Reuse m_trailDescriptorSetLayout (binding 0 = hdr sampler)
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_trailDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr,
                               &m_trailLayout) != VK_SUCCESS)
      throw std::runtime_error("Failed to create trail pipeline layout");

    PipelineHelper::FullscreenPipelineConfig cfg{};
    cfg.device = m_device;
    cfg.renderPass = m_particleRenderPass;
    cfg.pipelineLayout = m_trailLayout;
    cfg.vertexShaderPath = getShaderPath("fullscreen.vert.spv");
    cfg.fragmentShaderPath = getShaderPath("trail_decay.frag.spv");
    cfg.extent = m_swapChainExtent;
    cfg.additiveBlend = false;
    m_trailPipeline = PipelineHelper::createFullscreenPipeline(cfg);
  }
}

void VulkanContext::createTimestampPool() {
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
  m_timestampPeriod = props.limits.timestampPeriod;

  // Check that the graphics queue family supports timestamps
  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount,
                                           queueFamilies.data());

  auto indices = findQueueFamilies(m_physicalDevice);
  uint32_t gfxFamily = indices.graphicsFamily.value();
  m_timestampSupported = (queueFamilies[gfxFamily].timestampValidBits > 0);

  if (!m_timestampSupported)
    return;

  VkQueryPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
  poolInfo.queryCount = MAX_FRAMES_IN_FLIGHT * TIMESTAMPS_PER_FRAME;

  if (vkCreateQueryPool(m_device, &poolInfo, nullptr, &m_timestampPool) !=
      VK_SUCCESS)
    throw std::runtime_error("Failed to create timestamp query pool");
}

// Shader storage buffers / UBOs
void VulkanContext::createShaderStorageBuffers(uint32_t numParticles) {
  m_numParticles = numParticles;
  VkDeviceSize bufferSize = numParticles * sizeof(Particle);

  m_particleBuffers.resize(2);
  for (int i = 0; i < 2; ++i) {
    m_particleBuffers[i] = std::make_unique<Buffer>(
        m_device, m_physicalDevice, bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
  }

  VkDeviceSize treeBufferSize = 131072 * 2 * sizeof(BHNode);
  m_bhTreeBuffers.resize(2);
  for (int i = 0; i < 2; ++i) {
    m_bhTreeBuffers[i] =
        std::make_unique<Buffer>(m_device, m_physicalDevice, treeBufferSize,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  }
}

void VulkanContext::createUniformBuffers() {
  VkDeviceSize bufferSize = sizeof(glm::mat4);
  m_cameraUBOs.resize(MAX_FRAMES_IN_FLIGHT);
  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    m_cameraUBOs[i] =
        std::make_unique<Buffer>(m_device, m_physicalDevice, bufferSize,
                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  }
}

// Command buffer recording

void VulkanContext::recordCommandBuffer(VkCommandBuffer cmd,
                                        uint32_t imageIndex,
                                        const SimState &simState) {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
    throw std::runtime_error("Failed to begin recording command buffer");

  uint32_t frameSlot = m_currentFrame * TIMESTAMPS_PER_FRAME;

  // Reset timestamps
  if (m_timestampSupported && m_timestampPool != VK_NULL_HANDLE) {
    vkCmdResetQueryPool(cmd, m_timestampPool, frameSlot + 2, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_timestampPool,
                        frameSlot + 2); // gfxBegin
  }

  // Compute->vertex barrier
  {
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = m_particleBuffers[1 - m_currentFrame]->getBuffer();
    barrier.offset = 0;
    barrier.size = m_particleBuffers[1 - m_currentFrame]->getSize();
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1,
                         &barrier, 0, nullptr);
  }

  // Particle render pass into HDR offscreen image
  {
    // Transition HDR image: GENERAL (bloom compute just read it) →
    // COLOR_ATTACHMENT
    VkImageMemoryBarrier hdrBarrier{};
    hdrBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    hdrBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    hdrBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    hdrBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrBarrier.image = m_hdrImage;
    hdrBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    hdrBarrier.srcAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    hdrBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &hdrBarrier);

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_particleRenderPass;
    rpInfo.framebuffer = m_hdrFramebuffer;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_swapChainExtent;
    // No clearColor — we load the previous content (trails) or decay it

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Trail decay: draw fullscreen quad that multiplies HDR content by
    // trailDecay. If disabled, clear the framebuffer.
    if (simState.trailEnabled) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_trailPipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_trailLayout, 0, 1, &m_trailDS, 0, nullptr);
      float decay = simState.trailDecay;
      vkCmdPushConstants(cmd, m_trailLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(float), &decay);
      vkCmdDraw(cmd, 3, 1, 0, 0);
    } else {
      VkClearAttachment clearAttachment{};
      clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      clearAttachment.colorAttachment = 0;
      clearAttachment.clearValue.color = {{0.01f, 0.02f, 0.03f, 1.0f}};

      VkClearRect rect{};
      rect.rect.offset = {0, 0};
      rect.rect.extent = m_swapChainExtent;
      rect.baseArrayLayer = 0;
      rect.layerCount = 1;

      vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &rect);
    }

    // Draw particles
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    VkBuffer vertexBuffers[] = {
        m_particleBuffers[1 - m_currentFrame]->getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipelineLayout, 0, 1,
        &m_graphicsDescriptorSets[m_currentFrame], 0, nullptr);
    uint32_t colorMode = simState.colorMode;
    vkCmdPushConstants(cmd, m_graphicsPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t),
                       &colorMode);
    vkCmdDraw(cmd, m_numParticles, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
  }

  // Bloom post-processing
  if (simState.bloomEnabled) {
    // Transition HDR image to GENERAL for storage image read in bloom threshold
    VkImageMemoryBarrier hdrToGeneral{};
    hdrToGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    hdrToGeneral.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    hdrToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    hdrToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrToGeneral.image = m_hdrImage;
    hdrToGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    hdrToGeneral.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    hdrToGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &hdrToGeneral);

    uint32_t gx = (m_swapChainExtent.width + 15) / 16;
    uint32_t gy = (m_swapChainExtent.height + 15) / 16;

    // Threshold
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                      m_bloomThresholdPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_bloomComputeLayout, 0, 1, &m_bloomThresholdDS, 0,
                            nullptr);
    float threshold = simState.bloomThreshold;
    vkCmdPushConstants(cmd, m_bloomComputeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(float), &threshold);
    vkCmdDispatch(cmd, gx, gy, 1);

    // Barrier between threshold and blur passes
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &memBarrier, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_bloomBlurPipeline);

    // Horizontal blur: bloomA → bloomB
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_bloomComputeLayout, 0, 1, &m_bloomBlurHDS, 0,
                            nullptr);
    int horizontal = 1;
    vkCmdPushConstants(cmd, m_bloomComputeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(int), &horizontal);
    vkCmdDispatch(cmd, gx, gy, 1);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &memBarrier, 0, nullptr, 0, nullptr);

    // Vertical blur: bloomB → bloomA
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_bloomComputeLayout, 0, 1, &m_bloomBlurVDS, 0,
                            nullptr);
    horizontal = 0;
    vkCmdPushConstants(cmd, m_bloomComputeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(int), &horizontal);
    vkCmdDispatch(cmd, gx, gy, 1);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                         &memBarrier, 0, nullptr, 0, nullptr);

    // Transition HDR and Bloom A to SHADER_READ_ONLY for the composite sampler
    VkImageMemoryBarrier barriers[2]{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = m_hdrImage;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barriers[0].srcAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image = m_bloomImageA;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 2, barriers);

    // Composite render pass (bloom + HDR -> swapchain)
    VkRenderPassBeginInfo compRP{};
    compRP.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    compRP.renderPass = m_compositeRenderPass;
    compRP.framebuffer = m_swapChainFramebuffers[imageIndex];
    compRP.renderArea.offset = {0, 0};
    compRP.renderArea.extent = m_swapChainExtent;
    vkCmdBeginRenderPass(cmd, &compRP, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_compositePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_compositeLayout, 0, 1, &m_compositeDS, 0,
                            nullptr);
    float bloomIntensity = simState.bloomIntensity;
    vkCmdPushConstants(cmd, m_compositeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(float), &bloomIntensity);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // ImGui renders in the same composite render pass
    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
      ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }
    vkCmdEndRenderPass(cmd);

    // Transition HDR and Bloom A back to GENERAL for next frame
    VkImageMemoryBarrier barriersBack[2]{};
    barriersBack[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriersBack[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriersBack[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriersBack[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriersBack[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriersBack[0].image = m_hdrImage;
    barriersBack[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barriersBack[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriersBack[0].dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    barriersBack[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriersBack[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriersBack[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriersBack[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriersBack[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriersBack[1].image = m_bloomImageA;
    barriersBack[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barriersBack[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriersBack[1].dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 2, barriersBack);

  } else {
    // Bloom disabled: transition HDR (COLOR_ATTACHMENT) and Bloom A (GENERAL)
    // to SHADER_READ_ONLY
    VkImageMemoryBarrier barriers[2]{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = m_hdrImage;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image = m_bloomImageA;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 2, barriers);

    VkRenderPassBeginInfo compRP{};
    compRP.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    compRP.renderPass = m_compositeRenderPass;
    compRP.framebuffer = m_swapChainFramebuffers[imageIndex];
    compRP.renderArea.offset = {0, 0};
    compRP.renderArea.extent = m_swapChainExtent;
    vkCmdBeginRenderPass(cmd, &compRP, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_compositePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_compositeLayout, 0, 1, &m_compositeDS, 0,
                            nullptr);
    float bloomIntensity = 0.0f;
    vkCmdPushConstants(cmd, m_compositeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(float), &bloomIntensity);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
      ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }
    vkCmdEndRenderPass(cmd);

    // Transition both back to GENERAL layout
    VkImageMemoryBarrier barriersBack[2]{};
    barriersBack[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriersBack[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriersBack[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriersBack[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriersBack[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriersBack[0].image = m_hdrImage;
    barriersBack[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barriersBack[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriersBack[0].dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    barriersBack[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriersBack[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriersBack[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriersBack[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriersBack[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriersBack[1].image = m_bloomImageA;
    barriersBack[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barriersBack[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriersBack[1].dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 2, barriersBack);
  }

  if (m_timestampSupported && m_timestampPool != VK_NULL_HANDLE) {
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        m_timestampPool, frameSlot + 3); // gfxEnd
  }

  if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    throw std::runtime_error("Failed to record command buffer");
}

// drawFrame

bool VulkanContext::drawFrame(const glm::mat4 &viewProj, SimState &simState) {
  VkFence fencesToWait[] = {m_inFlightFences[m_currentFrame],
                            m_computeInFlightFences[1 - m_currentFrame]};
  vkWaitForFences(m_device, 2, fencesToWait, VK_TRUE,
                  std::numeric_limits<uint64_t>::max());

  double now = glfwGetTime();
  static double lastTime = 0.0;
  if (lastTime == 0.0) {
    lastTime = now;
  }
  m_statsTimer += static_cast<float>(now - lastTime);
  lastTime = now;
  if (m_statsTimer >= 1.0f) {
    readbackStats(simState);
    m_statsTimer = 0.0f;
  }

  // Read timestamps from the previous submission of this frame slot
  if (m_timestampSupported && m_timestampPool != VK_NULL_HANDLE &&
      m_queryPoolHasResults[m_currentFrame]) {
    uint64_t timestamps[TIMESTAMPS_PER_FRAME];
    VkResult res = vkGetQueryPoolResults(
        m_device, m_timestampPool, m_currentFrame * TIMESTAMPS_PER_FRAME,
        TIMESTAMPS_PER_FRAME, sizeof(timestamps), timestamps, sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);
    if (res == VK_SUCCESS) {
      // slots 0,1 = compute begin/end; slots 2,3 = gfx begin/end
      uint64_t computeBegin = timestamps[0];
      uint64_t computeEnd = timestamps[1];
      if (computeEnd > computeBegin) {
        float ns =
            static_cast<float>((computeEnd - computeBegin)) * m_timestampPeriod;
        simState.gpuComputeMs = ns / 1e6f;
      }
    }
  }

  uint32_t imageIndex;
  VkResult result = vkAcquireNextImageKHR(
      m_device, m_swapChain, std::numeric_limits<uint64_t>::max(),
      m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return true;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("Failed to acquire swap chain image");
  }

  m_cameraUBOs[m_currentFrame]->write(&viewProj, sizeof(viewProj));

  float dt = (simState.paused && !simState.singleStep) ? 0.0f : simState.dt;
  simState.singleStep = false;

  if (simState.algorithm == SimState::Algorithm::BarnesHut) {
    void *mapped = m_particleBuffers[m_currentFrame]->map();
    Particle *particles = static_cast<Particle *>(mapped);

    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
    for (uint32_t i = 0; i < m_numParticles; ++i) {
      glm::vec3 pos = glm::vec3(particles[i].pos);
      minBounds = glm::min(minBounds, pos);
      maxBounds = glm::max(maxBounds, pos);
    }

    glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    float size = std::max({maxBounds.x - minBounds.x, maxBounds.y - minBounds.y,
                           maxBounds.z - minBounds.z});
    size = std::max(size, 1.0f);

    static OctreeBuilder builder;
    builder.reset(center, size);
    for (uint32_t i = 0; i < m_numParticles; ++i) {
      builder.insert(builder.rootIndex, particles[i].pos, 0);
    }

    if (builder.nodeCount > 0) {
      builder.computeSkipLinks(builder.rootIndex, -1);
    }

    m_particleBuffers[m_currentFrame]->unmap();
    m_bhTreeBuffers[m_currentFrame]->write(builder.nodes.data(),
                                           builder.nodeCount * sizeof(BHNode));
  }

  vkWaitForFences(m_device, 1, &m_computeInFlightFences[m_currentFrame],
                  VK_TRUE, std::numeric_limits<uint64_t>::max());
  vkResetFences(m_device, 1, &m_computeInFlightFences[m_currentFrame]);

  vkResetCommandBuffer(m_computeCommandBuffers[m_currentFrame], 0);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(m_computeCommandBuffers[m_currentFrame], &beginInfo);

  // Timestamp: compute begin
  if (m_timestampSupported && m_timestampPool != VK_NULL_HANDLE) {
    uint32_t slot = m_currentFrame * TIMESTAMPS_PER_FRAME;
    vkCmdResetQueryPool(m_computeCommandBuffers[m_currentFrame],
                        m_timestampPool, slot, 2);
    vkCmdWriteTimestamp(m_computeCommandBuffers[m_currentFrame],
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_timestampPool,
                        slot + 0);
  }

  VkPipeline activePipeline =
      (simState.algorithm == SimState::Algorithm::BarnesHut)
          ? m_bhComputePipeline
          : m_computePipeline;
  vkCmdBindPipeline(m_computeCommandBuffers[m_currentFrame],
                    VK_PIPELINE_BIND_POINT_COMPUTE, activePipeline);

  vkCmdBindDescriptorSets(m_computeCommandBuffers[m_currentFrame],
                          VK_PIPELINE_BIND_POINT_COMPUTE,
                          m_computePipelineLayout, 0, 1,
                          &m_computeDescriptorSets[m_currentFrame], 0, nullptr);

  struct PushConstants {
    float dt;
    float G;
    float softeningSqr;
    uint32_t numParticles;
    float theta;
  } pcs{dt, simState.G, simState.softeningSqr, m_numParticles, simState.theta};

  vkCmdPushConstants(m_computeCommandBuffers[m_currentFrame],
                     m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(pcs), &pcs);

  uint32_t groupCount = (m_numParticles + 255) / 256;
  vkCmdDispatch(m_computeCommandBuffers[m_currentFrame], groupCount, 1, 1);

  // Timestamp: compute end
  if (m_timestampSupported && m_timestampPool != VK_NULL_HANDLE) {
    uint32_t slot = m_currentFrame * TIMESTAMPS_PER_FRAME;
    vkCmdWriteTimestamp(m_computeCommandBuffers[m_currentFrame],
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampPool,
                        slot + 1);
  }

  vkEndCommandBuffer(m_computeCommandBuffers[m_currentFrame]);

  VkSubmitInfo computeSubmitInfo{};
  computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  computeSubmitInfo.commandBufferCount = 1;
  computeSubmitInfo.pCommandBuffers = &m_computeCommandBuffers[m_currentFrame];
  computeSubmitInfo.signalSemaphoreCount = 1;
  computeSubmitInfo.pSignalSemaphores =
      &m_computeFinishedSemaphores[m_currentFrame];

  if (vkQueueSubmit(m_computeQueue, 1, &computeSubmitInfo,
                    m_computeInFlightFences[m_currentFrame]) != VK_SUCCESS)
    throw std::runtime_error("Failed to submit compute command buffer");

  vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

  vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
  recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex, simState);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame],
                                  m_computeFinishedSemaphores[m_currentFrame]};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT};
  submitInfo.waitSemaphoreCount = 2;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

  VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo,
                    m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
    throw std::runtime_error("Failed to submit draw command buffer");

  m_queryPoolHasResults[m_currentFrame] = true;

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;

  VkSwapchainKHR swapChains[] = {m_swapChain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &imageIndex;

  VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
  if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
      presentResult == VK_SUBOPTIMAL_KHR) {
    return true;
  }

  if (simState.recordingFrames && m_frameExportBuffer) {
    vkDeviceWaitIdle(m_device);
    VkCommandBuffer cmd = beginSingleTimeCommands();
    VkImageMemoryBarrier barrierToSrc{};
    barrierToSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrierToSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrierToSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrierToSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToSrc.image = m_swapChainImages[imageIndex];
    barrierToSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrierToSrc.srcAccessMask = 0;
    barrierToSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrierToSrc);
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {m_swapChainExtent.width, m_swapChainExtent.height, 1};
    vkCmdCopyImageToBuffer(cmd, m_swapChainImages[imageIndex],
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_frameExportBuffer->getBuffer(), 1, &region);
    VkImageMemoryBarrier barrierToPresent{};
    barrierToPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrierToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrierToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrierToPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToPresent.image = m_swapChainImages[imageIndex];
    barrierToPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrierToPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrierToPresent.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrierToPresent);
    endSingleTimeCommands(cmd);
    const uint8_t *srcBytes = static_cast<const uint8_t *>(m_frameExportMapped);
    std::vector<uint8_t> pixels(m_swapChainExtent.width *
                                m_swapChainExtent.height * 4);
    if (m_swapChainImageFormat == VK_FORMAT_B8G8R8A8_UNORM ||
        m_swapChainImageFormat == VK_FORMAT_B8G8R8A8_SRGB) {
      for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = srcBytes[i + 2];
        pixels[i + 1] = srcBytes[i + 1];
        pixels[i + 2] = srcBytes[i + 0];
        pixels[i + 3] = srcBytes[i + 3];
      }
    } else {
      std::memcpy(pixels.data(), srcBytes, pixels.size());
    }
    {
      std::lock_guard<std::mutex> lock(m_frameQueueMutex);
      m_frameQueue.push(FrameData{std::move(pixels), m_swapChainExtent.width,
                                  m_swapChainExtent.height,
                                  m_frameExportIndex++});
    }
    m_frameQueueCV.notify_one();
  }

  m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
  return false;
}

// Particle management

void VulkanContext::initParticles(uint32_t numParticles) {
  std::vector<Particle> particles =
      generatePreset(PresetType::RandomCloud, numParticles);

  m_particleBuffers[0]->write(particles.data(),
                              numParticles * sizeof(Particle));
  m_particleBuffers[1]->write(particles.data(),
                              numParticles * sizeof(Particle));

  // Clear HDR image to black on first init
  VkCommandBuffer cmd = beginSingleTimeCommands();
  VkImageMemoryBarrier toGeneral{};
  toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toGeneral.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toGeneral.image = m_hdrImage;
  toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  toGeneral.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  toGeneral.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toGeneral);

  VkClearColorValue clearColor{};
  clearColor.float32[0] = 0.01f;
  clearColor.float32[1] = 0.02f;
  clearColor.float32[2] = 0.03f;
  clearColor.float32[3] = 1.0f;
  VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdClearColorImage(cmd, m_hdrImage, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1,
                       &range);

  VkImageMemoryBarrier toAttach{};
  toAttach.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toAttach.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  toAttach.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  toAttach.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toAttach.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toAttach.image = m_hdrImage;
  toAttach.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  toAttach.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toAttach.dstAccessMask =
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &toAttach);
  endSingleTimeCommands(cmd);
}

void VulkanContext::reloadParticles(const std::vector<Particle> &particles) {
  if (m_device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_device);
  }

  uint32_t numParticles = static_cast<uint32_t>(particles.size());
  if (numParticles != m_numParticles) {
    m_particleBuffers.clear();
    createShaderStorageBuffers(numParticles);

    if (m_descriptorPool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
      m_descriptorPool = VK_NULL_HANDLE;
    }
    createDescriptorPool();
    createDescriptorSets();
    createBloomDescriptorSets();
  }

  m_particleBuffers[0]->write(particles.data(),
                              numParticles * sizeof(Particle));
  m_particleBuffers[1]->write(particles.data(),
                              numParticles * sizeof(Particle));

  // Also clear the HDR image so trails don't persist after a preset reset
  VkCommandBuffer cmd = beginSingleTimeCommands();
  VkClearColorValue clearColor{};
  clearColor.float32[0] = 0.01f;
  clearColor.float32[1] = 0.02f;
  clearColor.float32[2] = 0.03f;
  clearColor.float32[3] = 1.0f;
  VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdClearColorImage(cmd, m_hdrImage, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1,
                       &range);
  endSingleTimeCommands(cmd);
}

void VulkanContext::readbackStats(SimState &simState) {
  void *mapped = m_particleBuffers[m_currentFrame]->map();
  const Particle *particles = static_cast<const Particle *>(mapped);
  double totalKE = 0.0;
  glm::dvec3 totalP{0.0, 0.0, 0.0};
  for (uint32_t i = 0; i < m_numParticles; ++i) {
    float mass = particles[i].pos.w;
    glm::vec3 vel = glm::vec3(particles[i].vel);
    double speedSqr = glm::dot(vel, vel);
    totalKE += 0.5 * mass * speedSqr;
    totalP += glm::dvec3(vel) * static_cast<double>(mass);
  }
  m_particleBuffers[m_currentFrame]->unmap();
  simState.totalKineticEnergy = static_cast<float>(totalKE);
  simState.totalMomentum = static_cast<float>(glm::length(totalP));
}

std::vector<Particle> VulkanContext::readbackParticles() {
  if (m_device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_device);
  }
  std::vector<Particle> particles(m_numParticles);
  void *mapped = m_particleBuffers[m_currentFrame]->map();
  std::memcpy(particles.data(), mapped, m_numParticles * sizeof(Particle));
  m_particleBuffers[m_currentFrame]->unmap();
  return particles;
}

void VulkanContext::appendParticle(const Particle &p) {
  std::vector<Particle> particles = readbackParticles();
  particles.push_back(p);
  reloadParticles(particles);
}

// ImGui

void VulkanContext::initImGui(GLFWwindow *window) {
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.maxSets = 1000 * IM_ARRAYSIZE(poolSizes);
  poolInfo.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(poolSizes));
  poolInfo.pPoolSizes = poolSizes;

  if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr,
                             &m_imguiDescriptorPool) != VK_SUCCESS)
    throw std::runtime_error("Failed to create ImGui descriptor pool");

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  ImGui::StyleColorsDark();

  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.PopupRounding = 6.0f;
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.07f, 0.1f, 0.85f);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.1f, 0.15f, 0.9f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.15f, 0.22f, 0.9f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.2f, 0.28f, 0.6f);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.26f, 0.36f, 0.8f);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.32f, 0.44f, 1.0f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.35f, 0.55f, 0.7f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.45f, 0.7f, 0.9f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.3f, 0.55f, 0.85f, 1.0f);

  ImGui_ImplGlfw_InitForVulkan(window, true);
  ImGui_ImplVulkan_InitInfo initInfo{};
  initInfo.Instance = m_instance;
  initInfo.PhysicalDevice = m_physicalDevice;
  initInfo.Device = m_device;
  initInfo.QueueFamily =
      findQueueFamilies(m_physicalDevice).graphicsFamily.value();
  initInfo.Queue = m_graphicsQueue;
  initInfo.PipelineCache = VK_NULL_HANDLE;
  initInfo.DescriptorPool = m_imguiDescriptorPool;
  initInfo.MinImageCount = 2;
  initInfo.ImageCount = static_cast<uint32_t>(m_swapChainImages.size());
  initInfo.Allocator = nullptr;
  initInfo.CheckVkResultFn = nullptr;

  // ImGui renders in the composite render pass (targeting swapchain)
  initInfo.PipelineInfoMain.RenderPass = m_compositeRenderPass;
  initInfo.PipelineInfoMain.Subpass = 0;
  initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

  ImGui_ImplVulkan_Init(&initInfo);
}

void VulkanContext::shutdownImGui() {
  if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }
}

void VulkanContext::createFrameExportStagingBuffer() {
  VkDeviceSize bufferSize =
      m_swapChainExtent.width * m_swapChainExtent.height * 4;
  m_frameExportBuffer = std::make_unique<Buffer>(
      m_device, m_physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  m_frameExportMapped = m_frameExportBuffer->map();
}

void VulkanContext::destroyFrameExportStagingBuffer() {
  if (m_frameExportBuffer) {
    m_frameExportBuffer->unmap();
    m_frameExportBuffer.reset();
  }
  m_frameExportMapped = nullptr;
}

void VulkanContext::startFrameWriterThread() {
  m_frameWriterRunning = true;
  m_frameWriteThread = std::thread([this]() {
    while (m_frameWriterRunning) {
      FrameData data;
      {
        std::unique_lock<std::mutex> lock(m_frameQueueMutex);
        m_frameQueueCV.wait(lock, [this]() {
          return !m_frameQueue.empty() || !m_frameWriterRunning;
        });
        if (!m_frameWriterRunning && m_frameQueue.empty()) {
          break;
        }
        data = std::move(m_frameQueue.front());
        m_frameQueue.pop();
      }
      char filename[256];
      std::snprintf(filename, sizeof(filename), "frames/frame_%04u.png",
                    data.index);
#ifdef _WIN32
      _mkdir("frames");
#else
      mkdir("frames", 0777);
#endif
      stbi_write_png(filename, data.width, data.height, 4, data.pixels.data(),
                     data.width * 4);
    }
  });
}

void VulkanContext::stopFrameWriterThread() {
  m_frameWriterRunning = false;
  m_frameQueueCV.notify_all();
  if (m_frameWriteThread.joinable()) {
    m_frameWriteThread.join();
  }
  std::queue<FrameData> empty;
  std::swap(m_frameQueue, empty);
}
