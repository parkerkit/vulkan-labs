#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

#include "framework/aabb.h"
#include "framework/buffer.h"
#include "framework/descriptors.h"
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
  void CreateGeometry();
  void CreateDescriptors();
  void CreateDepthImages(VkExtent2D extent);
  void CreatePipeline(VkFormat color_format);
  void ResetAll() noexcept;
  void Draw(VkCommandBuffer cmd, std::uint32_t frame_index);

  fw::Device* m_device{};
  fw::FrameScheduler* m_frames{};
  fw::OrbitCamera m_camera{};
  fw::AABB m_scene_bounds{};
  fw::Buffer m_vertices{};
  fw::Buffer m_indices{};
  fw::Buffer m_instances{};
  uint32_t m_index_count{};
  uint32_t m_instance_count{};
  fw::Texture2D m_texture{};
  fw::DescriptorSetLayout m_set_layout{};
  fw::DescriptorPool m_descriptor_pool{};
  std::vector<fw::Buffer> m_uniforms{};
  std::vector<VkDescriptorSet> m_sets{};
  fw::Pipeline m_pipeline{};
  std::vector<fw::Image> m_depth_images{};
  VkFormat m_depth_format{VK_FORMAT_UNDEFINED};
  VkFormat m_color_format{VK_FORMAT_UNDEFINED};
  float m_rotation{};
  float m_rotation_speed{0.5F};
  VkExtent2D m_extent{};
  std::array<float, 4> m_clear_color{0.025F, 0.035F, 0.06F, 1.0F};
  bool m_enable_rotation{true};
};
}  // namespace demo
