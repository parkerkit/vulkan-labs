#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace fw {
class Device;
struct SwapchainDesc {
  VkExtent2D extent{};
  bool enable_vsync{true};
};
struct SwapchainInfo {
  VkExtent2D extent{};
  VkFormat format{VK_FORMAT_UNDEFINED};
  VkColorSpaceKHR color_space{};
  VkPresentModeKHR present_mode{};
  uint32_t image_count{};      ///< 驱动实际返回的交换链图像数量。
  uint32_t min_image_count{};  ///< 创建交换链时请求的最少图像数，可能小于实际数量。
};

/// @brief 管理交换链及呈现图像视图，图像本身借用自 Vulkan。
/// @details 深度附件、渲染通道和帧缓冲由调用者管理，释放交换链前须等待队列完成使用。
class Swapchain final {
public:
  ~Swapchain() noexcept;
  Swapchain(const Swapchain&) = delete;
  Swapchain& operator=(const Swapchain&) = delete;
  Swapchain(Swapchain&&) = delete;
  Swapchain& operator=(Swapchain&&) = delete;

  /// @brief 底层交换链创建成功后，old_swapchain 即被停用，即使后续视图创建失败也不能恢复使用。
  [[nodiscard]] static std::unique_ptr<Swapchain> Create(Device& device, const SwapchainDesc& desc,
                                                         VkSwapchainKHR old_swapchain = VK_NULL_HANDLE);
  [[nodiscard]] VkSwapchainKHR GetHandle() const noexcept;
  [[nodiscard]] const SwapchainInfo& GetInfo() const noexcept;
  [[nodiscard]] VkImage GetImage(uint32_t index) const;
  [[nodiscard]] VkImageView GetImageView(uint32_t index) const;

private:
  explicit Swapchain(Device& device);
  void Initialize(const SwapchainDesc& desc, VkSwapchainKHR old_swapchain);

  Device& m_device;
  VkSwapchainKHR m_swapchain{};
  SwapchainInfo m_info{};
  std::vector<VkImage> m_images{};
  std::vector<VkImageView> m_views{};
};
}  // namespace fw
