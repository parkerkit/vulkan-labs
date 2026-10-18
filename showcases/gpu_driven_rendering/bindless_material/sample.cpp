#include "sample.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <span>

#include "framework/device.h"
#include "framework/frame_scheduler.h"
#include "framework/shader_module.h"
#include "framework/shapes.h"
#include "framework/synchronization.h"

namespace demo {
namespace {
constexpr auto k_shader_directory = "gpu_driven_rendering/bindless_material";
constexpr std::array<const char*, BINDLESS_TEXTURE_COUNT> k_texture_files{
  "alpha_blend.png",
  "brick_wall.jpg",
  "nightsky.png",
  "red_brick_wall.png",
};

void BeginAttachments(const fw::FrameContext& frame, const fw::Image& depth) {
  VkImageMemoryBarrier2 depth_ready{};
  depth_ready.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  depth_ready.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  depth_ready.dstAccessMask =
    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  depth_ready.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth_ready.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depth_ready.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  depth_ready.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  depth_ready.image = depth.GetHandle();
  depth_ready.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
  fw::CmdBarrier(frame.command_buffer, {}, std::span{&depth_ready, 1});

  VkRenderingAttachmentInfo color_attachment{};
  color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color_attachment.imageView = frame.color_view;
  color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.clearValue.color = {.float32 = {0.025F, 0.035F, 0.06F, 1.0F}};
  VkRenderingAttachmentInfo depth_attachment{};
  depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depth_attachment.imageView = depth.GetView();
  depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.clearValue.depthStencil = {1.0F, 0};
  VkRenderingInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering.renderArea.extent = frame.extent;
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color_attachment;
  rendering.pDepthAttachment = &depth_attachment;
  vkCmdBeginRendering(frame.command_buffer, &rendering);
}
}  // namespace

void Sample::OnInitialize(const fw::SampleContext& context, const fw::SwapchainInfo& swapchain) {
  m_device = &context.device;
  m_frames.resize(context.frames.GetFrameCount());
  m_depth_format = fw::Image::FindDepthFormat(*m_device);
  CreateGeometry();
  CreateDescriptors();
  OnResize(swapchain);
  ResetCamera();
}

void Sample::CreateGeometry() {
  const auto cube = fw::Shapes::Cube();
  const auto vertex_bytes = std::as_bytes(std::span{cube.vertices});
  const auto index_bytes = std::as_bytes(std::span{cube.indices});
  m_vertices = fw::Buffer::Create(
    *m_device, {vertex_bytes.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, fw::MemoryUsage::Upload});
  m_indices = fw::Buffer::Create(*m_device,
                                 {index_bytes.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, fw::MemoryUsage::Upload});
  m_vertices.Write(vertex_bytes);
  m_indices.Write(index_bytes);
  m_index_count = static_cast<std::uint32_t>(cube.indices.size());
}

void Sample::CreateDescriptors() {
  const auto texture_directory = std::filesystem::path{PROJECT_ASSETS_DIR} / "textures";
  std::array<VkDescriptorImageInfo, BINDLESS_TEXTURE_COUNT> texture_infos{};
  for (std::size_t index{}; index < m_textures.size(); ++index) {
    auto& texture = m_textures[index];
    texture = fw::Texture2D::Load(*m_device, texture_directory / k_texture_files[index]);
    texture_infos[index] = {texture.GetSampler(), texture.GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  }

  const std::array<VkDescriptorSetLayoutBinding, 3> bindings{{
    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
    {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, BINDLESS_TEXTURE_COUNT, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
  }};
  m_set_layout = fw::DescriptorSetLayout::Create(*m_device, bindings);
  const auto frame_count = static_cast<std::uint32_t>(m_frames.size());
  const std::array<VkDescriptorPoolSize, 3> pool_sizes{{
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame_count},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame_count},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frame_count * BINDLESS_TEXTURE_COUNT},
  }};
  m_descriptor_pool = fw::DescriptorPool::Create(*m_device, {frame_count, pool_sizes});

