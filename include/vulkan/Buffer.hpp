#pragma once

#include <vulkan/vulkan.h>

class Buffer {
public:
  Buffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
         VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
  ~Buffer();

  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
  Buffer(Buffer &&other) noexcept;
  Buffer &operator=(Buffer &&other) noexcept;

  void cleanup();

  VkBuffer getBuffer() const { return m_buffer; }
  VkDeviceMemory getMemory() const { return m_memory; }
  VkDeviceSize getSize() const { return m_size; }

  void *map();
  void unmap();
  void write(const void *data, VkDeviceSize size, VkDeviceSize offset = 0);
  void copyTo(VkBuffer dstBuffer, VkQueue queue, VkCommandPool commandPool);

  static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                                 uint32_t typeFilter,
                                 VkMemoryPropertyFlags properties);

private:
  VkDevice m_device = VK_NULL_HANDLE;
  VkBuffer m_buffer = VK_NULL_HANDLE;
  VkDeviceMemory m_memory = VK_NULL_HANDLE;
  VkDeviceSize m_size = 0;
  void *m_mapped = nullptr;
};
