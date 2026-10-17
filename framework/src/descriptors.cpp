#include "framework/descriptors.h"

#include <cstdint>
#include <utility>

#include "framework/device.h"
#include "vulkan_helpers.h"

namespace fw {
DescriptorSetLayout DescriptorSetLayout::Create(Device& device,
                                                std::span<const VkDescriptorSetLayoutBinding> bindings) {
  DescriptorSetLayout layout{};
  layout.m_device = device.GetHandle();
  VkDescriptorSetLayoutCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .bindingCount = static_cast<uint32_t>(bindings.size()),
    .pBindings = bindings.data(),
  };
  detail::Check(vkCreateDescriptorSetLayout(layout.m_device, &info, nullptr, &layout.m_layout),
                "Create descriptor set layout");
  return layout;
}
DescriptorSetLayout::~DescriptorSetLayout() noexcept { Release(); }
DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout&& other) noexcept { *this = std::move(other); }
DescriptorSetLayout& DescriptorSetLayout::operator=(DescriptorSetLayout&& other) noexcept {
  if (this != &other) {
    Release();
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
  }
  return *this;
}
void DescriptorSetLayout::Release() noexcept {
  if (m_layout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
  }
  m_layout = VK_NULL_HANDLE;
}
VkDescriptorSetLayout DescriptorSetLayout::GetHandle() const noexcept { return m_layout; }

DescriptorPool DescriptorPool::Create(Device& device, const DescriptorPoolDesc& desc) {
  detail::Require(desc.max_sets > 0, "Descriptor pool capacity must be nonzero");
  DescriptorPool pool{};
  pool.m_device = device.GetHandle();
  VkDescriptorPoolCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .maxSets = desc.max_sets,
    .poolSizeCount = static_cast<uint32_t>(desc.sizes.size()),
    .pPoolSizes = desc.sizes.data(),
  };
  detail::Check(vkCreateDescriptorPool(pool.m_device, &info, nullptr, &pool.m_pool), "Create descriptor pool");
  return pool;
}
DescriptorPool::~DescriptorPool() noexcept { Release(); }
DescriptorPool::DescriptorPool(DescriptorPool&& other) noexcept { *this = std::move(other); }
DescriptorPool& DescriptorPool::operator=(DescriptorPool&& other) noexcept {
  if (this != &other) {
    Release();
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_pool = std::exchange(other.m_pool, VK_NULL_HANDLE);
  }
  return *this;
}
void DescriptorPool::Release() noexcept {
  if (m_pool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(m_device, m_pool, nullptr);
  }
  m_pool = VK_NULL_HANDLE;
}
VkDescriptorSet DescriptorPool::Allocate(VkDescriptorSetLayout layout) {
  detail::Require(m_pool != VK_NULL_HANDLE && layout != VK_NULL_HANDLE, "Invalid descriptor pool or layout");
  VkDescriptorSetAllocateInfo info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .pNext = nullptr,
    .descriptorPool = m_pool,
    .descriptorSetCount = 1,
    .pSetLayouts = &layout,
  };
  VkDescriptorSet set{};
  detail::Check(vkAllocateDescriptorSets(m_device, &info, &set), "Allocate descriptor set");
  return set;
}
void DescriptorPool::Reset() {
  detail::Require(m_pool != VK_NULL_HANDLE, "Descriptor pool is not initialized");
  detail::Check(vkResetDescriptorPool(m_device, m_pool, 0), "Reset descriptor pool");
}
VkDescriptorPool DescriptorPool::GetHandle() const noexcept { return m_pool; }
}  // namespace fw
