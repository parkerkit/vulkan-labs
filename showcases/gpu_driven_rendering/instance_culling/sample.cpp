#include "sample.h"

#include <imgui.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>
#include <span>
#include <stdexcept>
#include <vector>

#include "framework/device.h"
#include "framework/frame_scheduler.h"
#include "framework/shader_module.h"
#include "framework/shapes.h"
#include "framework/synchronization.h"

namespace demo {
namespace {
constexpr std::uint32_t k_compute_group_size{64};
constexpr std::uint32_t k_compute_begin{0};
constexpr std::uint32_t k_compute_end{1};
constexpr std::uint32_t k_scene_end{2};

struct LodVertex {
  glm::vec3 position{};
  glm::vec3 normal{};
  std::uint32_t lod{};
};

struct alignas(16) CullingUniform {
  std::array<glm::vec4, 6> frustum_planes{};
  glm::vec4 view_depth{};
  glm::vec4 lod_parameters{};          // 投影比例、高档阈值、中档阈值、填充
  glm::uvec4 options{};                // 视锥剔除、强制最高档、填充、填充
  std::array<glm::uvec4, 3> meshes{};  // indexCount、firstIndex、vertexOffset、填充
};
static_assert(sizeof(glm::mat4) == 64);
static_assert(sizeof(glm::vec4) == 16 && sizeof(glm::uvec4) == 16);
static_assert(sizeof(CullingUniform) == 192);
static_assert(offsetof(CullingUniform, view_depth) == 96);
static_assert(offsetof(CullingUniform, lod_parameters) == 112);
static_assert(offsetof(CullingUniform, options) == 128);
static_assert(offsetof(CullingUniform, meshes) == 144);
static_assert(sizeof(VkDrawIndexedIndirectCommand) == 20);

VkBufferMemoryBarrier2 BufferBarrier(VkBuffer buffer, VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
                                     VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access) {
  return {
    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = source_stage,
    .srcAccessMask = source_access,
    .dstStageMask = destination_stage,
    .dstAccessMask = destination_access,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .buffer = buffer,
    .offset = 0,
    .size = VK_WHOLE_SIZE,
  };
}

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
  const auto frame_count = context.frames.GetFrameCount();
  m_frames.resize(frame_count);
  m_gpu_timer = fw::GpuTimer::Create(*m_device, frame_count, k_scene_end + 1);
  m_depth_format = fw::Image::FindDepthFormat(*m_device);
  CreateGeometry();
  CreateFrameResources();
  CreateComputePipeline();
  OnResize(swapchain);
  ResetCamera();
}

void Sample::CreateGeometry() {
  constexpr std::uint32_t grid_size{10};
  constexpr float start = -static_cast<float>(grid_size - 1);
  constexpr float radius{0.5F};
  constexpr std::array segments{48U, 24U, 8U};
  m_object_count = grid_size * grid_size * grid_size;
  if (m_object_count > m_device->GetInfo().properties.limits.maxDrawIndirectCount) {
    throw std::runtime_error("Sphere grid exceeds maxDrawIndirectCount for a single indirect batch");
  }

  std::vector<LodVertex> vertices{};
  std::vector<std::uint32_t> indices{};
  for (std::uint32_t lod{}; lod < segments.size(); ++lod) {
    const auto mesh = fw::Shapes::Sphere(radius, segments[lod], segments[lod], glm::vec3{0.0F});
    m_lod_meshes[lod] = {static_cast<std::uint32_t>(mesh.indices.size()), static_cast<std::uint32_t>(indices.size()),
                         static_cast<std::uint32_t>(vertices.size()), 0U};
    for (const auto& vertex : mesh.vertices) {
      vertices.push_back({vertex.positions, vertex.normals, lod});
    }
    // 索引保持网格局部编号，命令的 vertexOffset 将其定位到共享顶点缓冲。
    indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
  }

  std::vector<glm::vec4> objects{};
  objects.reserve(m_object_count);
  for (std::uint32_t x{}; x < grid_size; ++x) {
    for (std::uint32_t y{}; y < grid_size; ++y) {
      for (std::uint32_t z{}; z < grid_size; ++z) {
        const glm::vec3 center{start + 2.0F * static_cast<float>(x), start + 2.0F * static_cast<float>(y),
                               start + 2.0F * static_cast<float>(z)};
        objects.emplace_back(center, radius);
        m_scene_bounds.Merge({center - glm::vec3{radius}, center + glm::vec3{radius}});
      }
    }
  }
  const auto object_bytes = std::as_bytes(std::span{objects});
  // Compute 和顶点着色器通过物体索引访问同一份球心和半径。
  m_objects = fw::Buffer::Create(
    *m_device, {object_bytes.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, fw::MemoryUsage::Upload});
  m_objects.Write(object_bytes);

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
  m_device->SubmitImmediate([&](VkCommandBuffer cmd) {
    const VkBufferCopy vertex_copy{.srcOffset = 0, .dstOffset = 0, .size = vertex_bytes.size_bytes()};
    const VkBufferCopy index_copy{.srcOffset = 0, .dstOffset = 0, .size = index_bytes.size_bytes()};
    vkCmdCopyBuffer(cmd, vertex_staging.GetHandle(), m_vertices.GetHandle(), 1, &vertex_copy);
    vkCmdCopyBuffer(cmd, index_staging.GetHandle(), m_indices.GetHandle(), 1, &index_copy);
    const std::array barriers{
      BufferBarrier(m_vertices.GetHandle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT),
      BufferBarrier(m_indices.GetHandle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT),
    };
    fw::CmdBarrier(cmd, barriers, {});
  });
}

void Sample::CreateFrameResources() {
  const std::array<VkDescriptorSetLayoutBinding, 5> compute_bindings{{
    {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
  }};
  const std::array<VkDescriptorSetLayoutBinding, 2> graphics_bindings{{
    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
    {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
  }};
  m_compute_set_layout = fw::DescriptorSetLayout::Create(*m_device, compute_bindings);
  m_graphics_set_layout = fw::DescriptorSetLayout::Create(*m_device, graphics_bindings);
  const auto frame_count = static_cast<std::uint32_t>(m_frames.size());
  const std::array<VkDescriptorPoolSize, 2> pool_sizes{{
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame_count * 5},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame_count * 2},
  }};
  m_descriptor_pool = fw::DescriptorPool::Create(*m_device, {frame_count * 2, pool_sizes});

  constexpr auto indirect_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
  for (auto& frame : m_frames) {
    frame.indirect_commands =
      fw::Buffer::Create(*m_device, {m_object_count * sizeof(VkDrawIndexedIndirectCommand), indirect_usage});
    // 每个 LOD 的区域可容纳全部物体，任何可见性分布均不会溢出。
    const auto visible_bytes = m_lod_meshes.size() * m_object_count * sizeof(std::uint32_t);
    frame.visible_instances = fw::Buffer::Create(
      *m_device, {visible_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT});
    frame.stats = fw::Buffer::Create(*m_device, {sizeof(LodStats), indirect_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT});
    frame.stats_readback =
      fw::Buffer::Create(*m_device, {sizeof(LodStats), VK_BUFFER_USAGE_TRANSFER_DST_BIT, fw::MemoryUsage::Readback});
    frame.culling_uniform = fw::Buffer::Create(
      *m_device, {sizeof(CullingUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, fw::MemoryUsage::Upload});
    frame.camera_uniform =
      fw::Buffer::Create(*m_device, {sizeof(glm::mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, fw::MemoryUsage::Upload});
    frame.compute_set = m_descriptor_pool.Allocate(m_compute_set_layout.GetHandle());
    frame.graphics_set = m_descriptor_pool.Allocate(m_graphics_set_layout.GetHandle());

    const std::array<VkDescriptorBufferInfo, 7> infos{{
      {m_objects.GetHandle(), 0, m_objects.GetSize()},
      {frame.indirect_commands.GetHandle(), 0, frame.indirect_commands.GetSize()},
      {frame.culling_uniform.GetHandle(), 0, frame.culling_uniform.GetSize()},
      {frame.stats.GetHandle(), 0, frame.stats.GetSize()},
      {frame.visible_instances.GetHandle(), 0, frame.visible_instances.GetSize()},
      {frame.camera_uniform.GetHandle(), 0, frame.camera_uniform.GetSize()},
      {m_objects.GetHandle(), 0, m_objects.GetSize()},
    }};
    std::array<VkWriteDescriptorSet, infos.size()> writes{};
    for (std::size_t index{}; index < compute_bindings.size(); ++index) {
      auto& write = writes[index];
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = frame.compute_set;
      write.dstBinding = compute_bindings[index].binding;
      write.descriptorCount = 1;
      write.descriptorType = compute_bindings[index].descriptorType;
      write.pBufferInfo = &infos[index];
    }
    for (std::size_t index{}; index < graphics_bindings.size(); ++index) {
      const auto write_index = compute_bindings.size() + index;
      auto& write = writes[write_index];
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = frame.graphics_set;
      write.dstBinding = graphics_bindings[index].binding;
      write.descriptorCount = 1;
      write.descriptorType = graphics_bindings[index].descriptorType;
      write.pBufferInfo = &infos[write_index];
    }
    vkUpdateDescriptorSets(m_device->GetHandle(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }
}

void Sample::CreateComputePipeline() {
  const auto shader_directory = std::filesystem::path{PROJECT_SHADER_DIR};
  const auto set_layout = m_compute_set_layout.GetHandle();
  const auto create_pipeline = [&](const char* filename) {
    auto shader = fw::ShaderModule::Load(*m_device, shader_directory / filename);
    fw::ComputePipelineDesc desc{};
    desc.shader = shader.GetHandle();
    desc.layout.set_layouts = std::span{&set_layout, 1};
    return fw::Pipeline::CreateCompute(*m_device, desc);
  };
  m_per_object_pipeline = create_pipeline("per_object.comp.spv");
  m_bin_instances_pipeline = create_pipeline("bin_instances.comp.spv");
  m_build_batches_pipeline = create_pipeline("build_batches.comp.spv");
}

void Sample::CreateGraphicsPipeline(VkFormat color_format) {
  const auto shader_directory = std::filesystem::path{PROJECT_SHADER_DIR};
  auto vertex = fw::ShaderModule::Load(*m_device, shader_directory / "lod.vert.spv");
  auto fragment = fw::ShaderModule::Load(*m_device, shader_directory / "lod.frag.spv");
  const std::array<VkVertexInputBindingDescription, 2> bindings{{
    {0, sizeof(LodVertex), VK_VERTEX_INPUT_RATE_VERTEX},
    {1, sizeof(std::uint32_t), VK_VERTEX_INPUT_RATE_INSTANCE},
  }};
  const std::array<VkVertexInputAttributeDescription, 4> attributes{{
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(LodVertex, position))},
    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(LodVertex, normal))},
    {2, 0, VK_FORMAT_R32_UINT, static_cast<std::uint32_t>(offsetof(LodVertex, lod))},
    {3, 1, VK_FORMAT_R32_UINT, 0},
  }};
  const auto set_layout = m_graphics_set_layout.GetHandle();
  fw::GraphicsPipelineDesc desc{};
  desc.vertex_shader = vertex.GetHandle();
  desc.fragment_shader = fragment.GetHandle();
  desc.layout.set_layouts = std::span{&set_layout, 1};
  desc.vertex_bindings = bindings;
  desc.vertex_attributes = attributes;
  desc.color_formats = std::span{&color_format, 1};
  desc.depth_format = m_depth_format;
  m_graphics_pipeline = fw::Pipeline::CreateGraphics(*m_device, desc);
  m_color_format = color_format;
}

void Sample::OnResize(const fw::SwapchainInfo& swapchain) {
  m_extent = swapchain.extent;
  m_depth_images.clear();
  m_depth_images.reserve(m_frames.size());
  for (std::size_t index{}; index < m_frames.size(); ++index) {
    fw::ImageDesc desc{};
    desc.extent = m_extent;
    desc.format = m_depth_format;
    desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    m_depth_images.push_back(fw::Image::Create(*m_device, desc));
  }
  if (m_graphics_pipeline.GetHandle() == VK_NULL_HANDLE || m_color_format != swapchain.format) {
    CreateGraphicsPipeline(swapchain.format);
  }
}

void Sample::ResetCamera() {
  m_camera.ResetCamera(m_scene_bounds, static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height));
}

