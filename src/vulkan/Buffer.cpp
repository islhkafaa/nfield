#include "vulkan/Buffer.hpp"
#include <cstring>
#include <stdexcept>

Buffer::Buffer(VkDevice device, VkPhysicalDevice physicalDevice,
               VkDeviceSize size, VkBufferUsageFlags usage,
               VkMemoryPropertyFlags properties)
    : m_device(device), m_size(size) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create buffer");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(m_device, m_buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      physicalDevice, memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory) !=
      VK_SUCCESS) {
    vkDestroyBuffer(m_device, m_buffer, nullptr);
    throw std::runtime_error("Failed to allocate buffer memory");
  }

  vkBindBufferMemory(m_device, m_buffer, m_memory, 0);

  if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    map();
  }
}

Buffer::~Buffer() { cleanup(); }

Buffer::Buffer(Buffer &&other) noexcept
    : m_device(other.m_device), m_buffer(other.m_buffer),
      m_memory(other.m_memory), m_size(other.m_size), m_mapped(other.m_mapped) {
  other.m_device = VK_NULL_HANDLE;
  other.m_buffer = VK_NULL_HANDLE;
  other.m_memory = VK_NULL_HANDLE;
  other.m_size = 0;
  other.m_mapped = nullptr;
}

Buffer &Buffer::operator=(Buffer &&other) noexcept {
  if (this != &other) {
    cleanup();
    m_device = other.m_device;
    m_buffer = other.m_buffer;
    m_memory = other.m_memory;
    m_size = other.m_size;
    m_mapped = other.m_mapped;

    other.m_device = VK_NULL_HANDLE;
    other.m_buffer = VK_NULL_HANDLE;
    other.m_memory = VK_NULL_HANDLE;
    other.m_size = 0;
    other.m_mapped = nullptr;
  }
  return *this;
}

void Buffer::cleanup() {
  if (m_mapped && m_memory != VK_NULL_HANDLE) {
    vkUnmapMemory(m_device, m_memory);
    m_mapped = nullptr;
  }
  if (m_buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(m_device, m_buffer, nullptr);
    m_buffer = VK_NULL_HANDLE;
  }
  if (m_memory != VK_NULL_HANDLE) {
    vkFreeMemory(m_device, m_memory, nullptr);
    m_memory = VK_NULL_HANDLE;
  }
}

void *Buffer::map() {
  if (!m_mapped && m_memory != VK_NULL_HANDLE) {
    if (vkMapMemory(m_device, m_memory, 0, m_size, 0, &m_mapped) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to map buffer memory");
    }
  }
  return m_mapped;
}

void Buffer::unmap() {
  // Keeping memory persistently mapped for performance; actual unmap happens in
  // cleanup()
}

void Buffer::write(const void *data, VkDeviceSize size, VkDeviceSize offset) {
  bool alreadyMapped = (m_mapped != nullptr);
  void *mapPtr = alreadyMapped ? m_mapped : map();
  std::memcpy(static_cast<char *>(mapPtr) + offset, data, size);
  if (!alreadyMapped) {
    unmap();
  }
}

void Buffer::copyTo(VkBuffer dstBuffer, VkQueue queue,
                    VkCommandPool commandPool) {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  VkBufferCopy copyRegion{};
  copyRegion.srcOffset = 0;
  copyRegion.dstOffset = 0;
  copyRegion.size = m_size;
  vkCmdCopyBuffer(commandBuffer, m_buffer, dstBuffer, 1, &copyRegion);

  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);

  vkFreeCommandBuffers(m_device, commandPool, 1, &commandBuffer);
}

uint32_t Buffer::findMemoryType(VkPhysicalDevice physicalDevice,
                                uint32_t typeFilter,
                                VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }

  // Fallback if HOST_CACHED_BIT is requested but not supported
  if (properties & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
    VkMemoryPropertyFlags fallbackProperties =
        properties & ~VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
      if ((typeFilter & (1 << i)) &&
          (memProperties.memoryTypes[i].propertyFlags & fallbackProperties) ==
              fallbackProperties) {
        return i;
      }
    }
  }

  throw std::runtime_error("Failed to find suitable memory type");
}
