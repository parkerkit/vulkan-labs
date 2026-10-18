#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/vec4.hpp>
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

namespace demo {
class Sample final : public fw::ISample {
public:
  void OnInitialize(const fw::SampleContext& context, const fw::SwapchainInfo& swapchain) override;
  void OnUpdate(const fw::FrameContext& frame, const fw::InputState& input, float delta_seconds) override;
  void OnRender(const fw::FrameContext& frame) override;
  void OnResize(const fw::SwapchainInfo& swapchain) override;
  void OnGui() override;

private:
  struct LodStats {
    std::uint32_t draw_count{};
    std::array<std::uint32_t, 3> lod_counts{};
  };
  static_assert(sizeof(LodStats) == 16);
  static_assert(offsetof(LodStats, lod_counts) == 4);

  struct FrameResources {
    fw::Buffer indirect_commands{};
    fw::Buffer visible_instances{};
    fw::Buffer stats{};
    fw::Buffer stats_readback{};
    fw::Buffer culling_uniform{};
    fw::Buffer camera_uniform{};
    VkDescriptorSet compute_set{};
    VkDescriptorSet graphics_set{};
    bool has_stats{false};
    bool batched{false};
  };

  void CreateGeometry();
  void CreateFrameResources();
  void CreateComputePipeline();
  void CreateGraphicsPipeline(VkFormat color_format);
  void GenerateDrawCommands(const fw::FrameContext& frame);
  void Draw(const fw::FrameContext& frame);
  void CopyStatsToReadback(const fw::FrameContext& frame);
  void ResetCamera();

  fw::Device* m_device{};
  fw::OrbitCamera m_camera{};
  fw::AABB m_scene_bounds{};
  VkExtent2D m_extent{};
  std::uint32_t m_object_count{};
  std::array<glm::uvec4, 3> m_lod_meshes{};
  fw::Buffer m_vertices{};
  fw::Buffer m_indices{};
  fw::Buffer m_objects{};
  std::vector<FrameResources> m_frames{};
  fw::DescriptorSetLayout m_compute_set_layout{};
  fw::DescriptorSetLayout m_graphics_set_layout{};
  fw::DescriptorPool m_descriptor_pool{};
  fw::Pipeline m_per_object_pipeline{};
  fw::Pipeline m_bin_instances_pipeline{};
  fw::Pipeline m_build_batches_pipeline{};
  fw::Pipeline m_graphics_pipeline{};
  fw::GpuTimer m_gpu_timer{};
  std::vector<fw::Image> m_depth_images{};
  VkFormat m_depth_format{VK_FORMAT_UNDEFINED};
  VkFormat m_color_format{VK_FORMAT_UNDEFINED};
  std::optional<LodStats> m_last_stats{};
  float m_high_threshold{30.0F};
  float m_medium_threshold{18.0F};
  bool m_enable_culling{true};
  bool m_force_high{false};
  bool m_batch_instances{true};
  bool m_last_batched{false};
};
}  // namespace demo
