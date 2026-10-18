#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

#include "framework/buffer.h"
#include "framework/descriptors.h"
#include "framework/image.h"
#include "framework/orbit_camera.h"
#include "framework/pipeline.h"
#include "framework/sample.h"
#include "framework/texture2d.h"

namespace demo {
class Sample final : public fw::ISample {
public:
  void OnInitialize(const fw::SampleContext& context, const fw::SwapchainInfo& swapchain) override;
  void OnUpdate(const fw::FrameContext& frame, const fw::InputState& input, float delta_seconds) override;
  void OnRender(const fw::FrameContext& frame) override;
  void OnResize(const fw::SwapchainInfo& swapchain) override;
  void OnGui() override;

private:
  struct FrameResources {
    fw::Buffer camera_uniform{};
    fw::Buffer materials{};
    fw::Image depth{};
    VkDescriptorSet set{};
  };

  void CreateGeometry();
  void CreateDescriptors();
  void CreatePipeline(VkFormat color_format);
  void ResetCamera();

  fw::Device* m_device{};
  fw::OrbitCamera m_camera{};
  VkExtent2D m_extent{};
  VkFormat m_depth_format{VK_FORMAT_UNDEFINED};
  VkFormat m_color_format{VK_FORMAT_UNDEFINED};
  fw::Buffer m_vertices{};
  fw::Buffer m_indices{};
  std::uint32_t m_index_count{};
  std::array<fw::Texture2D, BINDLESS_TEXTURE_COUNT> m_textures{};
  std::vector<FrameResources> m_frames{};
  fw::DescriptorSetLayout m_set_layout{};
  fw::DescriptorPool m_descriptor_pool{};
  fw::Pipeline m_pipeline{};
  std::array<std::uint32_t, 4> m_materials{0, 1, 2, 3};
  int m_selected_material{};
};
}  // namespace demo
