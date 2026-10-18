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
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "framework/device.h"
#include "framework/frame_scheduler.h"
#include "framework/shader_module.h"
#include "framework/shapes.h"
#include "framework/synchronization.h"
#include "framework/window.h"

namespace demo {
namespace {
struct alignas(16) CameraUniform {
  glm::mat4 model_view_projection{1.0F};
  glm::mat4 view_projection{1.0F};
  glm::mat4 inverse_culling_view_projection{1.0F};
};
static_assert(sizeof(CameraUniform) == 192, "CameraUniform must match the GLSL std140 block");

struct alignas(16) DrawInput {
  glm::vec4 bounding_sphere{};
  std::uint32_t index_count{};
  std::uint32_t first_index{};
  std::int32_t vertex_offset{};
};
static_assert(sizeof(DrawInput) == 32, "DrawInput must match the GLSL std430 array stride");
static_assert(offsetof(DrawInput, index_count) == 16);
static_assert(offsetof(DrawInput, first_index) == 20);
static_assert(offsetof(DrawInput, vertex_offset) == 24);

struct alignas(16) CullingUniform {
  std::array<glm::vec4, 6> frustum_planes{};
  std::array<std::uint32_t, 4> options{};
};
static_assert(sizeof(CullingUniform) == 112, "CullingUniform must match the GLSL std140 block");
static_assert(offsetof(CullingUniform, options) == 96, "CullingUniform options must match GLSL");

static_assert(sizeof(VkDrawIndexedIndirectCommand) == 20, "Indirect command must match the GLSL std430 struct");
// 与 frustum_culling.comp 的 local_size_x 保持一致。
constexpr std::uint32_t k_compute_group_size{64};
constexpr std::uint32_t k_compute_begin{0};
constexpr std::uint32_t k_compute_end{1};
constexpr std::uint32_t k_draw_end{2};
constexpr std::uint32_t k_debug_end{3};
constexpr std::uint32_t k_timestamp_count{4};
// 三个大圆，每个圆有 32 条线段，与 frustum_debug.vert 保持一致。
constexpr std::uint32_t k_sphere_line_vertices{3 * 32 * 2};

std::array<glm::vec3, 8> GetFrustumCorners(const glm::mat4& inverse_view_projection) {
  std::array<glm::vec3, 8> corners{};
  for (std::size_t index{}; index < corners.size(); ++index) {
    const glm::vec4 clip{
      (index & 1U) != 0 ? 1.0F : -1.0F,
      (index & 2U) != 0 ? 1.0F : -1.0F,
      (index & 4U) != 0 ? 1.0F : 0.0F,
      1.0F,
    };
    const auto world = inverse_view_projection * clip;
    corners.at(index) = glm::vec3{world} / world.w;
  }
  return corners;
}

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
  m_gpu_timer = fw::GpuTimer::Create(*m_device, m_frames->GetFrameCount(), k_timestamp_count);
  m_depth_format = fw::Image::FindDepthFormat(*m_device);
  CreateGeometry();
  CreateComputeResources();
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
  constexpr std::uint32_t grid_size{10};
  constexpr std::uint32_t object_count{grid_size * grid_size * grid_size};
  constexpr float start = (grid_size - 1) * -1.0F;
  if (object_count > m_device->GetInfo().properties.limits.maxDrawIndirectCount) {
    throw std::runtime_error("Sphere grid exceeds maxDrawIndirectCount for a single indirect batch");
  }
  m_object_count = object_count;

  const auto sphere = fw::Shapes::Sphere(0.5F, 64, 64, glm::vec3{0.0F});
  std::vector<fw::Vertex> vertices{};
  std::vector<std::uint32_t> indices{};
  std::vector<DrawInput> inputs{};
  vertices.reserve(sphere.vertices.size() * object_count);
  indices.reserve(sphere.indices.size() * object_count);
  inputs.reserve(object_count);
  for (std::uint32_t x{}; x < grid_size; ++x) {
    for (std::uint32_t y{}; y < grid_size; ++y) {
      for (std::uint32_t z{}; z < grid_size; ++z) {
        const glm::vec3 center{start + static_cast<float>(x) * 2.0F, start + static_cast<float>(y) * 2.0F,
                               start + static_cast<float>(z) * 2.0F};
        m_scene_bounds.Merge({sphere.bounds.GetMin() + center, sphere.bounds.GetMax() + center});
        inputs.push_back({
          .bounding_sphere = {center, sphere.bounds.GetBoundingRadius()},
          .index_count = static_cast<std::uint32_t>(sphere.indices.size()),
          .first_index = static_cast<std::uint32_t>(indices.size()),
          .vertex_offset = static_cast<std::int32_t>(vertices.size()),
        });
        for (auto vertex : sphere.vertices) {
          vertex.positions += center;
          vertices.push_back(vertex);
        }
        // 保留网格的局部索引，绘制时由 vertexOffset 定位到各自的顶点范围。
        indices.insert(indices.end(), sphere.indices.begin(), sphere.indices.end());
      }
    }
  }

