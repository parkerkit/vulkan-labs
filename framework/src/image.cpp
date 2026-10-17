#include "framework/image.h"

#include <vk_mem_alloc.h>

#include <array>
#include <stdexcept>
#include <utility>

#include "framework/device.h"
#include "vulkan_helpers.h"

namespace fw {
Image Image::Create(Device& device, const ImageDesc& desc) {
  detail::Require(desc.extent.width > 0 && desc.extent.height > 0 && desc.mip_levels > 0 && desc.array_layers > 0 &&
                    desc.usage != 0 && desc.aspect != 0,
                  "Invalid image description");
  Image image{};
  image.m_device = &device;
  image.m_desc = desc;
  VkImageCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .imageType = VK_IMAGE_TYPE_2D,
    .format = desc.format,
    .extent = {desc.extent.width, desc.extent.height, 1},
    .mipLevels = desc.mip_levels,
    .arrayLayers = desc.array_layers,
    .samples = desc.samples,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = desc.usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = {},
    .pQueueFamilyIndices = nullptr,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VmaAllocationCreateInfo allocation{
    .flags = {},
    .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .requiredFlags = {},
    .preferredFlags = {},
    .memoryTypeBits = {},
    .pool = {},
    .pUserData = nullptr,
    .priority = {},
  };
  detail::Check(vmaCreateImage(device.GetAllocator(), &info, &allocation, &image.m_image, &image.m_allocation, nullptr),
                "Create image");
  // 图像已交由局部对象持有，后续视图创建失败时会自动释放图像和分配。
  VkImageViewCreateInfo view{
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .image = image.m_image,
    .viewType = desc.array_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
    .format = desc.format,
    .components = {},
    .subresourceRange = {desc.aspect, 0, desc.mip_levels, 0, desc.array_layers},
  };
  detail::Check(vkCreateImageView(device.GetHandle(), &view, nullptr, &image.m_view), "Create image view");
  return image;
}
VkFormat Image::FindDepthFormat(const Device& device) {
  for (VkFormat format : std::array{VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDevice(), format, &properties);
    if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
      return format;
    }
  }
  throw std::runtime_error("No supported depth attachment format");
}
Image::~Image() noexcept { Release(); }
Image::Image(Image&& other) noexcept { *this = std::move(other); }
Image& Image::operator=(Image&& other) noexcept {
  if (this != &other) {
    Release();
    m_device = std::exchange(other.m_device, nullptr);
    m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
    m_view = std::exchange(other.m_view, VK_NULL_HANDLE);
    m_allocation = std::exchange(other.m_allocation, nullptr);
    m_desc = other.m_desc;
  }
  return *this;
}
void Image::Release() noexcept {
  if (m_view != VK_NULL_HANDLE) {
    vkDestroyImageView(m_device->GetHandle(), m_view, nullptr);
  }
  if (m_image != VK_NULL_HANDLE) {
    vmaDestroyImage(m_device->GetAllocator(), m_image, m_allocation);
  }
  m_view = VK_NULL_HANDLE;
  m_image = VK_NULL_HANDLE;
  m_allocation = nullptr;
  m_device = nullptr;
}
VkImage Image::GetHandle() const noexcept { return m_image; }
VkImageView Image::GetView() const noexcept { return m_view; }
const ImageDesc& Image::GetDesc() const noexcept { return m_desc; }
}  // namespace fw
