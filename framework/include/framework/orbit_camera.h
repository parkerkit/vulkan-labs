#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "framework/input_state.h"

namespace fw {
class AABB;

/// @brief 右手轨道相机，+Y 向上，使用 Vulkan 的 [0, 1] 深度范围，投影矩阵仅翻转一次 Y。
class OrbitCamera final {
public:
  /// @brief 恢复最近一次 ResetCamera 设置的取景；未设置时使用默认视图。
  void Reset() noexcept;
  /// @brief 以 AABB 中心为目标，恢复默认观察方向并按包围球取景，保留少量边距。
  /// @param aspect_ratio 取景视口的宽高比，省略时按正方形视口计算。
  /// @details 保存取景供 Reset 和 Home 使用；无效边界或宽高比不改变相机，点状边界使用最小半径。
  /// 最小观察距离同时考虑目标坐标的 float 精度，避免相机位置与目标点重合。
  void ResetCamera(const AABB& bounds, float aspect_ratio = 1.0F) noexcept;
  void Update(const InputState& input, float delta_seconds) noexcept;
  [[nodiscard]] glm::mat4 GetViewMatrix() const noexcept;
  [[nodiscard]] glm::mat4 GetProjectionMatrix(float aspect_ratio) const noexcept;
  [[nodiscard]] glm::vec3 GetPosition() const noexcept;

private:
  glm::vec3 m_reset_target{0.0F};
  float m_reset_distance{6.0F};
  glm::vec3 m_target{0.0F};
  float m_yaw{0.65F};
  float m_pitch{0.35F};
  float m_distance{6.0F};
};
}  // namespace fw
