#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>

namespace fw {
class Device;

/// @brief 管理离线编译的 SPIR-V 模块，Device 必须比模块存活更久。
class ShaderModule final {
public:
  ShaderModule() = default;
  ~ShaderModule() noexcept;
  ShaderModule(const ShaderModule&) = delete;
  ShaderModule& operator=(const ShaderModule&) = delete;
  ShaderModule(ShaderModule&& other) noexcept;
  ShaderModule& operator=(ShaderModule&& other) noexcept;

  /// @brief 读取并检查 SPIR-V 长度与魔数后创建模块，文件读取或创建失败时抛出异常。
  [[nodiscard]] static ShaderModule Load(Device& device, const std::filesystem::path& path);
  [[nodiscard]] VkShaderModule GetHandle() const noexcept;

private:
  void Release() noexcept;

  VkDevice m_device{};
  VkShaderModule m_module{};
};
}  // namespace fw