void Sample::OnUpdate(const fw::FrameContext& frame, const fw::InputState& input, float delta_seconds) {
  m_gpu_timer.BeginFrame(frame);
  auto& resources = m_frames.at(frame.frame_index);
  if (resources.has_stats) {
    // 帧调度器在 OnUpdate 前已等待槽位 fence，可以读取这个槽位上次提交的结果。
    LodStats stats{};
    resources.stats_readback.Read(std::as_writable_bytes(std::span{&stats, 1}));
    m_last_stats = stats;
    m_last_batched = resources.batched;
  }
  if (input.IsPressed(fw::Key::Home)) {
    ResetCamera();
  }
  m_camera.Update(input, delta_seconds);

  const auto view = m_camera.GetViewMatrix();
  const auto projection =
    m_camera.GetProjectionMatrix(static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height));
  const auto view_projection = projection * view;
  resources.camera_uniform.Write(std::as_bytes(std::span{&view_projection, 1}));

  const auto rows = glm::transpose(view_projection);
  CullingUniform culling{};
  culling.frustum_planes = {rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1],
                            rows[3] - rows[1], rows[2],           rows[3] - rows[2]};
  for (auto& plane : culling.frustum_planes) {
    plane /= glm::length(glm::vec3{plane});
  }
  // 右手相机朝视空间 -Z，看向前方的深度为正。
  culling.view_depth = -glm::transpose(view)[2];
  culling.lod_parameters = {std::abs(projection[1][1]) * static_cast<float>(frame.extent.height), m_high_threshold,
                            m_medium_threshold, 0.0F};
  culling.options = {m_enable_culling ? 1U : 0U, m_force_high ? 1U : 0U, 0U, 0U};
  culling.meshes = m_lod_meshes;
  resources.culling_uniform.Write(std::as_bytes(std::span{&culling, 1}));
}

