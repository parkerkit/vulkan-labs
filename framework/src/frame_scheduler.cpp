#include "framework/frame_scheduler.h"

#include <limits>
#include <span>
#include <utility>

#include "framework/device.h"
#include "framework/synchronization.h"
#include "framework/window.h"
#include "vulkan_helpers.h"

namespace fw {
namespace {
enum class BackbufferPhase { Rendering, Present };

void TransitionBackbuffer(VkCommandBuffer command_buffer, VkImage image, BackbufferPhase phase) {
  const bool is_presenting = phase == BackbufferPhase::Present;
  // 每帧重新生成颜色内容，进入渲染时可从 UNDEFINED 开始；输出后再转为呈现布局。
  VkImageMemoryBarrier2 barrier{
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = is_presenting ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_2_NONE,
    .srcAccessMask = is_presenting ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_NONE,
    .dstStageMask = is_presenting ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstAccessMask =
      is_presenting ? VK_ACCESS_2_NONE : VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    .oldLayout = is_presenting ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
    .newLayout = is_presenting ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = image,
    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
  };
  CmdBarrier(command_buffer, {}, std::span{&barrier, 1});
}
}  // namespace

FrameScheduler::FrameScheduler(Device& device, Window& window, const FrameSchedulerDesc& desc)
    : m_device{device}, m_window{window}, m_desc{desc} {}

std::unique_ptr<FrameScheduler> FrameScheduler::Create(Device& device, Window& window, const FrameSchedulerDesc& desc) {
  detail::Require(desc.frames_in_flight > 0 && desc.frames_in_flight <= 8 && desc.acquire_wait_stage != 0,
                  "Use 1-8 frame slots and a nonzero acquire wait stage");
  auto scheduler = std::unique_ptr<FrameScheduler>{new FrameScheduler{device, window, desc}};
  scheduler->Initialize();
  return scheduler;
}
void FrameScheduler::Initialize() {
  m_frames.resize(m_desc.frames_in_flight);
  for (auto& frame : m_frames) {
    VkCommandPoolCreateInfo pool{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = m_device.GetInfo().graphics_family,
    };
    detail::Check(vkCreateCommandPool(m_device.GetHandle(), &pool, nullptr, &frame.pool), "Create frame command pool");
    VkCommandBufferAllocateInfo allocate{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = frame.pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
    };
    detail::Check(vkAllocateCommandBuffers(m_device.GetHandle(), &allocate, &frame.command_buffer),
                  "Allocate frame command buffer");
    VkSemaphoreCreateInfo semaphore{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = nullptr,
      .flags = {},
    };
    detail::Check(vkCreateSemaphore(m_device.GetHandle(), &semaphore, nullptr, &frame.image_available),
                  "Create acquire semaphore");
    VkFenceCreateInfo fence{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = nullptr,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    detail::Check(vkCreateFence(m_device.GetHandle(), &fence, nullptr, &frame.fence), "Create frame fence");
  }
  RecreateSwapchain();
}

void FrameScheduler::RecreateSwapchain() {
  m_device.WaitIdle();
  m_window_extent = m_window.GetFramebufferExtent();
  auto replacement = Swapchain::Create(m_device, {m_window_extent, m_desc.enable_vsync},
                                       m_swapchain ? m_swapchain->GetHandle() : VK_NULL_HANDLE);
  // 已等待设备空闲，可一并回收旧交换链的同步对象和延迟释放资源。
  DestroyPresentSemaphores();
  m_swapchain = std::move(replacement);
  m_present_semaphores.resize(m_swapchain->GetInfo().image_count, VK_NULL_HANDLE);
  m_image_fences.assign(m_swapchain->GetInfo().image_count, VK_NULL_HANDLE);
  VkSemaphoreCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
  };
  for (auto& semaphore : m_present_semaphores) {
    detail::Check(vkCreateSemaphore(m_device.GetHandle(), &info, nullptr, &semaphore),
                  "Create image-indexed present semaphore");
  }
  for (auto& frame : m_frames) {
    frame.retired_resources.clear();
  }
  ++m_generation;
  m_needs_recreate = false;
}

std::optional<FrameContext> FrameScheduler::BeginFrame() {
  try {
    detail::Require(!m_has_failed, "Frame scheduler cannot continue after a failed frame");
    detail::Require(!m_is_frame_active, "EndFrame must follow a successful BeginFrame");
    const auto extent = m_window.GetFramebufferExtent();
    if (extent.width == 0 || extent.height == 0) {
      return std::nullopt;
    }
    if (m_needs_recreate || extent.width != m_window_extent.width || extent.height != m_window_extent.height) {
      RecreateSwapchain();
      return std::nullopt;
    }
    auto& frame = m_frames.at(m_frame_index);
    detail::Check(vkWaitForFences(m_device.GetHandle(), 1, &frame.fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
                  "Wait for frame slot");
    // 该槽位上次提交已完成，此时释放其旧资源并复用命令池与逐帧数据才安全。
    frame.retired_resources.clear();
    const auto acquired =
      vkAcquireNextImageKHR(m_device.GetHandle(), m_swapchain->GetHandle(), std::numeric_limits<uint64_t>::max(),
                            frame.image_available, VK_NULL_HANDLE, &m_image_index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
      RecreateSwapchain();
      return std::nullopt;
    }
    if (acquired == VK_SUBOPTIMAL_KHR) {
      // 本次获取仍有效，须先提交以消费获取信号量，再在下一轮重建交换链。
      m_needs_recreate = true;
    } else {
      detail::Check(acquired, "Acquire swapchain image");
    }
    const auto previous_use = m_image_fences.at(m_image_index);
    // 取得图像的顺序与飞行帧轮转独立，还需等待此前使用该图像的提交。
    if (previous_use != VK_NULL_HANDLE) {
      detail::Check(
        vkWaitForFences(m_device.GetHandle(), 1, &previous_use, VK_TRUE, std::numeric_limits<uint64_t>::max()),
        "Wait for swapchain image");
    }
    detail::Check(vkResetCommandPool(m_device.GetHandle(), frame.pool, 0), "Reset frame command pool");
    VkCommandBufferBeginInfo begin{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      .pInheritanceInfo = nullptr,
    };
    detail::Check(vkBeginCommandBuffer(frame.command_buffer, &begin), "Begin frame command buffer");
    TransitionBackbuffer(frame.command_buffer, m_swapchain->GetImage(m_image_index), BackbufferPhase::Rendering);
    m_is_frame_active = true;
    const auto& info = m_swapchain->GetInfo();
    return FrameContext{
      .frame_index = m_frame_index,
      .image_index = m_image_index,
      .command_buffer = frame.command_buffer,
      .color_image = m_swapchain->GetImage(m_image_index),
      .color_view = m_swapchain->GetImageView(m_image_index),
      .extent = info.extent,
      .color_format = info.format,
    };
  } catch (...) {
    m_has_failed = true;
    throw;
  }
}

void FrameScheduler::EndFrame() {
  try {
    detail::Require(!m_has_failed, "Frame scheduler cannot continue after a failed frame");
    detail::Require(m_is_frame_active, "No active frame to submit");
    auto& frame = m_frames.at(m_frame_index);
    TransitionBackbuffer(frame.command_buffer, m_swapchain->GetImage(m_image_index), BackbufferPhase::Present);
    detail::Check(vkEndCommandBuffer(frame.command_buffer), "End frame command buffer");
    VkSemaphoreSubmitInfo wait{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .pNext = nullptr,
      .semaphore = frame.image_available,
      .value = {},
      .stageMask = m_desc.acquire_wait_stage,
      .deviceIndex = {},
    };
    VkSemaphoreSubmitInfo signal{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .pNext = nullptr,
      .semaphore = m_present_semaphores.at(m_image_index),
      .value = {},
      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .deviceIndex = {},
    };
    VkCommandBufferSubmitInfo command{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .pNext = nullptr,
      .commandBuffer = frame.command_buffer,
      .deviceMask = {},
    };
    VkSubmitInfo2 submit{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .pNext = nullptr,
      .flags = {},
      .waitSemaphoreInfoCount = 1,
      .pWaitSemaphoreInfos = &wait,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &command,
      .signalSemaphoreInfoCount = 1,
      .pSignalSemaphoreInfos = &signal,
    };
    // 确认即将提交时才重置 fence，避免获取图像因交换链过期提前返回后，下一轮永久等待。
    detail::Check(vkResetFences(m_device.GetHandle(), 1, &frame.fence), "Reset frame fence");
    detail::Check(vkQueueSubmit2(m_device.GetGraphicsQueue(), 1, &submit, frame.fence), "Submit frame");
    m_image_fences.at(m_image_index) = frame.fence;
    m_is_frame_active = false;
    const auto swapchain = m_swapchain->GetHandle();
    VkPresentInfoKHR present{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .pNext = nullptr,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &signal.semaphore,
      .swapchainCount = 1,
      .pSwapchains = &swapchain,
      .pImageIndices = &m_image_index,
      .pResults = nullptr,
    };
    const auto presented = vkQueuePresentKHR(m_device.GetPresentQueue(), &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
      m_needs_recreate = true;
    } else {
      detail::Check(presented, "Present frame");
    }
    m_frame_index = (m_frame_index + 1) % m_desc.frames_in_flight;
  } catch (...) {
    m_has_failed = true;
    throw;
  }
}

void FrameScheduler::Retire(std::shared_ptr<void> resource) {
  detail::Require(m_is_frame_active && resource != nullptr, "Retire requires an active frame and a resource");
  m_frames.at(m_frame_index).retired_resources.push_back(std::move(resource));
}
FrameScheduler::~FrameScheduler() noexcept {
  vkDeviceWaitIdle(m_device.GetHandle());
  for (auto& frame : m_frames) {
    frame.retired_resources.clear();
    vkDestroyFence(m_device.GetHandle(), frame.fence, nullptr);
    vkDestroySemaphore(m_device.GetHandle(), frame.image_available, nullptr);
    vkDestroyCommandPool(m_device.GetHandle(), frame.pool, nullptr);
  }
  DestroyPresentSemaphores();
}
void FrameScheduler::DestroyPresentSemaphores() noexcept {
  for (VkSemaphore semaphore : m_present_semaphores) {
    vkDestroySemaphore(m_device.GetHandle(), semaphore, nullptr);
  }
  m_present_semaphores.clear();
}
const SwapchainInfo& FrameScheduler::GetSwapchainInfo() const noexcept { return m_swapchain->GetInfo(); }
uint64_t FrameScheduler::GetSwapchainGeneration() const noexcept { return m_generation; }
uint32_t FrameScheduler::GetFrameCount() const noexcept { return m_desc.frames_in_flight; }
}  // namespace fw
