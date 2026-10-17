#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct VmaAllocator_T;

namespace fw {
class Window;

struct DeviceDesc {
  bool enable_validation{
#ifdef NDEBUG
    false
#else
    true
#endif
  };
  bool enable_sampler_anisotropy{false};
  std::vector<std::string> required_extensions{};  ///< 必需设备扩展；框架自动加入交换链扩展。
};

struct DeviceInfo {
  VkPhysicalDeviceProperties properties{};
  VkPhysicalDeviceFeatures supported_features{};
  uint32_t graphics_family{};
  uint32_t present_family{};
  bool is_validation_enabled{false};
  bool is_sampler_anisotropy_enabled{false};
};

/// @brief 管理实例、窗口表面、设备及分配器，所有子资源必须先于 Device 销毁。
/// @details 队列访问在主线程串行执行，图形队列同时支持计算，呈现队列可来自不同队列族。
class Device final {
public:
  ~Device() noexcept;
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  Device(Device&&) = delete;
  Device& operator=(Device&&) = delete;

  [[nodiscard]] static std::unique_ptr<Device> Create(Window& window, const DeviceDesc& desc);
  [[nodiscard]] VkInstance GetInstance() const noexcept;
  [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const noexcept;
  [[nodiscard]] VkDevice GetHandle() const noexcept;
  [[nodiscard]] VkSurfaceKHR GetSurface() const noexcept;
  [[nodiscard]] VkQueue GetGraphicsQueue() const noexcept;
  [[nodiscard]] VkQueue GetPresentQueue() const noexcept;
  [[nodiscard]] VmaAllocator_T* GetAllocator() const noexcept;
  [[nodiscard]] const DeviceInfo& GetInfo() const noexcept;
  void WaitIdle() const;
  /// @brief 用于初始化上传，录制一次命令提交并等待其 fence 完成后返回。
  /// @details 捕获的资源必须存活到调用返回；录制回调抛出异常时放弃提交并清理临时资源。
  /// 该调用同步等待 GPU，不适合作为常规逐帧提交方式。
  void SubmitImmediate(const std::function<void(VkCommandBuffer)>& record) const;

private:
  Device() = default;
  void Initialize(Window& window, const DeviceDesc& desc);
  void CreateInstance(Window& window, const DeviceDesc& desc);
  void SelectPhysicalDevice(const DeviceDesc& desc);
  void CreateLogicalDevice(const DeviceDesc& desc);
  static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT types,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* callback,
                                                      void* user_data) noexcept;

  VkInstance m_instance{};
  VkDebugUtilsMessengerEXT m_debug_messenger{};
  VkSurfaceKHR m_surface{};
  VkPhysicalDevice m_physical_device{};
  VkDevice m_device{};
  VkQueue m_graphics_queue{};
  VkQueue m_present_queue{};
  VmaAllocator_T* m_allocator{};
  DeviceInfo m_info{};
};
}  // namespace fw
