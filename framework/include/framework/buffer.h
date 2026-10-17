#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>

struct VmaAllocation_T;

namespace fw {
class Device;
/// @brief 设备内存优先供 GPU 使用；上传和回读内存提供持久映射，分别面向 CPU 写入和读取。
enum class MemoryUsage { Device, Upload, Readback };

struct BufferDesc {
  VkDeviceSize size{};
  VkBufferUsageFlags usage{};
  MemoryUsage memory_usage{MemoryUsage::Device};
};

/// @brief 管理缓冲区及其 VMA 分配，仅允许移动；Device 必须比缓冲区存活更久。
/// @details 析构及移动赋值不等待 GPU，释放被替换资源前必须确保 GPU 已完成使用。
class Buffer final {
public:
  Buffer() = default;
  ~Buffer() noexcept;
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&& other) noexcept;
  Buffer& operator=(Buffer&& other) noexcept;

  [[nodiscard]] static Buffer Create(Device& device, const BufferDesc& desc);
  /// @brief 检查范围后写入映射内存并刷新缓存，CPU/GPU 访问顺序由调用者同步。
  /// @param offset 相对缓冲区起点的字节偏移；缓冲区必须已映射且写入不能越界。
  void Write(std::span<const std::byte> bytes, VkDeviceSize offset = 0);
  /// @brief 使主机缓存失效后读取映射内存，调用前必须等待产生数据的 GPU 提交完成。
  /// @param offset 相对缓冲区起点的字节偏移；缓冲区必须已映射且读取不能越界。
  void Read(std::span<std::byte> bytes, VkDeviceSize offset = 0) const;
  [[nodiscard]] VkBuffer GetHandle() const noexcept;
  [[nodiscard]] VkDeviceSize GetSize() const noexcept;

private:
  void Release() noexcept;

  Device* m_device{};
  VkBuffer m_buffer{};
  VmaAllocation_T* m_allocation{};
  void* m_mapped{};
  VkDeviceSize m_size{};
};
}  // namespace fw
