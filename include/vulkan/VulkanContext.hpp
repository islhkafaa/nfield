#pragma once

#define GLFW_INCLUDE_VULKAN
#include "vulkan/Buffer.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <vector>

struct QueueFamilyIndices {
  std::optional<uint32_t> graphicsFamily;
  std::optional<uint32_t> computeFamily;
  std::optional<uint32_t> presentFamily;

  bool isComplete() const {
    return graphicsFamily.has_value() && computeFamily.has_value() &&
           presentFamily.has_value();
  }
};

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

struct Particle {
  alignas(16) glm::vec4 pos;
  alignas(16) glm::vec4 vel;
};

class VulkanContext {
public:
  VulkanContext() = default;
  ~VulkanContext() = default;

  VulkanContext(const VulkanContext &) = delete;
  VulkanContext &operator=(const VulkanContext &) = delete;
  VulkanContext(VulkanContext &&) noexcept = default;
  VulkanContext &operator=(VulkanContext &&) noexcept = default;

  void init(GLFWwindow *window);
  void cleanup();

  void drawFrame(const glm::mat4 &viewProj, float dt, float G,
                 float softeningSqr);
  void initParticles(uint32_t numParticles);

  VkInstance getInstance() const { return m_instance; }
  VkDevice getDevice() const { return m_device; }
  VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
  VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
  VkQueue getComputeQueue() const { return m_computeQueue; }
  VkQueue getPresentQueue() const { return m_presentQueue; }
  VkSurfaceKHR getSurface() const { return m_surface; }
  VkSwapchainKHR getSwapChain() const { return m_swapChain; }
  const std::vector<VkImage> &getSwapChainImages() const {
    return m_swapChainImages;
  }
  VkFormat getSwapChainImageFormat() const { return m_swapChainImageFormat; }
  VkExtent2D getSwapChainExtent() const { return m_swapChainExtent; }
  const std::vector<VkImageView> &getSwapChainImageViews() const {
    return m_swapChainImageViews;
  }

private:
  void createInstance();
  void setupDebugMessenger();
  void createSurface(GLFWwindow *window);
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createSwapChain(GLFWwindow *window);
  void createImageViews();

  void createRenderPass();
  void createFramebuffers();
  void createCommandPool();
  void createCommandBuffers();
  void createSyncPrimitives();
  void createDescriptorSetLayouts();
  void createDescriptorPool();
  void createDescriptorSets();
  void createPipelines();
  void createShaderStorageBuffers(uint32_t numParticles);
  void createUniformBuffers();

  void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

  bool isDeviceSuitable(VkPhysicalDevice device);
  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<VkSurfaceFormatKHR> &availableFormats);
  VkPresentModeKHR chooseSwapPresentMode(
      const std::vector<VkPresentModeKHR> &availablePresentModes);
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities,
                              GLFWwindow *window);

  VkInstance m_instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;

  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;

  VkQueue m_graphicsQueue = VK_NULL_HANDLE;
  VkQueue m_computeQueue = VK_NULL_HANDLE;
  VkQueue m_presentQueue = VK_NULL_HANDLE;

  VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
  std::vector<VkImage> m_swapChainImages;
  std::vector<VkImageView> m_swapChainImageViews;
  VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
  VkExtent2D m_swapChainExtent = {0, 0};

  VkRenderPass m_renderPass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> m_swapChainFramebuffers;
  VkCommandPool m_commandPool = VK_NULL_HANDLE;
  VkCommandPool m_computeCommandPool = VK_NULL_HANDLE;

  static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
  std::vector<VkCommandBuffer> m_commandBuffers;
  std::vector<VkCommandBuffer> m_computeCommandBuffers;

  std::vector<VkSemaphore> m_imageAvailableSemaphores;
  std::vector<VkSemaphore> m_renderFinishedSemaphores;
  std::vector<VkFence> m_inFlightFences;

  std::vector<VkSemaphore> m_computeFinishedSemaphores;
  std::vector<VkFence> m_computeInFlightFences;

  VkDescriptorSetLayout m_graphicsDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_computeDescriptorSetLayout = VK_NULL_HANDLE;

  VkPipelineLayout m_graphicsPipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

  VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_computePipeline = VK_NULL_HANDLE;

  VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

  std::vector<std::unique_ptr<Buffer>> m_particleBuffers;
  std::vector<std::unique_ptr<Buffer>> m_cameraUBOs;

  std::vector<VkDescriptorSet> m_graphicsDescriptorSets;
  std::vector<VkDescriptorSet> m_computeDescriptorSets;

  uint32_t m_currentFrame = 0;
  uint32_t m_numParticles = 0;
};
