#pragma once

#include "framework/frame_context.h"
#include "framework/input_state.h"
#include "framework/swapchain.h"

namespace fw {
class Window;
class Device;
class FrameScheduler;

/// @brief 借用应用服务，被引用的服务存活至示例销毁之后；不得保存本聚合对象的地址。
struct SampleContext {
  Device& device;
  Window& window;
  FrameScheduler& frames;
};

/// @brief 由 Application 持有的渲染逻辑，初始化失败或正常退出时均先等待 GPU，再销毁示例。
/// @details C++ 异常传播到 main，成员资源通过 RAII 在异常展开时清理。
class ISample {
public:
  ISample() = default;
  virtual ~ISample() noexcept = default;
  ISample(const ISample&) = delete;
  ISample& operator=(const ISample&) = delete;
  ISample(ISample&&) = delete;
  ISample& operator=(ISample&&) = delete;

  /// @brief 创建资源并根据初始交换链配置建立附件，应用不会额外调用一次初始 OnResize。
  virtual void OnInitialize(const SampleContext& context, const SwapchainInfo& swapchain) = 0;
  /// @brief 当前帧 fence 完成后更新状态；启用 ImGui 时，输入已按其捕获标志过滤。
  virtual void OnUpdate(const FrameContext& frame, const InputState& input, float delta_seconds) = 0;
  /// @brief 录制场景命令，返回前结束渲染作用域并将颜色图像保留在 COLOR_ATTACHMENT_OPTIMAL。
  virtual void OnRender(const FrameContext& frame) = 0;
  /// @brief GPU 空闲且交换链替换后调用，用于重建依赖尺寸或格式的资源。
  virtual void OnResize(const SwapchainInfo& swapchain) = 0;
  /// @brief 可选 UI 回调，在 ImGui 帧内且先于 OnUpdate 调用，控件修改影响同一渲染帧。
  virtual void OnGui() {}
};
}  // namespace fw