  // 描述符只在初始化时写一次。每个帧槽位各有材质表，贴图资源全帧共用。
  for (auto& frame : m_frames) {
    frame.camera_uniform =
      fw::Buffer::Create(*m_device, {sizeof(glm::mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, fw::MemoryUsage::Upload});
    frame.materials =
      fw::Buffer::Create(*m_device, {sizeof(m_materials), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, fw::MemoryUsage::Upload});
    frame.set = m_descriptor_pool.Allocate(m_set_layout.GetHandle());

    const VkDescriptorBufferInfo camera_info{frame.camera_uniform.GetHandle(), 0, frame.camera_uniform.GetSize()};
    const VkDescriptorBufferInfo material_info{frame.materials.GetHandle(), 0, frame.materials.GetSize()};
    std::array<VkWriteDescriptorSet, bindings.size()> writes{};
    for (std::size_t index{}; index < bindings.size(); ++index) {
      auto& write = writes[index];
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = frame.set;
      write.dstBinding = bindings[index].binding;
      write.descriptorType = bindings[index].descriptorType;
      write.descriptorCount = bindings[index].descriptorCount;
    }
    writes[0].pBufferInfo = &camera_info;
    writes[1].pBufferInfo = &material_info;
    writes[2].pImageInfo = texture_infos.data();
    vkUpdateDescriptorSets(m_device->GetHandle(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }
}

void Sample::CreatePipeline(VkFormat color_format) {
  const auto shader_directory = std::filesystem::path{PROJECT_SHADER_DIR} / k_shader_directory;
  auto vertex = fw::ShaderModule::Load(*m_device, shader_directory / "material.vert.spv");
  auto fragment = fw::ShaderModule::Load(*m_device, shader_directory / "material.frag.spv");
  const VkVertexInputBindingDescription binding{0, sizeof(fw::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
  const std::array<VkVertexInputAttributeDescription, 3> attributes{{
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(fw::Vertex, positions))},
    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(fw::Vertex, normals))},
    {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(fw::Vertex, texcoords))},
  }};
  const auto set_layout = m_set_layout.GetHandle();
  fw::GraphicsPipelineDesc desc{};
  desc.vertex_shader = vertex.GetHandle();
  desc.fragment_shader = fragment.GetHandle();
  desc.layout.set_layouts = std::span{&set_layout, 1};
  desc.vertex_bindings = std::span{&binding, 1};
  desc.vertex_attributes = attributes;
  desc.color_formats = std::span{&color_format, 1};
  desc.depth_format = m_depth_format;
  m_pipeline = fw::Pipeline::CreateGraphics(*m_device, desc);
  m_color_format = color_format;
}

void Sample::OnResize(const fw::SwapchainInfo& swapchain) {
  m_extent = swapchain.extent;
  fw::ImageDesc depth_desc{};
  depth_desc.extent = m_extent;
  depth_desc.format = m_depth_format;
  depth_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  depth_desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  for (auto& frame : m_frames) {
    frame.depth = fw::Image::Create(*m_device, depth_desc);
  }
  if (m_pipeline.GetHandle() == VK_NULL_HANDLE || m_color_format != swapchain.format) {
    CreatePipeline(swapchain.format);
  }
}

void Sample::ResetCamera() {
  // material.vert 按 ±0.8 放置边长 1 的立方体。
  const fw::AABB bounds{{-1.3F, -1.3F, -0.5F}, {1.3F, 1.3F, 0.5F}};
  m_camera.ResetCamera(bounds, static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height));
}

void Sample::OnUpdate(const fw::FrameContext& frame, const fw::InputState& input, float delta_seconds) {
  if (input.IsPressed(fw::Key::Home)) {
    ResetCamera();
  }
  m_camera.Update(input, delta_seconds);
  const auto aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
  const auto view_projection = m_camera.GetProjectionMatrix(aspect) * m_camera.GetViewMatrix();
  auto& resources = m_frames.at(frame.frame_index);
  resources.camera_uniform.Write(std::as_bytes(std::span{&view_projection, 1}));
  // 框架在 OnUpdate 前等待当前帧槽位的 fence。
  resources.materials.Write(std::as_bytes(std::span{m_materials}));
}

void Sample::OnRender(const fw::FrameContext& frame) {
  const auto cmd = frame.command_buffer;
  const auto& resources = m_frames.at(frame.frame_index);
  BeginAttachments(frame, resources.depth);

  const VkViewport viewport{0.0F, 0.0F, static_cast<float>(frame.extent.width), static_cast<float>(frame.extent.height),
                            0.0F, 1.0F};
  const VkRect2D scissor{.offset = {0, 0}, .extent = frame.extent};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetHandle());
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetLayout(), 0, 1, &resources.set, 0,
                          nullptr);
  const auto vertex_buffer = m_vertices.GetHandle();
  const VkDeviceSize offset{};
  vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
  vkCmdBindIndexBuffer(cmd, m_indices.GetHandle(), 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, m_index_count, static_cast<std::uint32_t>(m_materials.size()), 0, 0, 0);
  vkCmdEndRendering(cmd);
}

void Sample::OnGui() {
  ImGui::SetNextWindowPos(ImVec2{16.0F, 16.0F}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Bindless Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const std::array material_names{"0 (bottom left)", "1 (bottom right)", "2 (top left)", "3 (top right)"};
    ImGui::Combo("Material", &m_selected_material, material_names.data(), static_cast<int>(material_names.size()));

    auto& texture_index = m_materials.at(static_cast<std::size_t>(m_selected_material));
    auto selected_texture = static_cast<int>(texture_index);
    if (ImGui::Combo("Texture", &selected_texture, k_texture_files.data(), static_cast<int>(k_texture_files.size()))) {
      texture_index = static_cast<std::uint32_t>(selected_texture);
    }
  }
  ImGui::End();
}
}  // namespace demo
