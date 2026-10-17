#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "framework/frame_context.h"
#include "framework/swapchain.h"

namespace fw {
class Window;
class Device;

struct FrameSchedulerDesc {
  uint32_t frames_in_flight{2};
  bool enable_vsync{true};
  VkPipelineStageFlags2 acquire_wait_stage{
    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};  ///< 必须覆盖首次访问交换链图像的阶段。
};

/// @brief 在主线程管理帧槽位、提交及呈现，渲染附件和管线由示例持有。
/// @details 帧操作失败后须停止渲染并销毁调度器，不支持从提交失败中恢复。
class FrameScheduler final {
public:
  ~FrameScheduler() noexcept;
  FrameScheduler(const FrameScheduler&) = delete;
  FrameScheduler& operator=(const FrameScheduler&) = delete;
  FrameScheduler(FrameScheduler&&) = delete;
  FrameScheduler& operator=(FrameScheduler&&) = delete;

  [[nodiscard]] static std::unique_ptr<FrameScheduler> Create(Device& device, Window& window,
                                                              const FrameSchedulerDesc& desc);
  /// @brief 返回空值表示暂停输出或刚完成交换链重建，应在下一次循环重试。
  [[nodiscard]] std::optional<FrameContext> BeginFrame();
  /// @brief 提交并呈现当前有效帧，必须与一次成功返回上下文的 BeginFrame 配对。
  void EndFrame();
  /// @brief 在有效帧内登记待释放资源，将其寿命延长至当前帧的 fence 完成。
  /// @details 资源的最后一次 GPU 使用必须由该 fence 覆盖，后续提交不得再次引用该资源。
  /// 共享所有权仅用于延迟释放，资源包装本身仍仅允许移动。
  void Retire(std::shared_ptr<void> resource);
  [[nodiscard]] const SwapchainInfo& GetSwapchainInfo() const noexcept;
  /// @brief 交换链每次重建时递增，调用方据此更新尺寸或格式相关资源。
  [[nodiscard]] uint64_t GetSwapchainGeneration() const noexcept;
  [[nodiscard]] uint32_t GetFrameCount() const noexcept;

private:
  struct FrameSlotState {
    VkCommandPool pool{};
    VkCommandBuffer command_buffer{};
    VkSemaphore image_available{};
    VkFence fence{};
    std::vector<std::shared_ptr<void>> retired_resources{};
  };

  FrameScheduler(Device& device, Window& window, const FrameSchedulerDesc& desc);
  void Initialize();
  void RecreateSwapchain();
  void DestroyPresentSemaphores() noexcept;

  Device& m_device;
  Window& m_window;
  FrameSchedulerDesc m_desc{};
  std::unique_ptr<Swapchain> m_swapchain{};
  std::vector<FrameSlotState> m_frames{};
  // 呈现等待的消费时机由呈现操作决定，按交换链图像复用信号量，不能仅依据帧 fence 复用。
  std::vector<VkSemaphore> m_present_semaphores{};
  std::vector<VkFence> m_image_fences{};
  VkExtent2D m_window_extent{};
  uint64_t m_generation{};
  uint32_t m_frame_index{};
  uint32_t m_image_index{};
  bool m_is_frame_active{false};
  bool m_needs_recreate{false};
  bool m_has_failed{false};
};
}  // namespace fw
