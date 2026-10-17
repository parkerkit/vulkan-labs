#include "framework/synchronization.h"

#include <cstdint>

namespace fw {
void CmdBarrier(VkCommandBuffer command_buffer, std::span<const VkBufferMemoryBarrier2> buffers,
                std::span<const VkImageMemoryBarrier2> images) noexcept {
  VkDependencyInfo dependency{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .pNext = nullptr,
    .dependencyFlags = {},
    .memoryBarrierCount = {},
    .pMemoryBarriers = nullptr,
    .bufferMemoryBarrierCount = static_cast<uint32_t>(buffers.size()),
    .pBufferMemoryBarriers = buffers.data(),
    .imageMemoryBarrierCount = static_cast<uint32_t>(images.size()),
    .pImageMemoryBarriers = images.data(),
  };
  vkCmdPipelineBarrier2(command_buffer, &dependency);
}
}  // namespace fw
