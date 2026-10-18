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
#include <glm/vec4.hpp>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <span>
#include <sstream>
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
};
static_assert(sizeof(CameraUniform) == 64, "CameraUniform must match the GLSL std140 block");

struct alignas(16) ObjectState {
  glm::vec4 offset_and_visible{};
  glm::vec4 color{1.0F};
};
static_assert(sizeof(ObjectState) == 32, "ObjectState must match the GLSL std430 array stride");
static_assert(offsetof(ObjectState, color) == 16, "ObjectState color must match GLSL");

struct SimulationParameters {
  float time{};
  float displacement{};
  float visible_fraction{};
  std::uint32_t draw_count{};
};
static_assert(sizeof(SimulationParameters) == 16, "SimulationParameters must match the GLSL push constant block");
static_assert(offsetof(SimulationParameters, draw_count) == 12, "SimulationParameters draw_count must match GLSL");
static_assert(sizeof(VkDrawIndexedIndirectCommand) == 20, "Indirect command must match the GLSL std430 struct");
// 与两个 Compute Shader 的 local_size_x 保持一致。
constexpr std::uint32_t k_compute_group_size{64};
constexpr float k_max_displacement{0.75F};
constexpr std::uint32_t k_state_begin{0};
constexpr std::uint32_t k_state_end{1};
constexpr std::uint32_t k_commands_end{2};
constexpr std::uint32_t k_draw_end{3};
constexpr std::uint32_t k_timestamp_count{4};

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
            << "Space: pause/resume rotation | Home: reset camera | R: reset all | Esc: close\n";
}

Drawable Sample::CreateDrawable(const fw::Mesh& mesh) {
  Drawable drawable{.index_count = static_cast<std::uint32_t>(mesh.indices.size())};

  const auto vertex_bytes = std::as_bytes(std::span{mesh.vertices});
  const auto index_bytes = std::as_bytes(std::span{mesh.indices});
  auto vertex_staging = fw::Buffer::Create(
    *m_device, {vertex_bytes.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, fw::MemoryUsage::Upload});
  auto index_staging = fw::Buffer::Create(
    *m_device, {index_bytes.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, fw::MemoryUsage::Upload});
  vertex_staging.Write(vertex_bytes);
  index_staging.Write(index_bytes);
  drawable.vertex_buffer = fw::Buffer::Create(
    *m_device, {vertex_bytes.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT});
  drawable.index_buffer = fw::Buffer::Create(
    *m_device, {index_bytes.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT});
  // 两次复制合并为一次同步上传，返回后局部暂存缓冲才可安全销毁。
  m_device->SubmitImmediate([&](VkCommandBuffer command_buffer) {
    const VkBufferCopy vertex_copy{.srcOffset = 0, .dstOffset = 0, .size = vertex_bytes.size_bytes()};
    const VkBufferCopy index_copy{.srcOffset = 0, .dstOffset = 0, .size = index_bytes.size_bytes()};
    vkCmdCopyBuffer(command_buffer, vertex_staging.GetHandle(), drawable.vertex_buffer.GetHandle(), 1, &vertex_copy);
    vkCmdCopyBuffer(command_buffer, index_staging.GetHandle(), drawable.index_buffer.GetHandle(), 1, &index_copy);
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
    vertex_barrier.buffer = drawable.vertex_buffer.GetHandle();
    vertex_barrier.size = VK_WHOLE_SIZE;
    barriers.back() = vertex_barrier;
    barriers.back().dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    barriers.back().dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT;
    barriers.back().buffer = drawable.index_buffer.GetHandle();
    fw::CmdBarrier(command_buffer, barriers, {});
  });

  return drawable;
}

void Sample::CreateGeometry() {
  constexpr std::size_t size = 10;
  constexpr float start = (size - 1) * -1.F;
  for (std::size_t x = 0; x < size; x++) {
    for (std::size_t y = 0; y < size; y++) {
      for (std::size_t z = 0; z < size; z++) {
        const auto center_x = start + static_cast<float>(x) * 2.F;
        const auto center_y = start + static_cast<float>(y) * 2.F;
        const auto center_z = start + static_cast<float>(z) * 2.F;
        const auto mesh = fw::Shapes::Sphere(0.5F, 64, 64, glm::vec3{center_x, center_y, center_z});
        // 预留 GPU 动画的最大位移，使相机的自动近远裁剪覆盖完整动画范围。
        const glm::vec3 margin{0.0F, k_max_displacement, 0.0F};
        m_scene_bounds.Merge(fw::AABB{mesh.bounds.GetMin() - margin, mesh.bounds.GetMax() + margin});
        m_drawables.emplace_back(CreateDrawable(mesh));
      }
    }
  }
}

