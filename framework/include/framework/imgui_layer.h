#pragma once

#include <vulkan/vulkan.h>

#include <memory>

#include "framework/frame_context.h"
#include "framework/input_state.h"
#include "framework/swapchain.h"

struct ImGuiContext;

namespace fw {
class Device;
class Window;

/// @brief 在主线程管理 ImGui 上下文以及 GLFW/Vulkan 后端。
/// @details Device 和 Window 必须比本层存活更久，更新交换链配置或销毁本层前由调用者等待 GPU 空闲。
class ImGuiLayer final {
public:
  ~ImGuiLayer() noexcept;
  ImGuiLayer(const ImGuiLayer&) = delete;
  ImGuiLayer& operator=(const ImGuiLayer&) = delete;
  ImGuiLayer(ImGuiLayer&&) = delete;
  ImGuiLayer& operator=(ImGuiLayer&&) = delete;

  [[nodiscard]] static std::unique_ptr<ImGuiLayer> Create(Device& device, Window& window,
                                                          const SwapchainInfo& swapchain);
  void BeginFrame();
  /// @brief 完成 CPU 端 UI 数据构建，不提交 GPU 命令；绘制命令由 Render 录制。
  void EndFrame();
  /// @brief 加载场景颜色并叠加 UI，图像保持 COLOR_ATTACHMENT_OPTIMAL 布局。
  void Render(const FrameContext& frame);
  void OnSwapchainChanged(const SwapchainInfo& swapchain);
  /// @brief 返回按 UI 捕获标志过滤后的输入副本，避免操作面板时同时驱动场景。
  [[nodiscard]] InputState FilterInput(InputState input) const;

private:
  ImGuiLayer(Device& device, Window& window);
  void Initialize(const SwapchainInfo& swapchain);
  void InitializeRenderer();
  void ShutdownRenderer() noexcept;
  static void OnVulkanResult(VkResult result) noexcept;

  Device& m_device;
  Window& m_window;
  ImGuiContext* m_context{};
  SwapchainInfo m_swapchain{};
  bool m_has_glfw_backend{false};
  bool m_has_vulkan_backend{false};
  bool m_is_frame_open{false};
  bool m_has_draw_data{false};
};
}  // namespace fw
