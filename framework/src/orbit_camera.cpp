#include "framework/orbit_camera.h"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <limits>

#include "framework/aabb.h"

namespace fw {
namespace {
constexpr float k_vertical_fov_degrees{55.0F};
constexpr float k_default_distance{6.0F};
constexpr float k_max_distance_scale{200.0F};

float GetPrecisionDistance(glm::vec3 target) noexcept {
  const float magnitude = std::max({std::abs(target.x), std::abs(target.y), std::abs(target.z)});
  // 为坐标加法留出足够的 float 精度余量，接近俯仰极限时也保留可表示的水平偏移。
  return magnitude * (32.0F * std::numeric_limits<float>::epsilon());
}
}  // namespace

void OrbitCamera::ResetCamera(const AABB& bounds, float aspect_ratio) noexcept {
  if (!bounds.IsValid() || !std::isfinite(aspect_ratio) || aspect_ratio <= 0.0F) {
    return;
  }
  const auto center = bounds.GetCenter();
  const float radius = bounds.GetBoundingRadius();
  if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || !std::isfinite(radius)) {
    return;
  }
  const float half_fov_y = glm::radians(k_vertical_fov_degrees) * 0.5F;
  const float half_fov_x = std::atan(std::tan(half_fov_y) * aspect_ratio);
  // 取更窄的 FOV，保证横向和纵向都能容纳包围球；点状边界也保留非零观察距离。
  const float framing_distance = 1.05F * std::max(radius, 0.001F) / std::sin(std::min(half_fov_x, half_fov_y));
  const float distance = std::max(framing_distance, GetPrecisionDistance(center));
  if (!std::isfinite(distance) || !std::isfinite(k_max_distance_scale * (distance / k_default_distance))) {
    return;
  }
  m_framing_distance = distance;
  m_target = center;
  m_yaw = 0.65F;
  m_pitch = 0.35F;
  m_distance = distance;
  SetSceneBoundingSphere(center, radius);
}

void OrbitCamera::SetSceneBoundingSphere(const glm::vec3& center, float radius) noexcept {
  if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || !std::isfinite(radius) ||
      radius < 0.0F) {
    return;
  }
  m_scene_center = center;
  m_scene_radius = radius;
  m_has_scene_bounds = true;
}

void OrbitCamera::Update(const InputState& input, float delta_seconds) noexcept {
  if (input.is_orbit_down) {
    m_yaw -= static_cast<float>(input.cursor_delta_x) * 0.006F;
    // 避开俯仰极点，防止观察方向与世界向上方向平行，导致相机基向量退化。
    m_pitch = std::clamp(m_pitch + static_cast<float>(input.cursor_delta_y) * 0.006F, -1.5F, 1.5F);
    m_yaw = std::remainder(m_yaw, 6.283185307F);
  }
  // 距离限制随取景尺度变化，避免新取景在下一帧被固定范围截断。
  const float scale = m_framing_distance / k_default_distance;
  const float min_distance = std::max(0.5F * scale, GetPrecisionDistance(m_target));
  const float max_distance = std::max(k_max_distance_scale * scale, min_distance);
  m_distance =
    std::clamp(m_distance * std::exp(-static_cast<float>(input.scroll_delta) * 0.12F), min_distance, max_distance);
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
glm::mat4 OrbitCamera::GetProjectionMatrix(float aspect_ratio,
                                           std::span<const glm::vec3> additional_clip_points) const noexcept {
  const double scale = static_cast<double>(m_framing_distance) / k_default_distance;
  double near_plane = 0.05 * scale;
  double far_plane = 500.0 * scale;
  if (m_has_scene_bounds || !additional_clip_points.empty()) {
    const auto eye = glm::dvec3{GetPosition()};
    const auto forward = glm::normalize(glm::dvec3{m_target} - eye);
    double min_depth = near_plane;
    double max_depth = far_plane;
    const double radius = std::max(static_cast<double>(m_scene_radius), 0.001);
    if (m_has_scene_bounds) {
      // 深度沿相机观察方向投影，平移后不能使用相机到球心的欧氏距离代替。
      const double depth = glm::dot(glm::dvec3{m_scene_center} - eye, forward);
      min_depth = depth - radius;
      max_depth = depth + radius;
    }
    for (const auto& point : additional_clip_points) {
      if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
        continue;
      }
      const double depth = glm::dot(glm::dvec3{point} - eye, forward);
      min_depth = std::min(min_depth, depth);
      max_depth = std::max(max_depth, depth);
    }
    if (std::isfinite(min_depth) && std::isfinite(max_depth) && min_depth <= max_depth) {
      const double minimum_near = std::max(radius * 1.0e-4, 1.0e-6);
      const double depth_magnitude = std::max(std::abs(min_depth), std::abs(max_depth));
      // 球半径的 2% 留白；额外角点和较大坐标下同时考虑深度跨度及 float 精度。
      const double margin =
        std::max({radius * 0.02, (max_depth - min_depth) * 0.01,
                  depth_magnitude * (32.0 * std::numeric_limits<float>::epsilon()), minimum_near * 2.0});
      near_plane = std::max(minimum_near, min_depth - margin);
      // 包围球跨过相机或完全位于身后时仍生成有效投影。
      far_plane = std::max(max_depth + margin, near_plane + std::max(margin, minimum_near * 2.0));
    }
  }
  const double aspect = std::isfinite(aspect_ratio) ? std::max(static_cast<double>(aspect_ratio), 0.001) : 1.0;
  // 用 double 计算后转回 GPU 使用的 float 矩阵，减少近远平面接近时的中间精度损失。
  auto projection = glm::mat4{
    glm::perspectiveRH_ZO(glm::radians(static_cast<double>(k_vertical_fov_degrees)), aspect, near_plane, far_plane)};
  // 深度已采用 [0, 1]，这里只翻转一次 Y 以配合正高度的 Vulkan 视口。
  projection[1][1] *= -1.0F;
  return projection;
}
}  // namespace fw
