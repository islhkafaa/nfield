#pragma once

#define GLFW_INCLUDE_VULKAN
#include "core/SimState.hpp"
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

struct alignas(16) BHNode {
  glm::vec4 centerOfMass; // xyz = position, w = mass
  glm::vec4 bounds;       // xyz = center, w = size (width)
  int children[8];        // children indices
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

  void drawFrame(const glm::mat4 &viewProj, SimState &simState);
  void initParticles(uint32_t numParticles);
  void reloadParticles(const std::vector<Particle> &particles);

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

  void createParticleRenderPass();  // renders particles -> HDR offscreen image
  void createCompositeRenderPass(); // tone-maps HDR -> swapchain image
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

  void createHDRImage();
  void createBloomImages();
  void createBloomDescriptorSets();
  void createTimestampPool();

  void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                           const SimState &simState);

  bool isDeviceSuitable(VkPhysicalDevice device);
  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<VkSurfaceFormatKHR> &availableFormats);
  VkPresentModeKHR chooseSwapPresentMode(
      const std::vector<VkPresentModeKHR> &availablePresentModes);
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities,
                              GLFWwindow *window);

  // Helper: allocate and bind memory for a VkImage
  VkDeviceMemory allocateImageMemory(VkImage image,
                                     VkMemoryPropertyFlags props);

  // Helper: one-shot command submission on graphics queue
  VkCommandBuffer beginSingleTimeCommands();
  void endSingleTimeCommands(VkCommandBuffer cmd);

  // Core Vulkan
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

  // Render Passes
  VkRenderPass m_particleRenderPass = VK_NULL_HANDLE;  // → HDR offscreen
  VkRenderPass m_compositeRenderPass = VK_NULL_HANDLE; // → swapchain

  // Framebuffers
  VkFramebuffer m_hdrFramebuffer = VK_NULL_HANDLE;    // particle render target
  std::vector<VkFramebuffer> m_swapChainFramebuffers; // composite targets

  // Command infrastructure
  VkCommandPool m_commandPool = VK_NULL_HANDLE;
  VkCommandPool m_computeCommandPool = VK_NULL_HANDLE;

  static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
  std::vector<VkCommandBuffer> m_commandBuffers;
  std::vector<VkCommandBuffer> m_computeCommandBuffers;

  // Synchronisation
  std::vector<VkSemaphore> m_imageAvailableSemaphores;
  std::vector<VkSemaphore> m_renderFinishedSemaphores;
  std::vector<VkFence> m_inFlightFences;

  std::vector<VkSemaphore> m_computeFinishedSemaphores;
  std::vector<VkFence> m_computeInFlightFences;

  // Descriptor infrastructure
  VkDescriptorSetLayout m_graphicsDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_computeDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_bloomDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_trailDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
  VkDescriptorPool m_imguiDescriptorPool = VK_NULL_HANDLE;

  std::vector<VkDescriptorSet> m_graphicsDescriptorSets;
  std::vector<VkDescriptorSet> m_computeDescriptorSets;

  // Bloom compute descriptor sets: threshold (src=HDR->bloomA), blur H
  // (bloomA->bloomB), blur V (bloomB->bloomA)
  VkDescriptorSet m_bloomThresholdDS = VK_NULL_HANDLE; // HDR -> bloomA
  VkDescriptorSet m_bloomBlurHDS = VK_NULL_HANDLE;     // bloomA -> bloomB
  VkDescriptorSet m_bloomBlurVDS = VK_NULL_HANDLE;     // bloomB -> bloomA
  // Composite and trail descriptor sets each frame (sampler-based)
  VkDescriptorSet m_compositeDS = VK_NULL_HANDLE; // HDR + bloomA -> swapchain
  VkDescriptorSet m_trailDS = VK_NULL_HANDLE;     // HDR -> HDR (decay)

  // Simulation pipelines
  VkPipelineLayout m_graphicsPipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

  VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_computePipeline = VK_NULL_HANDLE;
  VkPipeline m_bhComputePipeline = VK_NULL_HANDLE;

  // Post-processing pipelines
  VkPipelineLayout m_bloomComputeLayout = VK_NULL_HANDLE;
  VkPipeline m_bloomThresholdPipeline = VK_NULL_HANDLE;
  VkPipeline m_bloomBlurPipeline = VK_NULL_HANDLE;

  VkPipelineLayout m_compositeLayout = VK_NULL_HANDLE;
  VkPipeline m_compositePipeline = VK_NULL_HANDLE;

  VkPipelineLayout m_trailLayout = VK_NULL_HANDLE;
  VkPipeline m_trailPipeline = VK_NULL_HANDLE;

  // HDR offscreen image
  VkImage m_hdrImage = VK_NULL_HANDLE;
  VkImageView m_hdrImageView = VK_NULL_HANDLE;
  VkDeviceMemory m_hdrMemory = VK_NULL_HANDLE;
  VkSampler m_hdrSampler = VK_NULL_HANDLE;

  // Bloom ping-pong images
  VkImage m_bloomImageA = VK_NULL_HANDLE;
  VkImageView m_bloomImageViewA = VK_NULL_HANDLE;
  VkDeviceMemory m_bloomMemoryA = VK_NULL_HANDLE;
  VkSampler m_bloomSamplerA = VK_NULL_HANDLE;

  VkImage m_bloomImageB = VK_NULL_HANDLE;
  VkImageView m_bloomImageViewB = VK_NULL_HANDLE;
  VkDeviceMemory m_bloomMemoryB = VK_NULL_HANDLE;
  VkSampler m_bloomSamplerB = VK_NULL_HANDLE;

  // Simulation buffers
  std::vector<std::unique_ptr<Buffer>> m_particleBuffers;
  std::vector<std::unique_ptr<Buffer>> m_cameraUBOs;
  std::vector<std::unique_ptr<Buffer>> m_bhTreeBuffers;

  uint32_t m_currentFrame = 0;
  uint32_t m_numParticles = 0;

  // GPU Timestamps
  VkQueryPool m_timestampPool = VK_NULL_HANDLE;
  float m_timestampPeriod = 0.0f; // ns per tick
  bool m_timestampSupported = false;
  bool m_queryPoolHasResults[MAX_FRAMES_IN_FLIGHT] = {false, false};
  // 4 slots per frame-in-flight: [computeBegin, computeEnd, gfxBegin, gfxEnd]
  static constexpr uint32_t TIMESTAMPS_PER_FRAME = 4;

  void initImGui(GLFWwindow *window);
  void shutdownImGui();
};
