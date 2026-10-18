#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "framework/aabb.h"
#include "framework/buffer.h"
#include "framework/descriptors.h"
#include "framework/gpu_timer.h"
#include "framework/image.h"
#include "framework/orbit_camera.h"
#include "framework/pipeline.h"
#include "framework/sample.h"
#include "framework/swapchain.h"
#include "framework/texture2d.h"

namespace demo {
class Sample final : public fw::ISample {
public:
  Sample() = default;
  ~Sample() override = default;
  Sample(const Sample&) = delete;
  Sample& operator=(const Sample&) = delete;
  Sample(Sample&&) = delete;
  Sample& operator=(Sample&&) = delete;

  void OnInitialize(const fw::SampleContext& context, const fw::SwapchainInfo& swapchain) override;
  void OnUpdate(const fw::FrameContext& frame, const fw::InputState& input, float delta_seconds) override;
  void OnRender(const fw::FrameContext& frame) override;
  void OnResize(const fw::SwapchainInfo& swapchain) override;
  void OnGui() override;

private:
  struct ComputeFrame {
    fw::Buffer indirect_commands{};
    fw::Buffer draw_count{};
    fw::Buffer visibility{};
    fw::Buffer culling_uniform{};
    fw::Buffer count_readback{};
    VkDescriptorSet set{};
    bool has_count_result{false};
    bool used_prefix_sum{false};
  };

  void CreateGeometry();
  void CreateComputeResources();
  void GenerateDrawCommands(const fw::FrameContext& frame);
  void CopyDrawCountToReadback(const fw::FrameContext& frame);
  void CreateDescriptors();
  void CreateDepthImages(VkExtent2D extent);
  void CreatePipeline(VkFormat color_format);
  void CreateDebugPipeline(VkFormat color_format);
  void ResetAll() noexcept;
  void Draw(VkCommandBuffer cmd, std::uint32_t frame_index);
  void DrawDebug(const fw::FrameContext& frame);

  fw::Device* m_device{};
  fw::FrameScheduler* m_frames{};
  fw::OrbitCamera m_camera{};
  fw::AABB m_scene_bounds{};
  std::uint32_t m_object_count{};
  int m_active_object_count{};
  int m_visible_percent{50};
  bool m_use_prefix_sum{true};
  bool m_use_test_visibility{};
  bool m_last_used_prefix_sum{};
  std::optional<std::uint32_t> m_last_visible_count{};
  fw::Buffer m_vertices{};
  fw::Buffer m_indices{};
  fw::Buffer m_draw_inputs{};
  std::vector<ComputeFrame> m_compute_frames{};
  fw::DescriptorSetLayout m_compute_set_layout{};
  fw::DescriptorPool m_compute_descriptor_pool{};
  fw::Pipeline m_atomic_compact_pipeline{};
  fw::Pipeline m_scan_blocks_pipeline{};
  fw::Pipeline m_scan_block_totals_pipeline{};
  fw::Pipeline m_scatter_commands_pipeline{};
  fw::Texture2D m_texture{};
  fw::DescriptorSetLayout m_set_layout{};
  fw::DescriptorPool m_descriptor_pool{};
  std::vector<fw::Buffer> m_uniforms{};
  std::vector<VkDescriptorSet> m_sets{};
  fw::Pipeline m_pipeline{};
  fw::Pipeline m_debug_pipeline{};
  fw::GpuTimer m_gpu_timer{};
  glm::mat4 m_culling_view_projection{1.0F};
  std::vector<fw::Image> m_depth_images{};
  VkFormat m_depth_format{VK_FORMAT_UNDEFINED};
  VkFormat m_color_format{VK_FORMAT_UNDEFINED};
  float m_rotation{};
  float m_rotation_speed{0.5F};
  VkExtent2D m_extent{};
  std::array<float, 4> m_clear_color{0.025F, 0.035F, 0.06F, 1.0F};
  bool m_enable_rotation{true};
  bool m_enable_culling{true};
  bool m_freeze_frustum{false};
  bool m_has_culling_frustum{false};
  bool m_show_frustum{false};
  bool m_show_bounds{false};
};
}  // namespace demo
