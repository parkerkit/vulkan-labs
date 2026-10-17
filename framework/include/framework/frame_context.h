#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace fw {
/// @brief 仅在当前帧内有效的借用上下文，BeginFrame 返回后可更新该帧槽位的 CPU 数据。
/// @details 调度器丢弃颜色图像原有内容，并将其转换为 COLOR_ATTACHMENT_OPTIMAL。
/// 场景须结束自身渲染作用域并保持该布局，供 UI 叠加及呈现转换使用。
struct FrameContext {
  uint32_t frame_index{};  ///< 飞行帧槽位索引，用于选择逐帧 Uniform、描述符和深度附件。
  uint32_t image_index{};  ///< 本次取得的交换链图像索引，与帧槽位索引相互独立。
  VkCommandBuffer command_buffer{};
  VkImage color_image{};
  VkImageView color_view{};
  VkExtent2D extent{};
  VkFormat color_format{VK_FORMAT_UNDEFINED};
};
}  // namespace fw
