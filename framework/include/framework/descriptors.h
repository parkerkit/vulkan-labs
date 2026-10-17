#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace fw {
class Device;

/// @brief 管理描述符集合布局，仅允许移动；Device 必须比布局存活更久。
class DescriptorSetLayout final {
public:
  DescriptorSetLayout() = default;
  ~DescriptorSetLayout() noexcept;
  DescriptorSetLayout(const DescriptorSetLayout&) = delete;
  DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;
  DescriptorSetLayout(DescriptorSetLayout&& other) noexcept;
  DescriptorSetLayout& operator=(DescriptorSetLayout&& other) noexcept;

  [[nodiscard]] static DescriptorSetLayout Create(Device& device,
                                                  std::span<const VkDescriptorSetLayoutBinding> bindings);
  [[nodiscard]] VkDescriptorSetLayout GetHandle() const noexcept;

private:
  void Release() noexcept;

  VkDevice m_device{};
  VkDescriptorSetLayout m_layout{};
};

struct DescriptorPoolDesc {
  uint32_t max_sets{};
  std::span<const VkDescriptorPoolSize> sizes{};  ///< 仅在 Create 调用期间借用。
};

/// @brief 管理描述符池及其分配的集合，重置或销毁前必须等待 GPU 完成对集合的使用。
/// @details 持久集合与随帧槽位重置的临时集合应使用不同的池，Device 必须比池存活更久。
class DescriptorPool final {
public:
  DescriptorPool() = default;
  ~DescriptorPool() noexcept;
  DescriptorPool(const DescriptorPool&) = delete;
  DescriptorPool& operator=(const DescriptorPool&) = delete;
  DescriptorPool(DescriptorPool&& other) noexcept;
  DescriptorPool& operator=(DescriptorPool&& other) noexcept;

  [[nodiscard]] static DescriptorPool Create(Device& device, const DescriptorPoolDesc& desc);
  /// @brief 分配由池统一管理的集合，池重置或销毁后返回的句柄失效，不单独释放集合。
  [[nodiscard]] VkDescriptorSet Allocate(VkDescriptorSetLayout layout);
  /// @brief 使该池分配的全部集合失效并回收容量，不负责等待 GPU。
  void Reset();
  [[nodiscard]] VkDescriptorPool GetHandle() const noexcept;

private:
  void Release() noexcept;

  VkDevice m_device{};
  VkDescriptorPool m_pool{};
};
}  // namespace fw
