#include "framework/orbit_camera.h"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace fw {
void OrbitCamera::Reset() noexcept {
  m_target = glm::vec3{0.0F};
  m_yaw = 0.65F;
  m_pitch = 0.35F;
  m_distance = 6.0F;
}

void OrbitCamera::Update(const InputState& input, float delta_seconds) noexcept {
  if (input.IsPressed(Key::Home)) {
    Reset();
  }
  if (input.is_orbit_down) {
    m_yaw -= static_cast<float>(input.cursor_delta_x) * 0.006F;
    // 避开俯仰极点，防止观察方向与世界向上方向平行，导致相机基向量退化。
    m_pitch = std::clamp(m_pitch + static_cast<float>(input.cursor_delta_y) * 0.006F, -1.5F, 1.5F);
    m_yaw = std::remainder(m_yaw, 6.283185307F);
  }
  m_distance = std::clamp(m_distance * std::exp(-static_cast<float>(input.scroll_delta) * 0.12F), 0.5F, 200.0F);
  const glm::vec3 forward = glm::normalize(m_target - GetPosition());
  const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0.0F, 1.0F, 0.0F}));
  const glm::vec3 up = glm::cross(right, forward);
  if (input.is_pan_down) {
    m_target += (-right * static_cast<float>(input.cursor_delta_x) + up * static_cast<float>(input.cursor_delta_y)) *
                (m_distance * 0.0015F);
  }
  const float speed = std::clamp(delta_seconds, 0.0F, 0.1F) * m_distance;
  const float lateral = static_cast<float>(input.IsDown(Key::D)) - static_cast<float>(input.IsDown(Key::A));
  const float longitudinal = static_cast<float>(input.IsDown(Key::W)) - static_cast<float>(input.IsDown(Key::S));
  const float vertical = static_cast<float>(input.IsDown(Key::E)) - static_cast<float>(input.IsDown(Key::Q));
  m_target += speed * (right * lateral + forward * longitudinal + glm::vec3{0.0F, vertical, 0.0F});
}

glm::vec3 OrbitCamera::GetPosition() const noexcept {
  return m_target + m_distance * glm::vec3{std::cos(m_pitch) * std::sin(m_yaw), std::sin(m_pitch),
                                           std::cos(m_pitch) * std::cos(m_yaw)};
}
glm::mat4 OrbitCamera::GetViewMatrix() const noexcept {
  return glm::lookAtRH(GetPosition(), m_target, glm::vec3{0.0F, 1.0F, 0.0F});
}
glm::mat4 OrbitCamera::GetProjectionMatrix(float aspect_ratio) const noexcept {
  auto projection = glm::perspectiveRH_ZO(glm::radians(55.0F), std::max(aspect_ratio, 0.001F), 0.05F, 500.0F);
  // 深度已采用 [0, 1]，这里只翻转一次 Y 以配合正高度的 Vulkan 视口。
  projection[1][1] *= -1.0F;
  return projection;
}
}  // namespace fw