  const auto input_bytes = std::as_bytes(std::span{inputs});
  m_draw_inputs = fw::Buffer::Create(
    *m_device, {input_bytes.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, fw::MemoryUsage::Upload});
  m_draw_inputs.Write(input_bytes);

  const auto vertex_bytes = std::as_bytes(std::span{vertices});
  const auto index_bytes = std::as_bytes(std::span{indices});
  auto vertex_staging = fw::Buffer::Create(
    *m_device, {vertex_bytes.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, fw::MemoryUsage::Upload});
  auto index_staging = fw::Buffer::Create(
    *m_device, {index_bytes.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, fw::MemoryUsage::Upload});
  vertex_staging.Write(vertex_bytes);
  index_staging.Write(index_bytes);
  m_vertices = fw::Buffer::Create(
    *m_device, {vertex_bytes.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT});
  m_indices = fw::Buffer::Create(
    *m_device, {index_bytes.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT});
  // 两次复制合并为一次同步上传，返回后局部暂存缓冲才可安全销毁。
  m_device->SubmitImmediate([&](VkCommandBuffer command_buffer) {
    const VkBufferCopy vertex_copy{.srcOffset = 0, .dstOffset = 0, .size = vertex_bytes.size_bytes()};
    const VkBufferCopy index_copy{.srcOffset = 0, .dstOffset = 0, .size = index_bytes.size_bytes()};
    vkCmdCopyBuffer(command_buffer, vertex_staging.GetHandle(), m_vertices.GetHandle(), 1, &vertex_copy);
    vkCmdCopyBuffer(command_buffer, index_staging.GetHandle(), m_indices.GetHandle(), 1, &index_copy);
    // 将复制写入显式关联到后续顶点和索引读取，CPU 等待上传完成不能代替这项 GPU 依赖。
    std::array<VkBufferMemoryBarrier2, 2> barriers{};
    auto& vertex_barrier = barriers.front();
    vertex_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    vertex_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    vertex_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    vertex_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
    vertex_barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    vertex_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vertex_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vertex_barrier.buffer = m_vertices.GetHandle();
    vertex_barrier.size = VK_WHOLE_SIZE;
    barriers.back() = vertex_barrier;
    barriers.back().dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    barriers.back().dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT;
    barriers.back().buffer = m_indices.GetHandle();
    fw::CmdBarrier(command_buffer, barriers, {});
  });
}

void Sample::CreateComputeResources() {
  const std::array<VkDescriptorSetLayoutBinding, 5> bindings{{
    {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, nullptr},
    {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, nullptr},
  }};
  m_compute_set_layout = fw::DescriptorSetLayout::Create(*m_device, bindings);
  const auto frame_count = m_frames->GetFrameCount();
  const std::array<VkDescriptorPoolSize, 2> sizes{{
    {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = frame_count * 4},
    {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = frame_count},
  }};
  m_compute_descriptor_pool = fw::DescriptorPool::Create(*m_device, {frame_count, sizes});

  const VkDeviceSize command_bytes = m_object_count * sizeof(VkDrawIndexedIndirectCommand);
  const VkDeviceSize visibility_bytes = m_object_count * sizeof(std::uint32_t);
  constexpr auto indirect_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
  m_compute_frames.resize(frame_count);
  for (auto& resources : m_compute_frames) {
    resources.indirect_commands = fw::Buffer::Create(*m_device, {command_bytes, indirect_usage});
    resources.draw_count = fw::Buffer::Create(
      *m_device,
      {sizeof(std::uint32_t), indirect_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT});
    resources.visibility = fw::Buffer::Create(*m_device, {visibility_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT});
    resources.culling_uniform = fw::Buffer::Create(
      *m_device, {sizeof(CullingUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, fw::MemoryUsage::Upload});
    resources.count_readback = fw::Buffer::Create(
      *m_device, {sizeof(std::uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT, fw::MemoryUsage::Readback});
    resources.set = m_compute_descriptor_pool.Allocate(m_compute_set_layout.GetHandle());

    const std::array<VkDescriptorBufferInfo, bindings.size()> buffer_infos{{
      {m_draw_inputs.GetHandle(), 0, m_draw_inputs.GetSize()},
      {resources.indirect_commands.GetHandle(), 0, command_bytes},
      {resources.culling_uniform.GetHandle(), 0, sizeof(CullingUniform)},
      {resources.draw_count.GetHandle(), 0, sizeof(std::uint32_t)},
      {resources.visibility.GetHandle(), 0, visibility_bytes},
    }};
    std::array<VkWriteDescriptorSet, bindings.size()> writes{};
    for (std::size_t index{}; index < writes.size(); ++index) {
      writes[index] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = resources.set,
        .dstBinding = bindings[index].binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = bindings[index].descriptorType,
        .pImageInfo = nullptr,
        .pBufferInfo = &buffer_infos[index],
        .pTexelBufferView = nullptr,
      };
    }
    vkUpdateDescriptorSets(m_device->GetHandle(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  auto shader =
    fw::ShaderModule::Load(*m_device, std::filesystem::path{PROJECT_SHADER_DIR} / "frustum_culling_comp.spv");
  const auto set_layout = m_compute_set_layout.GetHandle();
  fw::ComputePipelineDesc desc{};
  desc.shader = shader.GetHandle();
  desc.layout.set_layouts = std::span{&set_layout, 1};
  m_compute_pipeline = fw::Pipeline::CreateCompute(*m_device, desc);
}

void Sample::GenerateDrawCommands(const fw::FrameContext& frame) {
  const auto cmd = frame.command_buffer;
  auto& resources = m_compute_frames.at(frame.frame_index);
  const auto count_buffer = resources.draw_count.GetHandle();
  vkCmdFillBuffer(cmd, count_buffer, 0, sizeof(std::uint32_t), 0);
  const VkBufferMemoryBarrier2 count_reset{
    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .buffer = count_buffer,
    .offset = 0,
    .size = VK_WHOLE_SIZE,
  };
  fw::CmdBarrier(cmd, std::span{&count_reset, 1}, {});

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute_pipeline.GetHandle());
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute_pipeline.GetLayout(), 0, 1, &resources.set, 0,
                          nullptr);
  vkCmdDispatch(cmd, (m_object_count + k_compute_group_size - 1) / k_compute_group_size, 1, 1);

  const VkBufferMemoryBarrier2 commands_ready{
    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
    .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .buffer = resources.indirect_commands.GetHandle(),
    .offset = 0,
    .size = VK_WHOLE_SIZE,
  };
  // 全部剔除时 Compute 不写计数，Indirect 和回读仍要看到清零操作。
  auto count_ready = commands_ready;
  count_ready.srcStageMask |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
  count_ready.srcAccessMask |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
  count_ready.dstStageMask |= VK_PIPELINE_STAGE_2_COPY_BIT;
  count_ready.dstAccessMask |= VK_ACCESS_2_TRANSFER_READ_BIT;
  count_ready.buffer = count_buffer;

  auto visibility_ready = commands_ready;
  visibility_ready.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
  visibility_ready.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
  visibility_ready.buffer = resources.visibility.GetHandle();
  const std::array barriers{commands_ready, count_ready, visibility_ready};
  fw::CmdBarrier(cmd, barriers, {});

  // 只为 UI 异步回读 4 字节，绘制始终直接读 GPU 计数缓冲。
  const auto readback = resources.count_readback.GetHandle();
  const VkBufferCopy count_copy{.srcOffset = 0, .dstOffset = 0, .size = sizeof(std::uint32_t)};
  vkCmdCopyBuffer(cmd, count_buffer, readback, 1, &count_copy);
  auto readback_ready = commands_ready;
  readback_ready.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  readback_ready.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  readback_ready.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
  readback_ready.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
  readback_ready.buffer = readback;
  fw::CmdBarrier(cmd, std::span{&readback_ready, 1}, {});
  resources.has_count_result = true;
}

void Sample::Draw(VkCommandBuffer cmd, std::uint32_t frame_index) {
  const auto set = m_sets.at(frame_index);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetLayout(), 0, 1, &set, 0, nullptr);
  const auto vertex_buffer = m_vertices.GetHandle();
  const VkDeviceSize offset{};
  vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
  vkCmdBindIndexBuffer(cmd, m_indices.GetHandle(), 0, VK_INDEX_TYPE_UINT32);

  const auto& resources = m_compute_frames.at(frame_index);
  vkCmdDrawIndexedIndirectCount(cmd, resources.indirect_commands.GetHandle(), 0, resources.draw_count.GetHandle(), 0,
                                m_object_count, sizeof(VkDrawIndexedIndirectCommand));
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
  auto vertex = fw::ShaderModule::Load(*m_device, shader_directory / "frustum_culling_vert.spv");
  auto fragment = fw::ShaderModule::Load(*m_device, shader_directory / "frustum_culling_frag.spv");
  const VkVertexInputBindingDescription binding{
    .binding = 0,
    .stride = sizeof(fw::Vertex),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
  };
  const std::array<VkVertexInputAttributeDescription, 3> attributes{{
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
  CreateDepthImages(swapchain.extent);
  // 视口和裁剪矩形是动态状态；只有颜色格式变化才影响管线的附件兼容性。
  if (m_pipeline.GetHandle() == VK_NULL_HANDLE || m_color_format != swapchain.format) {
    CreatePipeline(swapchain.format);
    CreateDebugPipeline(swapchain.format);
  }
}

void Sample::CreateDebugPipeline(VkFormat color_format) {
  const auto shader_directory = std::filesystem::path{PROJECT_SHADER_DIR};
  auto vertex = fw::ShaderModule::Load(*m_device, shader_directory / "frustum_debug_vert.spv");
  auto fragment = fw::ShaderModule::Load(*m_device, shader_directory / "frustum_debug_frag.spv");
  const std::array layouts{m_set_layout.GetHandle(), m_compute_set_layout.GetHandle()};
  const VkPushConstantRange mode_range{
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    .offset = 0,
    .size = sizeof(std::uint32_t),
  };
  fw::GraphicsPipelineDesc desc{};
  desc.vertex_shader = vertex.GetHandle();
  desc.fragment_shader = fragment.GetHandle();
  desc.layout.set_layouts = layouts;
  desc.layout.push_constants = std::span{&mode_range, 1};
  desc.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  desc.cull_mode = VK_CULL_MODE_NONE;
  desc.color_formats = std::span{&color_format, 1};
  desc.depth_format = m_depth_format;
  // 线框作为覆盖层独立绘制，剔除物体的包围球也始终可见。
  desc.depth_compare = VK_COMPARE_OP_ALWAYS;
  desc.enable_depth_write = false;
  m_debug_pipeline = fw::Pipeline::CreateGraphics(*m_device, desc);
}

void Sample::DrawDebug(const fw::FrameContext& frame) {
  if (!m_show_frustum && !m_show_bounds) {
    return;
  }
  const auto cmd = frame.command_buffer;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debug_pipeline.GetHandle());
  const std::array sets{m_sets.at(frame.frame_index), m_compute_frames.at(frame.frame_index).set};
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debug_pipeline.GetLayout(), 0,
                          static_cast<std::uint32_t>(sets.size()), sets.data(), 0, nullptr);
  if (m_show_frustum) {
    const std::uint32_t mode{0};
    vkCmdPushConstants(cmd, m_debug_pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mode), &mode);
    vkCmdDraw(cmd, 24, 1, 0, 0);
  }
  if (m_show_bounds) {
    const std::uint32_t mode{1};
    vkCmdPushConstants(cmd, m_debug_pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mode), &mode);
    vkCmdDraw(cmd, k_sphere_line_vertices, m_object_count, 0, 0);
  }
}

