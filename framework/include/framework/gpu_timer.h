#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "framework/frame_context.h"

namespace fw {
class Device;

/// @brief 图形队列的逐帧 GPU 时间戳；Device 必须比计时器存活更久。
/// @details 析构和移动赋值不等待 GPU，销毁前须确保所有使用已完成。
class GpuTimer final {
public:
  GpuTimer() = default;
  ~GpuTimer() noexcept;
  GpuTimer(const GpuTimer&) = delete;
  GpuTimer& operator=(const GpuTimer&) = delete;
  GpuTimer(GpuTimer&& other) noexcept;
  GpuTimer& operator=(GpuTimer&& other) noexcept;

  /// @brief 不支持时间戳时返回禁用的计时器，其录制方法为空操作，结果为 nullopt。
  [[nodiscard]] static GpuTimer Create(Device& device, std::uint32_t frame_count, std::uint32_t timestamp_count);
  [[nodiscard]] bool IsSupported() const noexcept;
  /// @brief 在帧 fence 完成后、渲染作用域外调用一次：读取槽位旧结果并录制 query reset。
  /// @details 可在 OnUpdate 中调用；结果来自该槽位上一次提交，不额外等待 GPU。
  void BeginFrame(const FrameContext& frame);
  /// @brief 当前帧每个索引最多写一次；默认等待此前各管线阶段的工作以划分连续测量区间。
  /// @details stage 必须为图形队列支持的单个阶段，跨阶段的测量应考虑管线重叠。
  void WriteTimestamp(const FrameContext& frame, std::uint32_t index,
                      VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
  /// @brief 获取 BeginFrame 最近读取的两个时间戳之差；尚未写入的查询返回 nullopt。
  /// @details begin/end 须对应同帧中依次写入的标记，测量区间须短于硬件计数器的回绕周期。
  [[nodiscard]] std::optional<double> GetElapsedMilliseconds(std::uint32_t begin, std::uint32_t end) const;

private:
  struct Slot {
    VkQueryPool pool{};
    bool has_results{false};
    std::vector<std::uint8_t> written{};
  };
  struct TimestampResult {
    std::uint64_t value{};
    std::uint64_t available{};
  };
  void Release() noexcept;

  VkDevice m_device{};
  double m_timestamp_period{};
  std::uint64_t m_timestamp_mask{};
  std::vector<Slot> m_slots{};
  std::vector<TimestampResult> m_results{};
};
}  // namespace fw
