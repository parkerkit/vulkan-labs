#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

struct VmaAllocation_T;

namespace fw {
class Device;
struct ImageDesc {
  VkExtent2D extent{};
  VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
  VkImageUsageFlags usage{VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
  VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
  uint32_t mip_levels{1};
  uint32_t array_layers{1};
  VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
};

/// @brief 管理二维图像、VMA 分配及覆盖全部 mip 和数组层的视图，不隐式转换图像布局。
/// @details Device 必须比图像存活更久；析构及移动赋值前须确保 GPU 已完成对被释放资源的使用。
class Image final {
public:
  Image() = default;
  ~Image() noexcept;
  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;
  Image(Image&& other) noexcept;
  Image& operator=(Image&& other) noexcept;

  [[nodiscard]] static Image Create(Device& device, const ImageDesc& desc);
  [[nodiscard]] static VkFormat FindDepthFormat(const Device& device);
  [[nodiscard]] VkImage GetHandle() const noexcept;
  [[nodiscard]] VkImageView GetView() const noexcept;
  [[nodiscard]] const ImageDesc& GetDesc() const noexcept;

private:
  void Release() noexcept;

  Device* m_device{};
  VkImage m_image{};
  VkImageView m_view{};
  VmaAllocation_T* m_allocation{};
  ImageDesc m_desc{};
};
}  // namespace fw
