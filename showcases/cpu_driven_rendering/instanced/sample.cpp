#include "sample.h"

#include <imgui.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <span>
#include <sstream>
#include <utility>
#include <vector>

#include "framework/device.h"
#include "framework/frame_scheduler.h"
#include "framework/mesh.h"
#include "framework/shader_module.h"
#include "framework/shapes.h"
#include "framework/synchronization.h"
#include "framework/window.h"

namespace demo {
namespace {
struct alignas(16) CameraUniform {
  glm::mat4 model_view_projection{1.0F};
};
static_assert(sizeof(CameraUniform) == 64, "CameraUniform must match the GLSL std140 block");

// 实例缓冲只保存各球体的平移，球体几何与纹理由全部实例共享。
struct InstanceData {
  glm::vec3 offset{};
};

void BeginAttachments(const fw::FrameContext& frame, const fw::Image& depth, const std::array<float, 4>& clear_color) {
  // 深度图按帧槽位独占且每帧清空，槽位 fence 已完成，可丢弃其上次内容。
  VkImageMemoryBarrier2 barrier{
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
    .srcAccessMask = {},
    .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
    .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = depth.GetHandle(),
    .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
  };
  fw::CmdBarrier(frame.command_buffer, {}, std::span{&barrier, 1});

  VkRenderingAttachmentInfo color_attachment{
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .pNext = nullptr,
    .imageView = frame.color_view,
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .resolveMode = {},
    .resolveImageView = {},
    .resolveImageLayout = {},
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue = {},
  };
  for (std::size_t index{}; index < clear_color.size(); ++index) {
    color_attachment.clearValue.color.float32[index] = clear_color.at(index);
  }
  VkRenderingAttachmentInfo depth_attachment{
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .pNext = nullptr,
    .imageView = depth.GetView(),
    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    .resolveMode = {},
    .resolveImageView = {},
    .resolveImageLayout = {},
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .clearValue = {},
  };
  depth_attachment.clearValue.depthStencil = {1.0F, 0};
  VkRenderingInfo rendering{
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .pNext = nullptr,
    .flags = {},
    .renderArea = {},
    .layerCount = {},
    .viewMask = {},
    .colorAttachmentCount = {},
    .pColorAttachments = nullptr,
    .pDepthAttachment = nullptr,
    .pStencilAttachment = nullptr,
  };
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
  m_frames = &context.frames;
  m_depth_format = fw::Image::FindDepthFormat(*m_device);
  CreateGeometry();
  m_texture = fw::Texture2D::Load(*m_device, std::filesystem::path{PROJECT_ASSETS_DIR} / "textures/football.jpg");
  CreateDescriptors();
  // 应用只在交换链重建时通知 OnResize，初始附件由初始化主动建立。
  OnResize(swapchain);
  m_camera.ResetCamera(m_scene_bounds, static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height));
  std::cout << "GPU: " << std::data(m_device->GetInfo().properties.deviceName) << '\n'
            << "LMB drag: orbit | RMB drag: pan | Wheel: zoom | WASD/QE: move\n"
            << "Space: pause/resume | Home: reset camera | R: reset all | Esc: close\n";
}

void Sample::CreateGeometry() {
  const auto mesh = fw::Shapes::Sphere(0.5F, 64, 64);
  constexpr uint32_t k_grid_size{10};
  constexpr float k_start{-static_cast<float>(k_grid_size - 1)};
  std::vector<InstanceData> instances{};
  instances.reserve(k_grid_size * k_grid_size * k_grid_size);
  for (uint32_t x{}; x < k_grid_size; ++x) {
    for (uint32_t y{}; y < k_grid_size; ++y) {
      for (uint32_t z{}; z < k_grid_size; ++z) {
        const glm::vec3 center{k_start + static_cast<float>(x) * 2.0F, k_start + static_cast<float>(y) * 2.0F,
                               k_start + static_cast<float>(z) * 2.0F};
        instances.push_back({.offset = center});
        m_scene_bounds.Merge(fw::AABB{mesh.bounds.GetMin() + center, mesh.bounds.GetMax() + center});
      }
    }
  }
  m_index_count = static_cast<uint32_t>(mesh.indices.size());
  m_instance_count = static_cast<uint32_t>(instances.size());

  const auto vertex_bytes = std::as_bytes(std::span{mesh.vertices});
  const auto index_bytes = std::as_bytes(std::span{mesh.indices});
  const auto instance_bytes = std::as_bytes(std::span{instances});
  auto vertex_staging = fw::Buffer::Create(
    *m_device, {vertex_bytes.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, fw::MemoryUsage::Upload});
  auto index_staging = fw::Buffer::Create(
    *m_device, {index_bytes.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, fw::MemoryUsage::Upload});
  auto instance_staging = fw::Buffer::Create(
    *m_device, {instance_bytes.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, fw::MemoryUsage::Upload});
  vertex_staging.Write(vertex_bytes);
  index_staging.Write(index_bytes);
  instance_staging.Write(instance_bytes);
  m_vertices = fw::Buffer::Create(
    *m_device, {vertex_bytes.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT});
  m_indices = fw::Buffer::Create(
    *m_device, {index_bytes.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT});
  m_instances = fw::Buffer::Create(
    *m_device, {instance_bytes.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT});
  // 静态几何和实例数据只上传一次；返回后才销毁暂存缓冲，所有飞行帧共同读取这些资源。
  m_device->SubmitImmediate([&](VkCommandBuffer command_buffer) {
    const VkBufferCopy vertex_copy{.srcOffset = 0, .dstOffset = 0, .size = vertex_bytes.size_bytes()};
    const VkBufferCopy index_copy{.srcOffset = 0, .dstOffset = 0, .size = index_bytes.size_bytes()};
    const VkBufferCopy instance_copy{.srcOffset = 0, .dstOffset = 0, .size = instance_bytes.size_bytes()};
    vkCmdCopyBuffer(command_buffer, vertex_staging.GetHandle(), m_vertices.GetHandle(), 1, &vertex_copy);
    vkCmdCopyBuffer(command_buffer, index_staging.GetHandle(), m_indices.GetHandle(), 1, &index_copy);
    vkCmdCopyBuffer(command_buffer, instance_staging.GetHandle(), m_instances.GetHandle(), 1, &instance_copy);
    // 实例属性与普通顶点属性在同一阶段读取，索引缓冲使用独立的索引读取依赖。
    const VkBufferMemoryBarrier2 vertex_barrier{
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
      .pNext = nullptr,
      .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
      .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = m_vertices.GetHandle(),
      .offset = 0,
      .size = VK_WHOLE_SIZE,
    };
    std::array barriers{vertex_barrier, vertex_barrier, vertex_barrier};
    barriers.at(1).dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    barriers.at(1).dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT;
    barriers.at(1).buffer = m_indices.GetHandle();
    barriers.back().buffer = m_instances.GetHandle();
    fw::CmdBarrier(command_buffer, barriers, {});
  });
}

void Sample::Draw(VkCommandBuffer cmd, std::uint32_t frame_index) {
  const std::array buffers{m_vertices.GetHandle(), m_instances.GetHandle()};
  const std::array<VkDeviceSize, 2> offsets{};
  vkCmdBindVertexBuffers(cmd, 0, static_cast<uint32_t>(buffers.size()), buffers.data(), offsets.data());
  vkCmdBindIndexBuffer(cmd, m_indices.GetHandle(), 0, VK_INDEX_TYPE_UINT32);
  const auto set = m_sets.at(frame_index);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetLayout(), 0, 1, &set, 0, nullptr);
  vkCmdDrawIndexed(cmd, m_index_count, m_instance_count, 0, 0, 0);
}