void Sample::ResetAll() noexcept {
  m_camera.ResetCamera(m_scene_bounds, static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height));
  m_rotation = 0.0F;
  m_rotation_speed = 0.5F;
  m_enable_rotation = true;
  m_enable_culling = true;
  m_freeze_frustum = false;
  m_has_culling_frustum = false;
  m_show_frustum = false;
  m_show_bounds = false;
  m_clear_color = {0.025F, 0.035F, 0.06F, 1.0F};
}

void Sample::OnUpdate(const fw::FrameContext& frame, const fw::InputState& input, float delta_seconds) {
  // 此帧槽位的 fence 已完成，在复用 Query Pool 前读取其上一次提交的结果。
  m_gpu_timer.BeginFrame(frame);
  auto& resources = m_compute_frames.at(frame.frame_index);
  if (resources.has_count_result) {
    std::uint32_t count{};
    resources.count_readback.Read(std::as_writable_bytes(std::span{&count, 1}));
    m_last_visible_count = count;
  }
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
  // 当前模型只进行刚性旋转，世界空间球心随模型变换，包围球半径保持不变。
  const auto scene_center = glm::vec3{model * glm::vec4{m_scene_bounds.GetCenter(), 1.0F}};
  m_camera.SetSceneBoundingSphere(scene_center, m_scene_bounds.GetBoundingRadius());
  const auto view = m_camera.GetViewMatrix();
  uniform.view_projection = m_camera.GetProjectionMatrix(aspect) * view;
  if (!m_freeze_frustum || !m_has_culling_frustum) {
    m_culling_view_projection = uniform.view_projection;
    m_has_culling_frustum = true;
  }
  uniform.inverse_culling_view_projection = glm::inverse(m_culling_view_projection);
  if (m_freeze_frustum && m_show_frustum) {
    // 观察范围纳入真实冻结视锥的八个角点；冻结的 GPU 剔除矩阵与相机位姿均保持不变。
    const auto corners = GetFrustumCorners(uniform.inverse_culling_view_projection);
    uniform.view_projection = m_camera.GetProjectionMatrix(aspect, corners) * view;
  }
  uniform.model_view_projection = uniform.view_projection * model;
  // 按帧槽位而非交换链图像选择缓冲，该槽位的 fence 已在更新回调前等待完成。
  m_uniforms.at(frame.frame_index).Write(std::as_bytes(std::span{&uniform, 1}));

  // 冻结相机视锥后，场景模型变换仍实时更新；球体与剔除平面保持在同一坐标系。
  const auto rows = glm::transpose(m_culling_view_projection * model);
  CullingUniform culling{};
  culling.frustum_planes = {
    rows[3] + rows[0],  // x >= -w
    rows[3] - rows[0],  // x <= w
    rows[3] + rows[1],  // y >= -w
    rows[3] - rows[1],  // y <= w
    rows[2],            // Vulkan 近裁剪面：z >= 0
    rows[3] - rows[2],  // 远裁剪面：z <= w
  };
  for (auto& plane : culling.frustum_planes) {
    plane /= glm::length(glm::vec3{plane});
  }
  culling.options.front() = m_enable_culling ? 1U : 0U;
  resources.culling_uniform.Write(std::as_bytes(std::span{&culling, 1}));
}

