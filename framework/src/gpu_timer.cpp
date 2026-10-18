#include "framework/gpu_timer.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "framework/device.h"
#include "vulkan_helpers.h"

namespace fw {
GpuTimer GpuTimer::Create(Device& device, std::uint32_t frame_count, std::uint32_t timestamp_count) {
  detail::Require(frame_count > 0 && timestamp_count > 0, "GPU timer counts must be nonzero");
  std::uint32_t family_count{};
  vkGetPhysicalDeviceQueueFamilyProperties(device.GetPhysicalDevice(), &family_count, nullptr);
  std::vector<VkQueueFamilyProperties> families(family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(device.GetPhysicalDevice(), &family_count, families.data());
  const auto valid_bits = families.at(device.GetInfo().graphics_family).timestampValidBits;
  const auto period = device.GetInfo().properties.limits.timestampPeriod;
  if (valid_bits == 0 || period <= 0.0F) {
    return {};
  }

  GpuTimer timer{};
  timer.m_device = device.GetHandle();
  timer.m_timestamp_period = static_cast<double>(period);
  timer.m_timestamp_mask =
    valid_bits == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << valid_bits) - 1;
  timer.m_slots.resize(frame_count);
  timer.m_results.resize(timestamp_count);
  const VkQueryPoolCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .queryType = VK_QUERY_TYPE_TIMESTAMP,
    .queryCount = timestamp_count,
    .pipelineStatistics = {},
  };
  for (auto& slot : timer.m_slots) {
    slot.written.resize(timestamp_count);
    detail::Check(vkCreateQueryPool(timer.m_device, &info, nullptr, &slot.pool), "Create GPU timer query pool");
  }
  return timer;
}

GpuTimer::~GpuTimer() noexcept { Release(); }
GpuTimer::GpuTimer(GpuTimer&& other) noexcept { *this = std::move(other); }
GpuTimer& GpuTimer::operator=(GpuTimer&& other) noexcept {
  if (this != &other) {
    Release();
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_timestamp_period = std::exchange(other.m_timestamp_period, 0.0);
    m_timestamp_mask = std::exchange(other.m_timestamp_mask, 0);
    m_slots = std::exchange(other.m_slots, {});
    m_results = std::move(other.m_results);
  }
  return *this;
}

void GpuTimer::Release() noexcept {
  for (const auto& slot : m_slots) {
    vkDestroyQueryPool(m_device, slot.pool, nullptr);
  }
  m_slots.clear();
  m_results.clear();
  m_device = VK_NULL_HANDLE;
  m_timestamp_period = 0.0;
  m_timestamp_mask = 0;
}

bool GpuTimer::IsSupported() const noexcept { return m_device != VK_NULL_HANDLE; }

void GpuTimer::BeginFrame(const FrameContext& frame) {
  if (!IsSupported()) {
    return;
  }
  auto& slot = m_slots.at(frame.frame_index);
  std::fill(m_results.begin(), m_results.end(), TimestampResult{});
  const auto count = static_cast<std::uint32_t>(m_results.size());
  if (slot.has_results) {
    // 帧 fence 已完成；未写过的查询保留 available=0，不使用 WAIT_BIT。
    const auto result =
      vkGetQueryPoolResults(m_device, slot.pool, 0, count, m_results.size() * sizeof(TimestampResult), m_results.data(),
                            sizeof(TimestampResult), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result != VK_NOT_READY) {
      detail::Check(result, "Read GPU timer query results");
    }
  }
  vkCmdResetQueryPool(frame.command_buffer, slot.pool, 0, count);
  std::fill(slot.written.begin(), slot.written.end(), std::uint8_t{});
  slot.has_results = true;
}

void GpuTimer::WriteTimestamp(const FrameContext& frame, std::uint32_t index, VkPipelineStageFlags2 stage) {
  if (!IsSupported()) {
    return;
  }
  auto& slot = m_slots.at(frame.frame_index);
  detail::Require(slot.has_results, "BeginFrame must precede GPU timestamps");
  auto& written = slot.written.at(index);
  detail::Require(written == 0, "A GPU timestamp index can only be written once per frame");
  vkCmdWriteTimestamp2(frame.command_buffer, stage, slot.pool, index);
  written = 1;
}

std::optional<double> GpuTimer::GetElapsedMilliseconds(std::uint32_t begin, std::uint32_t end) const {
  if (!IsSupported()) {
    return std::nullopt;
  }
  const auto& first = m_results.at(begin);
  const auto& last = m_results.at(end);
  if (first.available == 0 || last.available == 0) {
    return std::nullopt;
  }
  // 掩掉未实现的高位，并处理硬件时间戳计数器的一次回绕。
  const auto ticks = (last.value - first.value) & m_timestamp_mask;
  return static_cast<double>(ticks) * m_timestamp_period * 1.0e-6;
}
}  // namespace fw