void Sample::CreateDescriptors() {
  const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
    {.binding = 0,
     .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
     .descriptorCount = 1,
     .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
     .pImmutableSamplers = nullptr},
    {.binding = 1,
     .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
     .descriptorCount = 1,
     .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
     .pImmutableSamplers = nullptr},
  }};
  m_set_layout = fw::DescriptorSetLayout::Create(*m_device, bindings);
  const uint32_t frame_count = m_frames->GetFrameCount();
  const std::array<VkDescriptorPoolSize, 2> sizes{{
    {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = frame_count},
    {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = frame_count},
  }};
  m_descriptor_pool = fw::DescriptorPool::Create(*m_device, {frame_count, sizes});
  // 所有飞行帧和球体共享纹理，仅 Uniform 保持逐帧独立。
  const VkDescriptorImageInfo image_info{
    .sampler = m_texture.GetSampler(),
    .imageView = m_texture.GetView(),
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  m_uniforms.reserve(frame_count);
  m_sets.reserve(frame_count);
  // 每个飞行帧使用独立的 Uniform 和描述符，避免更新时覆盖仍被其他提交读取的数据。
  for (uint32_t index{}; index < frame_count; ++index) {
    m_uniforms.push_back(fw::Buffer::Create(
      *m_device, {sizeof(CameraUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, fw::MemoryUsage::Upload}));
    const auto set = m_descriptor_pool.Allocate(m_set_layout.GetHandle());
    m_sets.push_back(set);
    const VkDescriptorBufferInfo buffer_info{
      .buffer = m_uniforms.back().GetHandle(),
      .offset = 0,
      .range = sizeof(CameraUniform),
    };
    const std::array<VkWriteDescriptorSet, 2> writes{{
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = nullptr,
       .dstSet = set,
       .dstBinding = 0,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
       .pImageInfo = nullptr,
       .pBufferInfo = &buffer_info,
       .pTexelBufferView = nullptr},
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = nullptr,
       .dstSet = set,
       .dstBinding = 1,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .pImageInfo = &image_info,
       .pBufferInfo = nullptr,
       .pTexelBufferView = nullptr},
    }};
    vkUpdateDescriptorSets(m_device->GetHandle(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }
}

void Sample::CreateDepthImages(VkExtent2D extent) {
  std::vector<fw::Image> images{};
  images.reserve(m_frames->GetFrameCount());
  for (uint32_t index{}; index < m_frames->GetFrameCount(); ++index) {
    fw::ImageDesc desc{};
    desc.extent = extent;
    desc.format = m_depth_format;
    desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    images.push_back(fw::Image::Create(*m_device, desc));
  }
  m_depth_images = std::move(images);
}

void Sample::CreatePipeline(VkFormat color_format) {
  const auto shader_directory = std::filesystem::path{PROJECT_SHADER_DIR};
  auto vertex = fw::ShaderModule::Load(*m_device, shader_directory / "cpu_driven_rendering/instanced_vert.spv");
  auto fragment = fw::ShaderModule::Load(*m_device, shader_directory / "cpu_driven_rendering/instanced_frag.spv");
  const std::array<VkVertexInputBindingDescription, 2> bindings{{
    {.binding = 0, .stride = sizeof(fw::Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
    {.binding = 1, .stride = sizeof(InstanceData), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE},
  }};
  const std::array<VkVertexInputAttributeDescription, 4> attributes{{
    {.location = 0,
     .binding = 0,
     .format = VK_FORMAT_R32G32B32_SFLOAT,
     .offset = static_cast<uint32_t>(offsetof(fw::Vertex, positions))},
    {.location = 1,
     .binding = 0,
     .format = VK_FORMAT_R32G32B32_SFLOAT,
     .offset = static_cast<uint32_t>(offsetof(fw::Vertex, normals))},
    {.location = 2,
     .binding = 0,
     .format = VK_FORMAT_R32G32_SFLOAT,
     .offset = static_cast<uint32_t>(offsetof(fw::Vertex, texcoords))},
    {.location = 3,
     .binding = 1,
     .format = VK_FORMAT_R32G32B32_SFLOAT,
     .offset = static_cast<uint32_t>(offsetof(InstanceData, offset))},
  }};
  const auto set_layout = m_set_layout.GetHandle();
  fw::GraphicsPipelineDesc desc{};
  desc.vertex_shader = vertex.GetHandle();
  desc.fragment_shader = fragment.GetHandle();
  desc.layout.set_layouts = std::span{&set_layout, 1};
  desc.vertex_bindings = bindings;
  desc.vertex_attributes = attributes;
  desc.color_formats = std::span{&color_format, 1};
  desc.depth_format = m_depth_format;
  m_pipeline = fw::Pipeline::CreateGraphics(*m_device, desc);
  m_color_format = color_format;
}

void Sample::OnResize(const fw::SwapchainInfo& swapchain) {
  m_extent = swapchain.extent;
  CreateDepthImages(swapchain.extent);
  // 视口和裁剪矩形是动态状态；只有颜色格式变化才影响管线的附件兼容性。
  if (m_pipeline.GetHandle() == VK_NULL_HANDLE || m_color_format != swapchain.format) {
    CreatePipeline(swapchain.format);
  }
}

void Sample::ResetAll() noexcept {
  m_camera.ResetCamera(m_scene_bounds, static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height));
  m_rotation = 0.0F;
  m_rotation_speed = 0.5F;
  m_enable_rotation = true;
  m_clear_color = {0.025F, 0.035F, 0.06F, 1.0F};
}

void Sample::OnUpdate(const fw::FrameContext& frame, const fw::InputState& input, float delta_seconds) {
  if (input.IsPressed(fw::Key::Space)) {
    m_enable_rotation = !m_enable_rotation;
  }
  if (input.IsPressed(fw::Key::R)) {
    ResetAll();
  } else if (input.IsPressed(fw::Key::Home)) {
    m_camera.ResetCamera(m_scene_bounds,
                         static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height));
  }
  m_camera.Update(input, delta_seconds);
  if (m_enable_rotation) {
    m_rotation = std::remainder(m_rotation + delta_seconds * m_rotation_speed, 6.283185307F);
  }
  CameraUniform uniform{};
  auto model = glm::rotate(glm::mat4{1.0F}, m_rotation, glm::vec3{0.0F, 1.0F, 0.0F});
  const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
  uniform.model_view_projection = m_camera.GetProjectionMatrix(aspect) * m_camera.GetViewMatrix() * model;
  // 按帧槽位而非交换链图像选择缓冲，该槽位的 fence 已在更新回调前等待完成。
  m_uniforms.at(frame.frame_index).Write(std::as_bytes(std::span{&uniform, 1}));
}

void Sample::OnRender(const fw::FrameContext& frame) {
  BeginAttachments(frame, m_depth_images.at(frame.frame_index), m_clear_color);
  vkCmdBindPipeline(frame.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetHandle());
  const VkViewport viewport{
    .x = 0.0F,
    .y = 0.0F,
    .width = static_cast<float>(frame.extent.width),
    .height = static_cast<float>(frame.extent.height),
    .minDepth = 0.0F,
    .maxDepth = 1.0F,
  };
  const VkRect2D scissor{.offset = {.x = 0, .y = 0}, .extent = frame.extent};
  vkCmdSetViewport(frame.command_buffer, 0, 1, &viewport);
  vkCmdSetScissor(frame.command_buffer, 0, 1, &scissor);
  Draw(frame.command_buffer, frame.frame_index);
  vkCmdEndRendering(frame.command_buffer);
}

void Sample::OnGui() {
  ImGui::SetNextWindowPos(ImVec2{16.0F, 16.0F}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Instanced Rendering", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(std::data(m_device->GetInfo().properties.deviceName));
    ImGui::TextUnformatted(
      std::format("Instances: {} | Draw calls: 1", static_cast<unsigned int>(m_instance_count)).c_str());
    std::ostringstream stats{};
    stats << std::fixed << std::setprecision(1) << ImGui::GetIO().Framerate << " FPS | " << m_extent.width << " x "
          << m_extent.height;
    ImGui::TextUnformatted(stats.str().c_str());
    ImGui::Separator();
    ImGui::Checkbox("Auto rotate", &m_enable_rotation);
    ImGui::SliderFloat("Speed (rad/s)", &m_rotation_speed, 0.0F, 2.0F);
    ImGui::SliderAngle("Angle", &m_rotation, -180.0F, 180.0F);
    ImGui::ColorEdit3("Background", m_clear_color.data());
    if (ImGui::Button("Reset camera")) {
      m_camera.ResetCamera(m_scene_bounds, static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height));
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset all")) {
      ResetAll();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("LMB: orbit | RMB: pan | Wheel: zoom");
    ImGui::TextUnformatted("WASD/QE: move | Space: pause");
    ImGui::TextUnformatted("Home: camera | R: reset | Esc: exit");
  }
  // 无论 Begin 返回值如何，ImGui 窗口作用域都必须与 End 配对。
  ImGui::End();
}
}  // namespace demo
