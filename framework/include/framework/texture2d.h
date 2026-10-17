#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>

#include "framework/image.h"

namespace fw {
class Device;

/// @brief 管理二维颜色纹理及 sampler，仅允许移动，Device 必须比纹理存活更久。
/// @details 析构和移动赋值不等待 GPU，释放前须确保 GPU 已完成对被释放资源的使用。
class Texture2D final {
public:
  Texture2D() = default;
  ~Texture2D() noexcept;
  Texture2D(const Texture2D&) = delete;
  Texture2D& operator=(const Texture2D&) = delete;
  Texture2D(Texture2D&& other) noexcept;
  Texture2D& operator=(Texture2D&& other) noexcept;

  /// @brief 解码图片为 RGBA8，并同步上传至 sRGB 图像，失败时抛出异常。
  /// @details 使用单层 mip 和 linear filtering，U 方向 repeat，V 方向 clamp。
  /// 返回时图像处于 SHADER_READ_ONLY_OPTIMAL，可通过描述符供 shader 采样。
  [[nodiscard]] static Texture2D Load(Device& device, const std::filesystem::path& path);
  [[nodiscard]] VkImageView GetView() const noexcept;
  [[nodiscard]] VkSampler GetSampler() const noexcept;
  [[nodiscard]] VkExtent2D GetExtent() const noexcept;

private:
  void Release() noexcept;

  VkDevice m_device{};
  Image m_image{};
  VkSampler m_sampler{};
};
}  // namespace fw
