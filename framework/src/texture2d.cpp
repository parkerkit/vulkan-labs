#include "framework/texture2d.h"

#include <stb_image.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "framework/buffer.h"
#include "framework/device.h"
#include "framework/synchronization.h"
#include "vulkan_helpers.h"

namespace fw {
namespace {
std::string GetUtf8Path(const std::filesystem::path& path) {
  const auto utf8 = path.u8string();
  return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}
}  // namespace

Texture2D Texture2D::Load(Device& device, const std::filesystem::path& path) {
  // 通过 filesystem 路径读取文件，再从内存解码，避免依赖 stb 的窄字符文件接口。
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open texture: " + GetUtf8Path(path));
  }
  const auto length = static_cast<std::streamoff>(file.tellg());
  if (length <= 0 || length > std::numeric_limits<int>::max()) {
    throw std::runtime_error("Invalid texture size: " + GetUtf8Path(path));
  }
  std::vector<stbi_uc> encoded(static_cast<std::size_t>(length));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(length));
  if (!file) {
    throw std::runtime_error("Cannot read texture: " + GetUtf8Path(path));
  }

  int width{};
  int height{};
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{
    stbi_load_from_memory(encoded.data(), static_cast<int>(length), &width, &height, nullptr, STBI_rgb_alpha),
    &stbi_image_free};
  if (!pixels) {
    const char* reason = stbi_failure_reason();
    throw std::runtime_error("Cannot decode texture: " + GetUtf8Path(path) + " (" +
                             (reason != nullptr ? reason : "unknown image error") + ")");
  }
  const auto max_dimension = device.GetInfo().properties.limits.maxImageDimension2D;
  if (width <= 0 || height <= 0 || static_cast<uint32_t>(width) > max_dimension ||
      static_cast<uint32_t>(height) > max_dimension) {
    throw std::runtime_error("Texture dimensions exceed device limits: " + GetUtf8Path(path));
  }
  constexpr VkFormat k_format{VK_FORMAT_R8G8B8A8_SRGB};
  VkFormatProperties properties{};
  vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDevice(), k_format, &properties);
  constexpr VkFormatFeatureFlags k_required_features{VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                     VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                                     VK_FORMAT_FEATURE_TRANSFER_DST_BIT};
  detail::Require((properties.optimalTilingFeatures & k_required_features) == k_required_features,
                  "Device does not support linearly filtered RGBA8 sRGB textures");

  const auto byte_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * STBI_rgb_alpha;
  auto staging = Buffer::Create(device, {byte_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::Upload});
  staging.Write(std::as_bytes(std::span{pixels.get(), byte_count}));
  pixels.reset();

  Texture2D texture{};
  texture.m_device = device.GetHandle();
  ImageDesc image_desc{};
  image_desc.extent = {.width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height)};
  image_desc.format = k_format;
  texture.m_image = Image::Create(device, image_desc);
  const VkSamplerCreateInfo sampler_info{
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .magFilter = VK_FILTER_LINEAR,
    .minFilter = VK_FILTER_LINEAR,
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .mipLodBias = 0.0F,
    .anisotropyEnable = VK_FALSE,
    .maxAnisotropy = 1.0F,
    .compareEnable = VK_FALSE,
    .compareOp = VK_COMPARE_OP_ALWAYS,
    .minLod = 0.0F,
    .maxLod = 0.0F,
    .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    .unnormalizedCoordinates = VK_FALSE,
  };
  detail::Check(vkCreateSampler(texture.m_device, &sampler_info, nullptr, &texture.m_sampler),
                "Create texture sampler");

  // 上传完成前保持暂存缓冲存活，布局转换与复制放在同一次提交中。
  device.SubmitImmediate([&](VkCommandBuffer command_buffer) {
    VkImageMemoryBarrier2 barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .pNext = nullptr,
      .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
      .srcAccessMask = VK_ACCESS_2_NONE,
      .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = texture.m_image.GetHandle(),
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    CmdBarrier(command_buffer, {}, std::span{&barrier, 1});
    const VkBufferImageCopy copy{
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .mipLevel = 0,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
      .imageOffset = {.x = 0, .y = 0, .z = 0},
      .imageExtent = {.width = image_desc.extent.width, .height = image_desc.extent.height, .depth = 1},
    };
    vkCmdCopyBufferToImage(command_buffer, staging.GetHandle(), texture.m_image.GetHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    CmdBarrier(command_buffer, {}, std::span{&barrier, 1});
  });
  return texture;
}

Texture2D::~Texture2D() noexcept { Release(); }
Texture2D::Texture2D(Texture2D&& other) noexcept { *this = std::move(other); }
Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
  if (this != &other) {
    Release();
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_image = std::move(other.m_image);
    m_sampler = std::exchange(other.m_sampler, VK_NULL_HANDLE);
  }
  return *this;
}

void Texture2D::Release() noexcept {
  if (m_sampler != VK_NULL_HANDLE) {
    vkDestroySampler(m_device, m_sampler, nullptr);
  }
  m_sampler = VK_NULL_HANDLE;
  m_image = Image{};
  m_device = VK_NULL_HANDLE;
}

VkImageView Texture2D::GetView() const noexcept { return m_image.GetView(); }
VkSampler Texture2D::GetSampler() const noexcept { return m_sampler; }
VkExtent2D Texture2D::GetExtent() const noexcept { return m_image.GetDesc().extent; }
}  // namespace fw
