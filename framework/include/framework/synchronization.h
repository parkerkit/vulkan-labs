#pragma once

#include <vulkan/vulkan.h>

#include <span>

namespace fw {
/// @brief 录制显式 Synchronization2 依赖，调用者提供真实访问阶段、访问类型及资源范围。
/// @details 只建立 GPU 命令之间的依赖，不等待 GPU，也不跟踪或推断资源状态。
void CmdBarrier(VkCommandBuffer command_buffer, std::span<const VkBufferMemoryBarrier2> buffers,
                std::span<const VkImageMemoryBarrier2> images) noexcept;
}  // namespace fw
