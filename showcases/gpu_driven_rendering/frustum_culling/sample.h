#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <glm/vec4.hpp>
#include <vector>

#include "framework/aabb.h"
#include "framework/buffer.h"
#include "framework/descriptors.h"
#include "framework/gpu_timer.h"
#include "framework/image.h"
#include "framework/orbit_camera.h"
#include "framework/pipeline.h"
#include "framework/sample.h"
#include "framework/shapes.h"
#include "framework/swapchain.h"
#include "framework/texture2d.h"

namespace demo {
struct Drawable {
  glm::mat4 model_matrix{};
  fw::Buffer vertex_buffer{};
  fw::Buffer index_buffer{};
  glm::vec4 bounding_sphere{};
  std::uint32_t index_count{};
};

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
  void CreateComputeResources();
  void GenerateDrawCommands(const fw::FrameContext& frame);
  void CreateDescriptors();
  void CreateDepthImages(VkExtent2D extent);
  void CreatePipeline(VkFormat color_format);
  void CreateDebugPipeline(VkFormat color_format);
  void ResetAll() noexcept;
  Drawable CreateDrawable(const fw::Mesh& mesh);
  void Draw(VkCommandBuffer cmd, std::uint32_t frame_index);
  void DrawDebug(const fw::FrameContext& frame);

  fw::Device* m_device{};
  fw::FrameScheduler* m_frames{};
  fw::OrbitCamera m_camera{};
  fw::AABB m_scene_bounds{};
  std::vector<Drawable> m_drawables{};
  fw::Buffer m_draw_inputs{};
  std::vector<fw::Buffer> m_culling_uniforms{};
  std::vector<fw::Buffer> m_indirect_buffers{};
  fw::DescriptorSetLayout m_compute_set_layout{};
  fw::DescriptorPool m_compute_descriptor_pool{};
  std::vector<VkDescriptorSet> m_compute_sets{};
  fw::Pipeline m_compute_pipeline{};
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