void Sample::CreateComputeResources() {
  // CPU 只提供几何元数据；完整的 VkDrawIndexedIndirectCommand 由 Compute 写入。
  std::vector<std::uint32_t> index_counts{};
  index_counts.reserve(m_drawables.size());
  for (const auto& drawable : m_drawables) {
    index_counts.push_back(drawable.index_count);
  }
  const auto index_count_bytes = std::as_bytes(std::span{index_counts});
  m_draw_index_counts = fw::Buffer::Create(
    *m_device, {index_count_bytes.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, fw::MemoryUsage::Upload});
  // Write 刷新映射内存，首次队列提交使此前的主机写入对 Compute 可见；之后不再修改。
  m_draw_index_counts.Write(index_count_bytes);

  const std::array<VkDescriptorSetLayoutBinding, 3> bindings{{
    {.binding = 0,
     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
     .descriptorCount = 1,
     .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
     .pImmutableSamplers = nullptr},
    {.binding = 1,
     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
     .descriptorCount = 1,
     .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
     .pImmutableSamplers = nullptr},
    {.binding = 2,
     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
     .descriptorCount = 1,
     .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
     .pImmutableSamplers = nullptr},
  }};
  m_compute_set_layout = fw::DescriptorSetLayout::Create(*m_device, bindings);
  const auto frame_count = m_frames->GetFrameCount();
  const std::array<VkDescriptorPoolSize, 1> sizes{{
    {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = frame_count * 3},
  }};
  m_compute_descriptor_pool = fw::DescriptorPool::Create(*m_device, {frame_count, sizes});
  const VkDeviceSize command_bytes = m_drawables.size() * sizeof(VkDrawIndexedIndirectCommand);
  const VkDeviceSize state_bytes = m_drawables.size() * sizeof(ObjectState);
  const VkDescriptorBufferInfo input_info{
    .buffer = m_draw_index_counts.GetHandle(),
    .offset = 0,
    .range = index_count_bytes.size_bytes(),
  };
  m_indirect_buffers.reserve(frame_count);
  m_object_states.reserve(frame_count);
  m_compute_sets.reserve(frame_count);
  for (std::uint32_t index{}; index < frame_count; ++index) {
    // 每个飞行帧独占状态和间接命令，槽位复用由框架的帧 fence 保护。
    m_indirect_buffers.push_back(fw::Buffer::Create(
      *m_device, {command_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT}));
    m_object_states.push_back(fw::Buffer::Create(*m_device, {state_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT}));
    const auto set = m_compute_descriptor_pool.Allocate(m_compute_set_layout.GetHandle());
    m_compute_sets.push_back(set);
    const VkDescriptorBufferInfo output_info{
      .buffer = m_indirect_buffers.back().GetHandle(),
      .offset = 0,
      .range = command_bytes,
    };
    const VkDescriptorBufferInfo state_info{
      .buffer = m_object_states.back().GetHandle(),
      .offset = 0,
      .range = state_bytes,
    };
    const std::array<VkWriteDescriptorSet, 3> writes{{
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = nullptr,
       .dstSet = set,
       .dstBinding = 0,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .pImageInfo = nullptr,
       .pBufferInfo = &input_info,
       .pTexelBufferView = nullptr},
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = nullptr,
       .dstSet = set,
       .dstBinding = 1,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .pImageInfo = nullptr,
       .pBufferInfo = &output_info,
       .pTexelBufferView = nullptr},
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .pNext = nullptr,
       .dstSet = set,
       .dstBinding = 2,
       .dstArrayElement = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .pImageInfo = nullptr,
       .pBufferInfo = &state_info,
       .pTexelBufferView = nullptr},
    }};
    vkUpdateDescriptorSets(m_device->GetHandle(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  auto shader = fw::ShaderModule::Load(
    *m_device, std::filesystem::path{PROJECT_SHADER_DIR} / "gpu_driven_rendering/synchronization_commands_comp.spv");
  const auto set_layout = m_compute_set_layout.GetHandle();
  const VkPushConstantRange simulation_range{
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    .offset = 0,
    .size = sizeof(SimulationParameters),
  };
  fw::ComputePipelineDesc desc{};
  desc.shader = shader.GetHandle();
  desc.layout.set_layouts = std::span{&set_layout, 1};
  desc.layout.push_constants = std::span{&simulation_range, 1};
  m_compute_pipeline = fw::Pipeline::CreateCompute(*m_device, desc);
  auto state_shader = fw::ShaderModule::Load(
    *m_device, std::filesystem::path{PROJECT_SHADER_DIR} / "gpu_driven_rendering/synchronization_state_comp.spv");
  desc.shader = state_shader.GetHandle();
  m_state_pipeline = fw::Pipeline::CreateCompute(*m_device, desc);
}

void Sample::GenerateObjectStates(const fw::FrameContext& frame) {
  const auto cmd = frame.command_buffer;
  const auto set = m_compute_sets.at(frame.frame_index);
  const SimulationParameters parameters{
    .time = m_simulation_time,
    .displacement = m_displacement,
    .visible_fraction = m_visible_fraction,
    .draw_count = static_cast<std::uint32_t>(m_drawables.size()),
  };
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_state_pipeline.GetHandle());
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_state_pipeline.GetLayout(), 0, 1, &set, 0, nullptr);
  vkCmdPushConstants(cmd, m_state_pipeline.GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters),
                     &parameters);
  vkCmdDispatch(cmd, (parameters.draw_count + k_compute_group_size - 1) / k_compute_group_size, 1, 1);

  // 依赖 1：Compute A 写入 ObjectState，Compute B 随后读取其绘制开关。
  const VkBufferMemoryBarrier2 barrier{
    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .buffer = m_object_states.at(frame.frame_index).GetHandle(),
    .offset = 0,
    .size = VK_WHOLE_SIZE,
  };
  fw::CmdBarrier(cmd, std::span{&barrier, 1}, {});
}

