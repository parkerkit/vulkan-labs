#include "framework/application.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "framework/imgui_layer.h"
#include "vulkan_helpers.h"

namespace fw {
Application::Application() = default;

void Application::Run(const ApplicationDesc& desc, const SampleFactory& factory) {
  Application application{};
  application.Execute(desc, factory);
}

void Application::Execute(const ApplicationDesc& desc, const SampleFactory& factory) {
  detail::Require(static_cast<bool>(factory), "A sample factory is required");
  m_window = Window::Create(desc.window);
  m_device = Device::Create(*m_window, desc.device);
  // 零尺寸窗口无法创建交换链，等帧缓冲恢复有效尺寸后再初始化渲染资源。
  while (!m_window->ShouldClose()) {
    const auto extent = m_window->GetFramebufferExtent();
    if (extent.width > 0 && extent.height > 0) {
      break;
    }
    m_window->WaitEvents();
    m_window->PollEvents();
  }
  if (m_window->ShouldClose()) {
    return;
  }
  m_frames = FrameScheduler::Create(*m_device, *m_window, desc.frames);
  if (desc.enable_imgui) {
    m_ui = ImGuiLayer::Create(*m_device, *m_window, m_frames->GetSwapchainInfo());
  }
  m_sample = factory();
  detail::Require(m_sample != nullptr, "Sample factory returned no sample");
  // 先交由成员持有示例，初始化抛出异常时才能先等待 GPU，再销毁已创建的示例资源。
  m_sample->OnInitialize({*m_device, *m_window, *m_frames}, m_frames->GetSwapchainInfo());
  MainLoop();
}

void Application::MainLoop() {
  auto previous = std::chrono::steady_clock::now();
  auto generation = m_frames->GetSwapchainGeneration();
  while (!m_window->ShouldClose()) {
    m_window->PollEvents();
    if (m_window->GetInputState().IsPressed(Key::Escape)) {
      m_window->RequestClose();
    }
    if (m_window->ShouldClose()) {
      break;
    }
    const auto now = std::chrono::steady_clock::now();
    // 限制暂停、拖动窗口或调试中断造成的时间步长，避免恢复时相机和动画突跳。
    const float delta = std::clamp(std::chrono::duration<float>(now - previous).count(), 0.0F, 0.1F);
    previous = now;
    const auto frame = m_frames->BeginFrame();
    // 重建可以返回空帧，但仍须立即通知 UI 和示例，供下一有效帧使用新附件配置。
    if (generation != m_frames->GetSwapchainGeneration()) {
      generation = m_frames->GetSwapchainGeneration();
      if (m_ui) {
        m_ui->OnSwapchainChanged(m_frames->GetSwapchainInfo());
      }
      m_sample->OnResize(m_frames->GetSwapchainInfo());
    }
    if (!frame) {
      const auto extent = m_window->GetFramebufferExtent();
      if (extent.width == 0 || extent.height == 0) {
        m_window->WaitEvents();
      }
      continue;
    }
    InputState input = m_window->GetInputState();
    if (m_ui) {
      // 先构建 UI，再过滤输入和更新场景，使控件在本帧生效且不会同时操纵相机。
      m_ui->BeginFrame();
      m_sample->OnGui();
      m_ui->EndFrame();
      input = m_ui->FilterInput(input);
    }
    m_sample->OnUpdate(frame.value(), input, delta);
    m_sample->OnRender(frame.value());
    if (m_ui) {
      m_ui->Render(frame.value());
    }
    m_frames->EndFrame();
  }
  m_device->WaitIdle();
}

Application::~Application() noexcept {
  if (m_device) {
    // 析构函数体先于成员析构执行，覆盖主循环或示例初始化抛出异常的退出路径。
    const auto result = vkDeviceWaitIdle(m_device->GetHandle());
    if (result != VK_SUCCESS) {
      try {
        std::cerr << "Application shutdown: VkResult " << static_cast<int>(result) << '\n';
      } catch (...) {
        // 日志输出失败不能中断异常展开时的资源清理。
      }
    }
  }
}
}  // namespace fw