void Sample::OnRender(const fw::FrameContext& frame) {
  m_gpu_timer.WriteTimestamp(frame, k_compute_begin);
  // Dispatch 与 Compute -> Indirect 屏障都位于动态渲染作用域之外。
  GenerateDrawCommands(frame);
  m_gpu_timer.WriteTimestamp(frame, k_compute_end);
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
  m_gpu_timer.WriteTimestamp(frame, k_draw_end);
  DrawDebug(frame);
  m_gpu_timer.WriteTimestamp(frame, k_debug_end);
  vkCmdEndRendering(frame.command_buffer);
}

void Sample::OnGui() {
  ImGui::SetNextWindowPos(ImVec2{16.0F, 16.0F}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Frustum Culling", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(std::data(m_device->GetInfo().properties.deviceName));
    ImGui::TextUnformatted(std::format("Objects: {} | Indirect submissions: 1", m_object_count).c_str());
    if (m_last_visible_count) {
      ImGui::TextUnformatted(std::format("Visible (completed frame): {}", *m_last_visible_count).c_str());
    } else {
      ImGui::TextUnformatted("Visible (completed frame): --");
    }
    std::ostringstream stats{};
    stats << std::fixed << std::setprecision(1) << ImGui::GetIO().Framerate << " FPS | " << m_extent.width << " x "
          << m_extent.height;
    ImGui::TextUnformatted(stats.str().c_str());
    if (m_gpu_timer.IsSupported()) {
      const auto show_timing = [&](const char* label, std::uint32_t begin, std::uint32_t end) {
        const auto milliseconds = m_gpu_timer.GetElapsedMilliseconds(begin, end);
        if (milliseconds) {
          ImGui::TextUnformatted(std::format("{}: {:.3f} ms", label, *milliseconds).c_str());
        } else {
          ImGui::TextUnformatted(std::format("{}: --", label).c_str());
        }
      };
      show_timing("GPU Compute", k_compute_begin, k_compute_end);
      show_timing("GPU Scene", k_compute_end, k_draw_end);
      show_timing("GPU Debug", k_draw_end, k_debug_end);
      ImGui::TextUnformatted("GPU timings: completed frames, excluding ImGui");
    } else {
      ImGui::TextUnformatted("GPU timestamps unavailable on this queue");
    }
    ImGui::Separator();
    ImGui::Checkbox("Frustum culling", &m_enable_culling);
    ImGui::Checkbox("Freeze culling frustum", &m_freeze_frustum);
    ImGui::Checkbox("Show culling frustum", &m_show_frustum);
    ImGui::Checkbox("Show bounding spheres", &m_show_bounds);
    if (m_show_frustum || m_show_bounds) {
      ImGui::TextUnformatted("Yellow: frustum | Green: drawn | Red: culled");
      ImGui::TextUnformatted("Freeze the frustum, then move the camera to inspect");
      ImGui::TextUnformatted("Disable debug overlays when comparing performance");
    }
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
