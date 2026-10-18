#include "vulkan_helpers.h"

#include <stdexcept>
#include <string>

namespace fw::detail {
void Check(VkResult result, std::string_view operation) {
  // 此处仅接受 VK_SUCCESS，交换链过期等需要分支处理的返回值须由调用方先行处理。
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string{operation} + " (VkResult " + std::to_string(static_cast<int>(result)) + ")");
  }
}
}  // namespace fw::detail
