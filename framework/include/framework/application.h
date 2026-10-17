#pragma once

#include <functional>
#include <memory>

#include "framework/device.h"
#include "framework/frame_scheduler.h"
#include "framework/sample.h"
#include "framework/window.h"

namespace fw {
class ImGuiLayer;

struct ApplicationDesc {
  WindowDesc window{};
  DeviceDesc device{};
  FrameSchedulerDesc frames{};
  bool enable_imgui{true};  ///< 是否创建 ImGui 层并调用示例的 OnGui。
};

/// @brief 在主线程运行应用，资源创建和渲染失败通过异常传播到调用方的 main。
class Application final {
public:
  using SampleFactory = std::function<std::unique_ptr<ISample>()>;

  ~Application() noexcept;
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;
  Application(Application&&) = delete;
  Application& operator=(Application&&) = delete;

  /// @brief 阻塞运行至窗口关闭或发生异常，退出时先等待 GPU，再释放子资源。
  /// @details 工厂创建的示例由应用持有，初始化失败时也按相同顺序清理。
  static void Run(const ApplicationDesc& desc, const SampleFactory& factory);

private:
  Application();
  void Execute(const ApplicationDesc& desc, const SampleFactory& factory);
  void MainLoop();

  // 成员按声明的逆序销毁，保证示例、UI 和帧调度器先于设备及窗口释放。
  std::unique_ptr<Window> m_window{};
  std::unique_ptr<Device> m_device{};
  std::unique_ptr<FrameScheduler> m_frames{};
  std::unique_ptr<ImGuiLayer> m_ui{};
  std::unique_ptr<ISample> m_sample{};
};
}  // namespace fw
