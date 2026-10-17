#include "framework/buffer.h"

#include <vk_mem_alloc.h>

#include <cstring>
#include <utility>

#include "framework/device.h"
#include "vulkan_helpers.h"

namespace fw {
Buffer Buffer::Create(Device& device, const BufferDesc& desc) {
  detail::Require(desc.size > 0 && desc.usage != 0, "Buffer size and usage must be nonzero");
  Buffer buffer{};
  buffer.m_device = &device;
  buffer.m_size = desc.size;
  VkBufferCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .size = desc.size,
    .usage = desc.usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = {},
    .pQueueFamilyIndices = nullptr,
  };
  VmaAllocationCreateInfo allocation{
    .flags = {},
    .usage = VMA_MEMORY_USAGE_AUTO,
    .requiredFlags = {},
    .preferredFlags = {},
    .memoryTypeBits = {},
    .pool = {},
    .pUserData = nullptr,
    .priority = {},
  };
  if (desc.memory_usage != MemoryUsage::Device) {
    // 上传按顺序写入，回读允许随机访问；两者都申请持久映射以复用主机地址。
    allocation.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | (desc.memory_usage == MemoryUsage::Upload
                                                             ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                                                             : VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
  } else {
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  }
  VmaAllocationInfo allocation_info{};
  detail::Check(vmaCreateBuffer(device.GetAllocator(), &info, &allocation, &buffer.m_buffer, &buffer.m_allocation,
                                &allocation_info),
                "Create buffer");
  buffer.m_mapped = allocation_info.pMappedData;
  return buffer;
}

Buffer::~Buffer() noexcept { Release(); }
Buffer::Buffer(Buffer&& other) noexcept { *this = std::move(other); }
Buffer& Buffer::operator=(Buffer&& other) noexcept {
  if (this != &other) {
    Release();
    m_device = std::exchange(other.m_device, nullptr);
    m_buffer = std::exchange(other.m_buffer, VK_NULL_HANDLE);
    m_allocation = std::exchange(other.m_allocation, nullptr);
    m_mapped = std::exchange(other.m_mapped, nullptr);
    m_size = std::exchange(other.m_size, 0);
  }
  return *this;
}
void Buffer::Release() noexcept {
  if (m_buffer != VK_NULL_HANDLE) {
    vmaDestroyBuffer(m_device->GetAllocator(), m_buffer, m_allocation);
  }
  m_buffer = VK_NULL_HANDLE;
  m_allocation = nullptr;
  m_device = nullptr;
  m_mapped = nullptr;
  m_size = 0;
}

void Buffer::Write(std::span<const std::byte> bytes, VkDeviceSize offset) {
  detail::Require(m_mapped != nullptr, "Buffer is not host mapped");
  detail::Require(offset <= m_size && bytes.size_bytes() <= m_size - offset, "Buffer write exceeds allocation");
  if (!bytes.empty()) {
    std::memcpy(static_cast<std::byte*>(m_mapped) + offset, bytes.data(), bytes.size_bytes());
    detail::Check(vmaFlushAllocation(m_device->GetAllocator(), m_allocation, offset, bytes.size_bytes()),
                  "Flush mapped buffer");
  }
}
void Buffer::Read(std::span<std::byte> bytes, VkDeviceSize offset) const {
  detail::Require(m_mapped != nullptr, "Buffer is not host mapped");
  detail::Require(offset <= m_size && bytes.size_bytes() <= m_size - offset, "Buffer read exceeds allocation");
  if (!bytes.empty()) {
    detail::Check(vmaInvalidateAllocation(m_device->GetAllocator(), m_allocation, offset, bytes.size_bytes()),
                  "Invalidate mapped buffer");
    std::memcpy(bytes.data(), static_cast<const std::byte*>(m_mapped) + offset, bytes.size_bytes());
  }
}
VkBuffer Buffer::GetHandle() const noexcept { return m_buffer; }
VkDeviceSize Buffer::GetSize() const noexcept { return m_size; }
}  // namespace fw
