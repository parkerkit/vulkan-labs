#pragma once

#include <vulkan/vulkan.h>

#include <span>

namespace fw {
class Device;

struct PipelineLayoutDesc {
  std::span<const VkDescriptorSetLayout> set_layouts{};
  std::span<const VkPushConstantRange> push_constants{};
};

struct GraphicsPipelineDesc {
  VkShaderModule vertex_shader{};
  VkShaderModule fragment_shader{};
  PipelineLayoutDesc layout{};
  std::span<const VkVertexInputBindingDescription> vertex_bindings{};
  std::span<const VkVertexInputAttributeDescription> vertex_attributes{};
  std::span<const VkFormat> color_formats{};
  VkFormat depth_format{VK_FORMAT_UNDEFINED};
  VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  VkCullModeFlags cull_mode{VK_CULL_MODE_BACK_BIT};
  VkFrontFace front_face{VK_FRONT_FACE_COUNTER_CLOCKWISE};
  VkCompareOp depth_compare{VK_COMPARE_OP_LESS};
  VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
  bool enable_depth_write{true};
  bool enable_alpha_blending{false};
  VkPipelineCache cache{};
};

struct ComputePipelineDesc {
  VkShaderModule shader{};
  PipelineLayoutDesc layout{};
  VkPipelineCache cache{};
};

/// @brief 管理管线及其布局，创建参数中的 span 仅在工厂调用期间借用。
/// @details 图形管线使用动态渲染及动态视口、裁剪矩形，录制绘制命令前须设置这两个动态状态。
/// Device 必须比管线存活更久，释放资源前须等待 GPU 完成使用。
class Pipeline final {
public:
  Pipeline() = default;
  ~Pipeline() noexcept;
  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;
  Pipeline(Pipeline&& other) noexcept;
  Pipeline& operator=(Pipeline&& other) noexcept;

  [[nodiscard]] static Pipeline CreateGraphics(Device& device, const GraphicsPipelineDesc& desc);
  [[nodiscard]] static Pipeline CreateCompute(Device& device, const ComputePipelineDesc& desc);
  [[nodiscard]] VkPipeline GetHandle() const noexcept;
  [[nodiscard]] VkPipelineLayout GetLayout() const noexcept;

private:
  void CreateLayout(const PipelineLayoutDesc& desc);
  void Release() noexcept;

  VkDevice m_device{};
  VkPipeline m_pipeline{};
  VkPipelineLayout m_layout{};
};
}  // namespace fw
