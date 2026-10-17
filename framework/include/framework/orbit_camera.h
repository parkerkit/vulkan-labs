#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "framework/input_state.h"

namespace fw {
/// @brief 右手轨道相机，+Y 向上，使用 Vulkan 的 [0, 1] 深度范围，投影矩阵仅翻转一次 Y。
class OrbitCamera final {
public:
  void Reset() noexcept;
  void Update(const InputState& input, float delta_seconds) noexcept;
  [[nodiscard]] glm::mat4 GetViewMatrix() const noexcept;
  [[nodiscard]] glm::mat4 GetProjectionMatrix(float aspect_ratio) const noexcept;
  [[nodiscard]] glm::vec3 GetPosition() const noexcept;

private:
  glm::vec3 m_target{0.0F};
  float m_yaw{0.65F};
  float m_pitch{0.35F};
  float m_distance{6.0F};
};
}  // namespace fw
