#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <span>

#include "framework/input_state.h"

namespace fw {
class AABB;

/// @brief 右手轨道相机，+Y 向上，使用 Vulkan 的 [0, 1] 深度范围，投影矩阵仅翻转一次 Y。
class OrbitCamera final {
public:
  /// @brief 以 AABB 中心为目标，恢复默认观察方向并按包围球取景，保留少量边距。
  /// @param aspect_ratio 取景视口的宽高比，省略时按正方形视口计算。
  /// @details 每次调用按传入边界和宽高比重新取景；无效输入不改变相机，点状边界使用最小半径。
  /// 同时保存场景包围球供投影自动计算裁剪范围；最小观察距离考虑目标坐标的 float 精度。
  void ResetCamera(const AABB& bounds, float aspect_ratio = 1.0F) noexcept;
  /// @brief 更新用于自动裁剪的世界空间包围球，保留当前位姿和交互使用的取景尺度。
  /// @details 半径须非负，且球心和半径均有限；无效输入保留原包围球。
  void SetSceneBoundingSphere(const glm::vec3& center, float radius) noexcept;
  void Update(const InputState& input, float delta_seconds) noexcept;
  [[nodiscard]] glm::mat4 GetViewMatrix() const noexcept;
  /// @brief 按当前相机位姿计算带余量的场景近远裁剪范围；未设置包围球时保留默认范围。
  /// @param additional_clip_points 可选世界空间点，只扩展裁剪深度，不调整相机位姿或 FOV。
  /// @details 相机进入包围球或场景位于相机后方时仍保持正 near 和大于 near 的 far。
  [[nodiscard]] glm::mat4 GetProjectionMatrix(float aspect_ratio,
                                              std::span<const glm::vec3> additional_clip_points = {}) const noexcept;
  [[nodiscard]] glm::vec3 GetPosition() const noexcept;

private:
  float m_framing_distance{6.0F};
  glm::vec3 m_scene_center{0.0F};
  float m_scene_radius{};
  bool m_has_scene_bounds{false};
  glm::vec3 m_target{0.0F};
  float m_yaw{0.65F};
  float m_pitch{0.35F};
  float m_distance{6.0F};
};
}  // namespace fw
