#include "framework/shader_module.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "framework/device.h"
#include "vulkan_helpers.h"

namespace fw {
ShaderModule ShaderModule::Load(Device& device, const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  detail::Require(file.is_open(), "Cannot open SPIR-V file: " + path.string());
  const auto length = static_cast<std::streamoff>(file.tellg());
  // SPIR-V 包含五个 32 位字的头部；使用字数组也保证传给 Vulkan 的代码地址正确对齐。
  detail::Require(length >= 20 && length % 4 == 0, "Invalid SPIR-V size: " + path.string());
  std::vector<uint32_t> words(static_cast<std::size_t>(length) / sizeof(uint32_t));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(length));
  detail::Require(static_cast<bool>(file) && words.front() == 0x07230203U, "Invalid SPIR-V: " + path.string());
  ShaderModule shader{};
  shader.m_device = device.GetHandle();
  VkShaderModuleCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .codeSize = static_cast<std::size_t>(length),
    .pCode = words.data(),
  };
  detail::Check(vkCreateShaderModule(shader.m_device, &info, nullptr, &shader.m_module), "Create shader module");
  return shader;
}
ShaderModule::~ShaderModule() noexcept { Release(); }
ShaderModule::ShaderModule(ShaderModule&& other) noexcept { *this = std::move(other); }
ShaderModule& ShaderModule::operator=(ShaderModule&& other) noexcept {
  if (this != &other) {
    Release();
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_module = std::exchange(other.m_module, VK_NULL_HANDLE);
  }
  return *this;
}
void ShaderModule::Release() noexcept {
  if (m_module != VK_NULL_HANDLE) {
    vkDestroyShaderModule(m_device, m_module, nullptr);
  }
  m_module = VK_NULL_HANDLE;
}
VkShaderModule ShaderModule::GetHandle() const noexcept { return m_module; }
}  // namespace fw
