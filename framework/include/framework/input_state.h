#pragma once

#include <array>
#include <cstddef>

namespace fw {
enum class Key { Escape, Space, Home, R, W, A, S, D, Q, E, Count };

/// @brief 每次 PollEvents 更新的输入快照，鼠标位移使用窗口逻辑坐标。
struct InputState {
  std::array<bool, static_cast<std::size_t>(Key::Count)> keys_down{};
  std::array<bool, static_cast<std::size_t>(Key::Count)> keys_pressed{};  ///< 相对上次采样从释放变为按下的按键。
  double cursor_delta_x{};
  double cursor_delta_y{};
  double scroll_delta{};
  bool is_orbit_down{false};
  bool is_pan_down{false};
  bool is_focused{true};

  [[nodiscard]] bool IsDown(Key key) const noexcept { return keys_down.at(static_cast<std::size_t>(key)); }
  [[nodiscard]] bool IsPressed(Key key) const noexcept { return keys_pressed.at(static_cast<std::size_t>(key)); }
};
}  // namespace fw
