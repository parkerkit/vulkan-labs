#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "framework/input_state.h"

struct GLFWwindow;

namespace fw {
struct WindowDesc {
  std::string title{"Vulkan Lab"};
  uint32_t width{1280};
  uint32_t height{720};
};

/// @brief 仅在主线程使用的 GLFW 窗口，Vulkan 窗口表面必须先于窗口销毁。
class Window final {
public:
  ~Window() noexcept;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;

  [[nodiscard]] static std::unique_ptr<Window> Create(const WindowDesc& desc);
  /// @brief 处理事件并更新输入快照，位移、滚轮和新按下状态在每次采样时重新计算。
  void PollEvents();
  void WaitEvents() const;
  void RequestClose() noexcept;
  void SetTitle(std::string_view title);
  [[nodiscard]] bool ShouldClose() const noexcept;
  /// @brief 返回帧缓冲像素尺寸，可能因缩放比例与窗口逻辑尺寸不同，最小化时可为零。
  [[nodiscard]] VkExtent2D GetFramebufferExtent() const noexcept;
  [[nodiscard]] const InputState& GetInputState() const noexcept;
  /// @brief 向平台后端提供借用的 GLFW 句柄，所有权仍归 Window。
  [[nodiscard]] GLFWwindow* GetNativeHandle() const noexcept;
  /// @brief 扩展名指针归 GLFW 所有，在最后一个窗口销毁前有效。
  [[nodiscard]] std::vector<const char*> GetInstanceExtensions() const;
  /// @brief 创建由调用者负责销毁的窗口表面，其寿命不得超过 instance 和当前窗口。
  [[nodiscard]] VkSurfaceKHR CreateSurface(VkInstance instance) const;

private:
  Window() = default;
  static void ScrollCallback(GLFWwindow* window, double offset_x, double offset_y) noexcept;

  GLFWwindow* m_window{};
  InputState m_input{};
  double m_pending_scroll{};
  double m_cursor_x{};
  double m_cursor_y{};
  bool m_has_cursor_position{false};
  bool m_has_glfw_reference{false};
};
}  // namespace fw