void Sample::GenerateDrawCommands(const fw::FrameContext& frame) {
  const auto cmd = frame.command_buffer;
  const auto& resources = m_frames.at(frame.frame_index);
  const auto stats = resources.stats.GetHandle();
  vkCmdFillBuffer(cmd, stats, 0, sizeof(LodStats), 0);
  const auto stats_reset = BufferBarrier(stats, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                         VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
  fw::CmdBarrier(cmd, std::span{&stats_reset, 1}, {});

  const auto& culling_pipeline = m_batch_instances ? m_bin_instances_pipeline : m_per_object_pipeline;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, culling_pipeline.GetHandle());
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, culling_pipeline.GetLayout(), 0, 1,
                          &resources.compute_set, 0, nullptr);
  vkCmdDispatch(cmd, (m_object_count + k_compute_group_size - 1) / k_compute_group_size, 1, 1);

  if (m_batch_instances) {
    // 第二遍读取各 LOD 的实例计数，写出非空批次的命令和批次数。
    const auto bins_ready = BufferBarrier(
      stats, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    fw::CmdBarrier(cmd, std::span{&bins_ready, 1}, {});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_build_batches_pipeline.GetHandle());
    vkCmdDispatch(cmd, 1, 1, 1);
  }

  const std::array barriers{
    BufferBarrier(resources.indirect_commands.GetHandle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT),
    // 全部剔除时 drawCount 只有清零操作写入。
    BufferBarrier(stats, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT),
    BufferBarrier(resources.visible_instances.GetHandle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
                  VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT),
  };
  fw::CmdBarrier(cmd, barriers, {});
}