void Sample::GenerateDrawCommands(const fw::FrameContext& frame) {
  const auto cmd = frame.command_buffer;
  const auto set = m_compute_sets.at(frame.frame_index);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute_pipeline.GetHandle());
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute_pipeline.GetLayout(), 0, 1, &set, 0, nullptr);
  const auto draw_count = static_cast<std::uint32_t>(m_drawables.size());
  vkCmdDispatch(cmd, (draw_count + k_compute_group_size - 1) / k_compute_group_size, 1, 1);
  // 依赖 2：Compute B 写入的命令在 DRAW_INDIRECT 阶段作为间接参数读取。
  const VkBufferMemoryBarrier2 indirect_barrier{
    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
    .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .buffer = m_indirect_buffers.at(frame.frame_index).GetHandle(),
    .offset = 0,
    .size = VK_WHOLE_SIZE,
  };
  // 依赖 3/4：Compute A 写入的位移和颜色还需分别对顶点、片元着色器的 SSBO 读取可见。
  // 前一道 Compute -> Compute 屏障只覆盖 Compute B 的读取；此处单独发布到图形着色器。
  auto state_barrier = indirect_barrier;
  state_barrier.buffer = m_object_states.at(frame.frame_index).GetHandle();
  state_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
  state_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
  const std::array barriers{indirect_barrier, state_barrier};
  fw::CmdBarrier(cmd, barriers, {});
}

