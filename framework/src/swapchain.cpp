#include "framework/swapchain.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

#include "framework/device.h"
#include "vulkan_helpers.h"

namespace fw {
namespace {
VkSurfaceFormatKHR ChooseFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
  if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
    return {.format = VK_FORMAT_B8G8R8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  }
  for (VkFormat preferred : std::array{VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB}) {
    const auto found = std::ranges::find_if(formats, [preferred](const VkSurfaceFormatKHR& format) {
      return format.format == preferred && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (found != formats.end()) {
      return *found;
    }
  }
  return formats.front();
}
VkCompositeAlphaFlagBitsKHR ChooseAlpha(VkCompositeAlphaFlagsKHR supported) {
  for (auto alpha : std::array{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                               VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
    if ((supported & alpha) != 0) {
      return alpha;
    }
  }
  throw std::runtime_error("Surface has no supported composite alpha mode");
}
}  // namespace

Swapchain::Swapchain(Device& device) : m_device{device} {}
std::unique_ptr<Swapchain> Swapchain::Create(Device& device, const SwapchainDesc& desc, VkSwapchainKHR old_swapchain) {
  auto swapchain = std::unique_ptr<Swapchain>{new Swapchain{device}};
  swapchain->Initialize(desc, old_swapchain);
  return swapchain;
}
void Swapchain::Initialize(const SwapchainDesc& desc, VkSwapchainKHR old_swapchain) {
  detail::Require(desc.extent.width > 0 && desc.extent.height > 0, "Cannot create a zero-sized swapchain");
  VkSurfaceCapabilitiesKHR capabilities{};
  detail::Check(
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device.GetPhysicalDevice(), m_device.GetSurface(), &capabilities),
    "Query surface capabilities");
  detail::Require((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0,
                  "Surface images do not support color attachments");
  uint32_t count{};
  detail::Check(
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.GetPhysicalDevice(), m_device.GetSurface(), &count, nullptr),
    "Query surface formats");
  std::vector<VkSurfaceFormatKHR> formats(count);
  detail::Check(
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.GetPhysicalDevice(), m_device.GetSurface(), &count, formats.data()),
    "Query surface formats");
  formats.resize(count);
  detail::Require(!formats.empty(), "Surface has no formats");
  const auto format = ChooseFormat(formats);
  m_info.format = format.format;
  m_info.color_space = format.colorSpace;
  detail::Check(
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.GetPhysicalDevice(), m_device.GetSurface(), &count, nullptr),
    "Query present modes");
  std::vector<VkPresentModeKHR> modes(count);
  detail::Check(vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.GetPhysicalDevice(), m_device.GetSurface(), &count,
                                                          modes.data()),
                "Query present modes");
  modes.resize(count);
  m_info.present_mode = VK_PRESENT_MODE_FIFO_KHR;
  if (!desc.enable_vsync && std::ranges::find(modes, VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) {
    m_info.present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
  } else if (!desc.enable_vsync && std::ranges::find(modes, VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) {
    m_info.present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  }
  m_info.extent = capabilities.currentExtent;
  if (capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max()) {
    m_info.extent.width =
      std::clamp(desc.extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    m_info.extent.height =
      std::clamp(desc.extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
  }
  detail::Require(m_info.extent.width > 0 && m_info.extent.height > 0, "Surface is currently suspended");
  uint32_t image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0) {
    image_count = std::min(image_count, capabilities.maxImageCount);
  }
  // 保存请求的最少数量供 ImGui 使用，实际图像数量须在创建后单独查询。
  m_info.min_image_count = image_count;
  const std::array families{m_device.GetInfo().graphics_family, m_device.GetInfo().present_family};
  VkSwapchainCreateInfoKHR info{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .pNext = nullptr,
    .flags = {},
    .surface = m_device.GetSurface(),
    .minImageCount = image_count,
    .imageFormat = m_info.format,
    .imageColorSpace = m_info.color_space,
    .imageExtent = m_info.extent,
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = {},
    .pQueueFamilyIndices = nullptr,
    .preTransform = {},
    .compositeAlpha = {},
    .presentMode = {},
    .clipped = {},
    .oldSwapchain = {},
  };
  if (families.front() != families.back()) {
    // 图形和呈现使用不同队列族时共享图像，避免在帧循环中额外转移队列族所有权。
    info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    info.queueFamilyIndexCount = static_cast<uint32_t>(families.size());
    info.pQueueFamilyIndices = families.data();
  }
  info.preTransform = capabilities.currentTransform;
  info.compositeAlpha = ChooseAlpha(capabilities.supportedCompositeAlpha);
  info.presentMode = m_info.present_mode;
  info.clipped = VK_TRUE;
  info.oldSwapchain = old_swapchain;
  detail::Check(vkCreateSwapchainKHR(m_device.GetHandle(), &info, nullptr, &m_swapchain), "Create swapchain");
  detail::Check(vkGetSwapchainImagesKHR(m_device.GetHandle(), m_swapchain, &count, nullptr), "Query swapchain images");
  m_images.resize(count);
  detail::Check(vkGetSwapchainImagesKHR(m_device.GetHandle(), m_swapchain, &count, m_images.data()),
                "Query swapchain images");
  m_images.resize(count);
  m_info.image_count = count;
  m_views.resize(count, VK_NULL_HANDLE);
  for (uint32_t index{}; index < count; ++index) {
    VkImageViewCreateInfo view{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = nullptr,
      .flags = {},
      .image = m_images.at(index),
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = m_info.format,
      .components = {},
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    detail::Check(vkCreateImageView(m_device.GetHandle(), &view, nullptr, &m_views.at(index)),
                  "Create swapchain image view");
  }
}
Swapchain::~Swapchain() noexcept {
  for (VkImageView view : m_views) {
    vkDestroyImageView(m_device.GetHandle(), view, nullptr);
  }
  vkDestroySwapchainKHR(m_device.GetHandle(), m_swapchain, nullptr);
}
VkSwapchainKHR Swapchain::GetHandle() const noexcept { return m_swapchain; }
const SwapchainInfo& Swapchain::GetInfo() const noexcept { return m_info; }
VkImage Swapchain::GetImage(uint32_t index) const { return m_images.at(index); }
VkImageView Swapchain::GetImageView(uint32_t index) const { return m_views.at(index); }
}  // namespace fw