void Sample::Draw(const fw::FrameContext& frame) {
  const auto cmd = frame.command_buffer;
  const auto& resources = m_frames.at(frame.frame_index);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphics_pipeline.GetHandle());
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphics_pipeline.GetLayout(), 0, 1,
                          &resources.graphics_set, 0, nullptr);
  const std::array vertex_buffers{m_vertices.GetHandle(), resources.visible_instances.GetHandle()};
  const std::array<VkDeviceSize, 2> offsets{};
  vkCmdBindVertexBuffers(cmd, 0, static_cast<std::uint32_t>(vertex_buffers.size()), vertex_buffers.data(),
                         offsets.data());
  vkCmdBindIndexBuffer(cmd, m_indices.GetHandle(), 0, VK_INDEX_TYPE_UINT32);

  const auto max_draw_count = m_batch_instances ? static_cast<std::uint32_t>(m_lod_meshes.size()) : m_object_count;
  vkCmdDrawIndexedIndirectCount(cmd, resources.indirect_commands.GetHandle(), 0, resources.stats.GetHandle(), 0,
                                max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
}

void Sample::CopyStatsToReadback(const fw::FrameContext& frame) {
  const auto cmd = frame.command_buffer;
  auto& resources = m_frames.at(frame.frame_index);
  const auto stats = resources.stats.GetHandle();
  const auto readback = resources.stats_readback.GetHandle();
  const auto copy_ready = BufferBarrier(stats, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
  fw::CmdBarrier(cmd, std::span{&copy_ready, 1}, {});
  const VkBufferCopy copy{.srcOffset = 0, .dstOffset = 0, .size = sizeof(LodStats)};
  vkCmdCopyBuffer(cmd, stats, readback, 1, &copy);
  const auto host_ready = BufferBarrier(readback, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
  fw::CmdBarrier(cmd, std::span{&host_ready, 1}, {});
  resources.has_stats = true;
  resources.batched = m_batch_instances;
}

void Sample::OnRender(const fw::FrameContext& frame) {
  m_gpu_timer.WriteTimestamp(frame, k_compute_begin);
  GenerateDrawCommands(frame);
  m_gpu_timer.WriteTimestamp(frame, k_compute_end);

  const auto cmd = frame.command_buffer;
  BeginAttachments(frame, m_depth_images.at(frame.frame_index));
  const VkViewport viewport{0.0F, 0.0F, static_cast<float>(frame.extent.width), static_cast<float>(frame.extent.height),
                            0.0F, 1.0F};
  const VkRect2D scissor{.offset = {0, 0}, .extent = frame.extent};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);
  Draw(frame);
  vkCmdEndRendering(cmd);
  m_gpu_timer.WriteTimestamp(frame, k_scene_end);

  // 统计只供界面使用，回读复制在绘制和 GPU 计时之后录制。
  CopyStatsToReadback(frame);
}

void Sample::OnGui() {
  ImGui::SetNextWindowPos(ImVec2{16.0F, 16.0F}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("GPU Instance Culling", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Checkbox("Batch instances by LOD", &m_batch_instances);
    ImGui::Checkbox("Force highest LOD", &m_force_high);
    ImGui::Checkbox("Frustum culling", &m_enable_culling);
    ImGui::SliderFloat("High LOD diameter (px)", &m_high_threshold, m_medium_threshold + 1.0F, 256.0F, "%.0f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Medium LOD diameter (px)", &m_medium_threshold, 2.0F, m_high_threshold - 1.0F, "%.0f",
                       ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::Button("Reset camera")) {
      ResetCamera();
    }
    ImGui::TextUnformatted("Wheel: zoom | LMB: orbit | RMB: pan | WASD/QE: move");

    ImGui::Separator();
    if (m_last_stats) {
      const auto& stats = *m_last_stats;
      ImGui::TextUnformatted(m_last_batched ? "Completed frame: batched draws" : "Completed frame: per-object draws");
      ImGui::TextUnformatted(std::format("GPU draw commands: {}", stats.draw_count).c_str());
      const std::array labels{"LOD 0 (High)", "LOD 1 (Medium)", "LOD 2 (Low)"};
      const std::array colors{ImVec4{0.24F, 0.78F, 1.0F, 1.0F}, ImVec4{0.33F, 0.95F, 0.52F, 1.0F},
                              ImVec4{1.0F, 0.62F, 0.22F, 1.0F}};
      std::uint32_t visible_count{};
      std::uint64_t triangles{};
      for (std::size_t lod{}; lod < labels.size(); ++lod) {
        const auto lod_triangles = static_cast<std::uint64_t>(stats.lod_counts[lod]) * (m_lod_meshes[lod].x / 3U);
        visible_count += stats.lod_counts[lod];
        triangles += lod_triangles;
        ImGui::PushStyleColor(ImGuiCol_Text, colors[lod]);
        ImGui::TextUnformatted(labels[lod]);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(std::format("{} objects", stats.lod_counts[lod]).c_str());
      }
      ImGui::TextUnformatted(std::format("Visible instances: {} / {}", visible_count, m_object_count).c_str());
      ImGui::TextUnformatted(std::format("Submitted triangles: {}", triangles).c_str());
    }
    if (const auto compute_ms = m_gpu_timer.GetElapsedMilliseconds(k_compute_begin, k_compute_end)) {
      ImGui::TextUnformatted(std::format("GPU command generation: {:.3f} ms", *compute_ms).c_str());
    }
    if (const auto scene_ms = m_gpu_timer.GetElapsedMilliseconds(k_compute_end, k_scene_end)) {
      ImGui::TextUnformatted(std::format("GPU scene: {:.3f} ms", *scene_ms).c_str());
    }
  }
  ImGui::End();
}
}  // namespace demo
