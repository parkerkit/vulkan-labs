#include "framework/window.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

namespace fw {
namespace {
std::size_t g_window_count{};
constexpr std::array k_glfw_keys{GLFW_KEY_ESCAPE, GLFW_KEY_SPACE, GLFW_KEY_HOME, GLFW_KEY_R, GLFW_KEY_W,
                                 GLFW_KEY_A,      GLFW_KEY_S,     GLFW_KEY_D,    GLFW_KEY_Q, GLFW_KEY_E};

std::string GetGlfwError() {
  const char* description{};
  glfwGetError(&description);
  return description != nullptr ? std::string{description} : std::string{"Unknown GLFW error"};
}
}  // namespace

std::unique_ptr<Window> Window::Create(const WindowDesc& desc) {
  if (desc.width == 0 || desc.height == 0 || desc.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      desc.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("Invalid window dimensions");
  }
  auto window = std::unique_ptr<Window>{new Window{}};
  if (g_window_count == 0 && glfwInit() != GLFW_TRUE) {
    throw std::runtime_error(GetGlfwError());
  }
  ++g_window_count;
  window->m_has_glfw_reference = true;
  if (glfwVulkanSupported() != GLFW_TRUE) {
    throw std::runtime_error("GLFW cannot find a Vulkan loader");
  }
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window->m_window =
    glfwCreateWindow(static_cast<int>(desc.width), static_cast<int>(desc.height), desc.title.c_str(), nullptr, nullptr);
  if (window->m_window == nullptr) {
    throw std::runtime_error(GetGlfwError());
  }
  glfwSetWindowUserPointer(window->m_window, window.get());
  glfwSetScrollCallback(window->m_window, ScrollCallback);
  return window;
}

Window::~Window() noexcept {
  if (m_window != nullptr) {
    glfwDestroyWindow(m_window);
  }
  if (m_has_glfw_reference && --g_window_count == 0) {
    glfwTerminate();
  }
}

void Window::PollEvents() {
  glfwPollEvents();
  const bool was_focused = m_input.is_focused;
  m_input.is_focused = glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE;
  for (std::size_t index{}; index < k_glfw_keys.size(); ++index) {
    const bool is_down = m_input.is_focused && glfwGetKey(m_window, k_glfw_keys.at(index)) == GLFW_PRESS;
    m_input.keys_pressed.at(index) = is_down && !m_input.keys_down.at(index);
    m_input.keys_down.at(index) = is_down;
  }
  double cursor_x{};
  double cursor_y{};
  glfwGetCursorPos(m_window, &cursor_x, &cursor_y);
  // 首次采样及重新获得焦点时忽略位置差，避免光标跳变导致相机突然转动。
  const bool can_track = m_has_cursor_position && was_focused && m_input.is_focused;
  m_input.cursor_delta_x = can_track ? cursor_x - m_cursor_x : 0.0;
  m_input.cursor_delta_y = can_track ? cursor_y - m_cursor_y : 0.0;
  m_cursor_x = cursor_x;
  m_cursor_y = cursor_y;
  m_has_cursor_position = true;
  // 等待事件也可能触发滚轮回调，累计量统一在采样时取出并清零。
  m_input.scroll_delta = m_input.is_focused ? m_pending_scroll : 0.0;
  m_pending_scroll = 0.0;
  m_input.is_orbit_down = m_input.is_focused && glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  m_input.is_pan_down = m_input.is_focused && glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
}

void Window::WaitEvents() const { glfwWaitEventsTimeout(0.1); }
void Window::RequestClose() noexcept { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }
void Window::SetTitle(std::string_view title) { glfwSetWindowTitle(m_window, std::string{title}.c_str()); }
bool Window::ShouldClose() const noexcept { return glfwWindowShouldClose(m_window) == GLFW_TRUE; }
VkExtent2D Window::GetFramebufferExtent() const noexcept {
  int width{};
  int height{};
  glfwGetFramebufferSize(m_window, &width, &height);
  return {.width = static_cast<uint32_t>(std::max(width, 0)), .height = static_cast<uint32_t>(std::max(height, 0))};
}
const InputState& Window::GetInputState() const noexcept { return m_input; }
GLFWwindow* Window::GetNativeHandle() const noexcept { return m_window; }

std::vector<const char*> Window::GetInstanceExtensions() const {
  uint32_t count{};
  const char** extensions = glfwGetRequiredInstanceExtensions(&count);
  if (extensions == nullptr) {
    throw std::runtime_error(GetGlfwError());
  }
  return {extensions, extensions + count};
}

VkSurfaceKHR Window::CreateSurface(VkInstance instance) const {
  VkSurfaceKHR surface{};
  const auto result = glfwCreateWindowSurface(instance, m_window, nullptr, &surface);
  if (result != VK_SUCCESS) {
    throw std::runtime_error("Failed to create the window surface (VkResult " +
                             std::to_string(static_cast<int>(result)) + ")");
  }
  return surface;
}

void Window::ScrollCallback(GLFWwindow* window, [[maybe_unused]] double offset_x, double offset_y) noexcept {
  // 用户指针由 Window 持有，供原生窗口回调定位对象，平台后端不得覆盖它。
  auto* owner = static_cast<Window*>(glfwGetWindowUserPointer(window));
  owner->m_pending_scroll += offset_y;
}
}  // namespace fw
