#pragma once

#include <vulkan/vulkan.h>

#include <string_view>

namespace fw::detail {
// 将初始化和运行失败转换为异常；析构及 C 回调必须在其边界内处理异常。
void Check(VkResult result, std::string_view operation);
void Require(bool condition, std::string_view message);
}  // namespace fw::detail