void Sample::Draw(VkCommandBuffer cmd, std::uint32_t frame_index) {
  const std::array sets{m_sets.at(frame_index), m_compute_sets.at(frame_index)};
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetLayout(), 0,
                          static_cast<std::uint32_t>(sets.size()), sets.data(), 0, nullptr);
  const auto indirect_buffer = m_indirect_buffers.at(frame_index).GetHandle();
  VkDeviceSize indirect_offset{};
  std::uint32_t object_index{};
  for (const auto& drawable : m_drawables) {
    const auto vertex_buffer = drawable.vertex_buffer.GetHandle();
    const VkDeviceSize offset{};
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
    vkCmdBindIndexBuffer(cmd, drawable.index_buffer.GetHandle(), 0, VK_INDEX_TYPE_UINT32);
    // 对象 ID 由 CPU 提供，位移、颜色和是否绘制均读取 GPU 本帧生成的状态。
    vkCmdPushConstants(cmd, m_pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(object_index), &object_index);
    vkCmdDrawIndexedIndirect(cmd, indirect_buffer, indirect_offset, 1, sizeof(VkDrawIndexedIndirectCommand));
    indirect_offset += sizeof(VkDrawIndexedIndirectCommand);
    ++object_index;
  }
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
  auto vertex = fw::ShaderModule::Load(*m_device, shader_directory / "gpu_driven_rendering/synchronization_vert.spv");
  auto fragment = fw::ShaderModule::Load(*m_device, shader_directory / "gpu_driven_rendering/synchronization_frag.spv");
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
  const std::array set_layouts{m_set_layout.GetHandle(), m_compute_set_layout.GetHandle()};
  const VkPushConstantRange object_range{
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    .offset = 0,
    .size = sizeof(std::uint32_t),
  };
  fw::GraphicsPipelineDesc desc{};
  desc.vertex_shader = vertex.GetHandle();
  desc.fragment_shader = fragment.GetHandle();
  desc.layout.set_layouts = set_layouts;
  desc.layout.push_constants = std::span{&object_range, 1};
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
  }
}

void Sample::ResetAll() noexcept {
  m_camera.ResetCamera(m_scene_bounds, static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height));
  m_rotation = 0.0F;
  m_rotation_speed = 0.5F;
  m_enable_rotation = true;
  m_enable_animation = true;
  m_simulation_time = 0.0F;
  m_displacement = 0.35F;
  m_visible_fraction = 1.0F;
  m_clear_color = {0.025F, 0.035F, 0.06F, 1.0F};
}

void Sample::OnUpdate(const fw::FrameContext& frame, const fw::InputState& input, float delta_seconds) {
  m_gpu_timer.BeginFrame(frame);
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
  if (m_enable_animation) {
    m_simulation_time = std::remainder(m_simulation_time + delta_seconds, 6.283185307F);
  }
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
  m_gpu_timer.WriteTimestamp(frame, k_state_begin);
  GenerateObjectStates(frame);
  m_gpu_timer.WriteTimestamp(frame, k_state_end);
  // Dispatch 与 Compute -> Indirect 屏障都位于动态渲染作用域之外。
  GenerateDrawCommands(frame);
  m_gpu_timer.WriteTimestamp(frame, k_commands_end);
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
  m_gpu_timer.WriteTimestamp(frame, k_draw_end);
}

void Sample::OnGui() {
  ImGui::SetNextWindowPos(ImVec2{16.0F, 16.0F}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Synchronization", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(std::data(m_device->GetInfo().properties.deviceName));
    ImGui::TextUnformatted(
      std::format("GPU-generated commands: {} | Draw calls: {}", m_drawables.size(), m_drawables.size()).c_str());
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
      show_timing("GPU State", k_state_begin, k_state_end);
      show_timing("GPU Commands", k_state_end, k_commands_end);
      show_timing("GPU Draw", k_commands_end, k_draw_end);
    } else {
      ImGui::TextUnformatted("GPU timestamps unavailable on this queue");
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Compute A writes object state");
    ImGui::TextUnformatted("Compute B reads state, writes indirect commands");
    ImGui::TextUnformatted("Vertex reads offsets | Fragment reads colors");
    ImGui::Separator();
    ImGui::Checkbox("Animate GPU state", &m_enable_animation);
    ImGui::SliderFloat("Displacement", &m_displacement, 0.0F, k_max_displacement);
    ImGui::SliderFloat("Active fraction", &m_visible_fraction, 0.0F, 1.0F, "%.2f");
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
    ImGui::TextUnformatted("WASD/QE: move | Space: pause rotation");
    ImGui::TextUnformatted("Home: camera | R: reset | Esc: exit");
  }
  // 无论 Begin 返回值如何，ImGui 窗口作用域都必须与 End 配对。
  ImGui::End();
}
}  // namespace demo
