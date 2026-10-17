#include "framework/imgui_layer.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <cmath>
#include <span>
#include <utility>

#include "framework/device.h"
#include "framework/synchronization.h"
#include "framework/window.h"
#include "vulkan_helpers.h"

namespace fw {
namespace {
// 后端回调不能抛异常，先记录首个失败，返回框架的 C++ 调用边界后再报告。
thread_local VkResult g_pending_result{VK_SUCCESS};

void CheckBackendResult() { detail::Check(std::exchange(g_pending_result, VK_SUCCESS), "ImGui Vulkan backend"); }

float SrgbToLinear(float value) noexcept {
  return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

void ConvertColorsForSrgbTarget(ImDrawData& data, VkFormat format) {
  if (format != VK_FORMAT_B8G8R8A8_SRGB && format != VK_FORMAT_R8G8B8A8_SRGB) {
    return;
  }
  // sRGB 附件会在写入时编码，先将 UI 顶点 RGB 转为线性值，Alpha 保持不变。
  for (ImDrawList* list : data.CmdLists) {
    for (auto& vertex : list->VtxBuffer) {
      auto color = ImGui::ColorConvertU32ToFloat4(vertex.col);
      color.x = SrgbToLinear(color.x);
      color.y = SrgbToLinear(color.y);
      color.z = SrgbToLinear(color.z);
      vertex.col = ImGui::ColorConvertFloat4ToU32(color);
    }
  }
}
}  // namespace

ImGuiLayer::ImGuiLayer(Device& device, Window& window) : m_device{device}, m_window{window} {}

std::unique_ptr<ImGuiLayer> ImGuiLayer::Create(Device& device, Window& window, const SwapchainInfo& swapchain) {
  auto layer = std::unique_ptr<ImGuiLayer>{new ImGuiLayer{device, window}};
  layer->Initialize(swapchain);
  return layer;
}

void ImGuiLayer::Initialize(const SwapchainInfo& swapchain) {
  IMGUI_CHECKVERSION();
  m_context = ImGui::CreateContext();
  detail::Require(m_context != nullptr, "Create ImGui context");
  ImGui::SetCurrentContext(m_context);
  auto& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  // GLFW 后端串接 Window 的滚轮回调，关闭时恢复，保证 UI 和窗口输入均能接收事件。
  m_has_glfw_backend = ImGui_ImplGlfw_InitForVulkan(m_window.GetNativeHandle(), true);
  detail::Require(m_has_glfw_backend, "Initialize ImGui GLFW backend");
  m_swapchain = swapchain;
  InitializeRenderer();
}

void ImGuiLayer::InitializeRenderer() {
  detail::Require(m_swapchain.min_image_count >= 2 && m_swapchain.image_count >= m_swapchain.min_image_count,
                  "ImGui needs at least two swapchain images");
  VkPipelineRenderingCreateInfo rendering{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .pNext = nullptr,
    .viewMask = {},
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &m_swapchain.format,
    .depthAttachmentFormat = {},
    .stencilAttachmentFormat = {},
  };
  ImGui_ImplVulkan_InitInfo info{};
  info.ApiVersion = VK_API_VERSION_1_3;
  info.Instance = m_device.GetInstance();
  info.PhysicalDevice = m_device.GetPhysicalDevice();
  info.Device = m_device.GetHandle();
  info.QueueFamily = m_device.GetInfo().graphics_family;
  info.Queue = m_device.GetGraphicsQueue();
  info.DescriptorPoolSize = 128;
  info.MinImageCount = m_swapchain.min_image_count;
  info.ImageCount = m_swapchain.image_count;
  info.UseDynamicRendering = true;
  info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering;
  info.CheckVkResultFn = OnVulkanResult;
  g_pending_result = VK_SUCCESS;
  try {
    const bool is_initialized = ImGui_ImplVulkan_Init(&info);
    m_has_vulkan_backend = ImGui::GetIO().BackendRendererUserData != nullptr;
    CheckBackendResult();
    detail::Require(is_initialized, "Initialize ImGui Vulkan backend");
  } catch (...) {
    // 初始化可能先建立后端状态再因资源创建失败退出，须记录状态以便析构清理。
    m_has_vulkan_backend = ImGui::GetIO().BackendRendererUserData != nullptr;
    throw;
  }
}

void ImGuiLayer::BeginFrame() {
  ImGui::SetCurrentContext(m_context);
  detail::Require(!m_is_frame_open, "ImGui frame is already open");
  m_has_draw_data = false;
  g_pending_result = VK_SUCCESS;
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  m_is_frame_open = true;
  CheckBackendResult();
}

void ImGuiLayer::EndFrame() {
  ImGui::SetCurrentContext(m_context);
  detail::Require(m_is_frame_open, "ImGui frame has not begun");
  ImGui::Render();
  m_is_frame_open = false;
  m_has_draw_data = true;
}

void ImGuiLayer::Render(const FrameContext& frame) {
  ImGui::SetCurrentContext(m_context);
  detail::Require(m_has_draw_data && !m_is_frame_open, "Finalize ImGui before recording its draw data");
  m_has_draw_data = false;
  ImDrawData* data = ImGui::GetDrawData();
  if (data == nullptr || data->TotalVtxCount == 0) {
    return;
  }
  ConvertColorsForSrgbTarget(*data, frame.color_format);
  // UI 用 LOAD 保留场景颜色；即使布局不变，也需让场景写入对颜色加载和混合可见。
  VkImageMemoryBarrier2 barrier{
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = frame.color_image,
    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
  };
  CmdBarrier(frame.command_buffer, {}, std::span{&barrier, 1});
  VkRenderingAttachmentInfo color{
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .pNext = nullptr,
    .imageView = frame.color_view,
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .resolveMode = {},
    .resolveImageView = {},
    .resolveImageLayout = {},
    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue = {},
  };
  VkRenderingInfo rendering{
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .pNext = nullptr,
    .flags = {},
    .renderArea = {},
    .layerCount = {},
    .viewMask = {},
    .colorAttachmentCount = {},
    .pColorAttachments = nullptr,
    .pDepthAttachment = nullptr,
    .pStencilAttachment = nullptr,
  };
  rendering.renderArea.extent = frame.extent;
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color;
  vkCmdBeginRendering(frame.command_buffer, &rendering);
  g_pending_result = VK_SUCCESS;
  ImGui_ImplVulkan_RenderDrawData(data, frame.command_buffer);
  vkCmdEndRendering(frame.command_buffer);
  // 先结束渲染作用域，再将后端回调记录的错误转换为异常。
  CheckBackendResult();
}

InputState ImGuiLayer::FilterInput(InputState input) const {
  ImGui::SetCurrentContext(m_context);
  const auto& io = ImGui::GetIO();
  if (io.WantCaptureMouse) {
    input.is_orbit_down = false;
    input.is_pan_down = false;
    input.cursor_delta_x = 0.0;
    input.cursor_delta_y = 0.0;
    input.scroll_delta = 0.0;
  }
  if (io.WantCaptureKeyboard) {
    input.keys_down.fill(false);
    input.keys_pressed.fill(false);
  }
  return input;
}

void ImGuiLayer::OnSwapchainChanged(const SwapchainInfo& swapchain) {
  ImGui::SetCurrentContext(m_context);
  // 格式和图像数量影响后端资源；仅尺寸变化无需重建，UI 上下文始终保留。
  const bool should_reset = swapchain.format != m_swapchain.format ||
                            swapchain.image_count != m_swapchain.image_count ||
                            swapchain.min_image_count != m_swapchain.min_image_count;
  if (should_reset) {
    ShutdownRenderer();
  }
  m_swapchain = swapchain;
  if (should_reset) {
    InitializeRenderer();
  }
}

void ImGuiLayer::OnVulkanResult(VkResult result) noexcept {
  if (g_pending_result == VK_SUCCESS && result != VK_SUCCESS) {
    g_pending_result = result;
  }
}

void ImGuiLayer::ShutdownRenderer() noexcept {
  if (m_has_vulkan_backend) {
    ImGui_ImplVulkan_Shutdown();
    m_has_vulkan_backend = false;
  }
}

ImGuiLayer::~ImGuiLayer() noexcept {
  if (m_context != nullptr) {
    ImGuiContext* previous_context = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);
    ShutdownRenderer();
    if (m_has_glfw_backend) {
      ImGui_ImplGlfw_Shutdown();
    }
    ImGui::DestroyContext(m_context);
    if (previous_context != m_context) {
      ImGui::SetCurrentContext(previous_context);
    }
  }
}
}  // namespace fw
