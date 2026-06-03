#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace PipelineHelper {
std::vector<char> readShaderFile(const std::string &filename);
VkShaderModule createShaderModule(VkDevice device,
                                  const std::vector<char> &code);

struct GraphicsPipelineConfig {
  VkDevice device;
  VkRenderPass renderPass;
  VkPipelineLayout pipelineLayout;
  std::string vertexShaderPath;
  std::string fragmentShaderPath;
  VkExtent2D extent;
};

VkPipeline createGraphicsPipeline(const GraphicsPipelineConfig &config);

struct FullscreenPipelineConfig {
  VkDevice device;
  VkRenderPass renderPass;
  VkPipelineLayout pipelineLayout;
  std::string vertexShaderPath; // fullscreen.vert
  std::string fragmentShaderPath;
  VkExtent2D extent;
  bool additiveBlend = false; // true for composite pass
};

VkPipeline createFullscreenPipeline(const FullscreenPipelineConfig &config);

struct ComputePipelineConfig {
  VkDevice device;
  VkPipelineLayout pipelineLayout;
  std::string computeShaderPath;
};

VkPipeline createComputePipeline(const ComputePipelineConfig &config);
} // namespace PipelineHelper
