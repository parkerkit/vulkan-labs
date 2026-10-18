#include "framework/device.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "framework/window.h"
#include "vulkan_helpers.h"

namespace fw {
namespace {
constexpr const char* k_validation_layer = "VK_LAYER_KHRONOS_validation";

struct QueueSelectionInfo {
  uint32_t graphics_family{};
  uint32_t present_family{};
};

std::optional<QueueSelectionInfo> FindQueues(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
  uint32_t count{};
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, families.data());
  std::optional<uint32_t> graphics{};
  std::optional<uint32_t> present{};
  constexpr VkQueueFlags k_required_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
  for (uint32_t index{}; index < count; ++index) {
    VkBool32 can_present{};
    detail::Check(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, surface, &can_present),
                  "Query present support");
    const bool can_render =
      families.at(index).queueCount > 0 && (families.at(index).queueFlags & k_required_flags) == k_required_flags;
    // 优先用同一队列族完成图形、计算和呈现；不满足时再分别选择图形族与呈现族。
    if (can_render && can_present == VK_TRUE) {
      return QueueSelectionInfo{.graphics_family = index, .present_family = index};
    }
    if (can_render) {
      graphics = index;
    }
    if (can_present == VK_TRUE) {
      present = index;
    }
  }
  if (graphics && present) {
    return QueueSelectionInfo{.graphics_family = graphics.value(), .present_family = present.value()};
  }
  return std::nullopt;
}

std::vector<const char*> GetDeviceExtensions(const DeviceDesc& desc) {
  std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  for (const auto& extension : desc.required_extensions) {
    const bool has_extension =
      std::ranges::any_of(extensions, [&extension](const char* name) { return extension == name; });
    if (!has_extension) {
      extensions.push_back(extension.c_str());
    }
  }
  return extensions;
}

bool HasExtensions(VkPhysicalDevice physical_device, const DeviceDesc& desc) {
  uint32_t count{};
  detail::Check(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr),
                "Enumerate device extensions");
  std::vector<VkExtensionProperties> extensions(count);
  detail::Check(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions.data()),
                "Enumerate device extensions");
  for (const char* required : GetDeviceExtensions(desc)) {
    if (!std::ranges::any_of(extensions, [required](const VkExtensionProperties& extension) {
          return std::string_view{std::data(extension.extensionName)} == required;
        })) {
      return false;
    }
  }
  return true;
}
}  // namespace

std::unique_ptr<Device> Device::Create(Window& window, const DeviceDesc& desc) {
  // 先建立所有权，使任一步初始化失败时都能按已创建的句柄清理。
  auto device = std::unique_ptr<Device>{new Device{}};
  device->Initialize(window, desc);
  return device;
}

void Device::Initialize(Window& window, const DeviceDesc& desc) {
  CreateInstance(window, desc);
  m_surface = window.CreateSurface(m_instance);
  SelectPhysicalDevice(desc);
  CreateLogicalDevice(desc);
  VmaAllocatorCreateInfo allocator_info{
    .flags = {},
    .physicalDevice = m_physical_device,
    .device = m_device,
    .preferredLargeHeapBlockSize = {},
    .pAllocationCallbacks = nullptr,
    .pDeviceMemoryCallbacks = nullptr,
    .pHeapSizeLimit = nullptr,
    .pVulkanFunctions = nullptr,
    .instance = m_instance,
    .vulkanApiVersion = VK_API_VERSION_1_3,
#if VMA_EXTERNAL_MEMORY
    .pTypeExternalMemoryHandleTypes = nullptr,
#endif
  };
  detail::Check(vmaCreateAllocator(&allocator_info, &m_allocator), "Create VMA allocator");
}

void Device::CreateInstance(Window& window, const DeviceDesc& desc) {
  uint32_t version{};
  detail::Check(vkEnumerateInstanceVersion(&version), "Query Vulkan loader version");
  detail::Require(version >= VK_API_VERSION_1_3, "The framework requires a Vulkan 1.3 loader");
  auto required_extensions = window.GetInstanceExtensions();
  if (desc.enable_validation) {
    uint32_t count{};
    detail::Check(vkEnumerateInstanceLayerProperties(&count, nullptr), "Enumerate instance layers");
    std::vector<VkLayerProperties> layers(count);
    detail::Check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "Enumerate instance layers");
    detail::Require(std::ranges::any_of(layers,
                                        [](const VkLayerProperties& layer) {
                                          return std::string_view{std::data(layer.layerName)} == k_validation_layer;
                                        }),
                    "Validation was requested but VK_LAYER_KHRONOS_validation is unavailable");
    required_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  VkApplicationInfo application{
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext = nullptr,
    .pApplicationName = "vulkan-labs",
    .applicationVersion = {},
    .pEngineName = "framework",
    .engineVersion = {},
    .apiVersion = VK_API_VERSION_1_3,
  };
  VkDebugUtilsMessengerCreateInfoEXT debug_info{
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .pNext = nullptr,
    .flags = {},
    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .pfnUserCallback = DebugCallback,
    .pUserData = nullptr,
  };
  VkInstanceCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
    .pApplicationInfo = &application,
    .enabledLayerCount = {},
    .ppEnabledLayerNames = nullptr,
    .enabledExtensionCount = static_cast<uint32_t>(required_extensions.size()),
    .ppEnabledExtensionNames = required_extensions.data(),
  };
  if (desc.enable_validation) {
    info.enabledLayerCount = 1;
    info.ppEnabledLayerNames = &k_validation_layer;
    info.pNext = &debug_info;
  }
  detail::Check(vkCreateInstance(&info, nullptr, &m_instance), "Create Vulkan instance");
  m_info.is_validation_enabled = desc.enable_validation;
  if (desc.enable_validation) {
    const auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
    detail::Require(create_debug != nullptr, "Debug utils entry point is unavailable");
    detail::Check(create_debug(m_instance, &debug_info, nullptr, &m_debug_messenger), "Create debug messenger");
  }
}

void Device::SelectPhysicalDevice(const DeviceDesc& desc) {
  uint32_t count{};
  detail::Check(vkEnumeratePhysicalDevices(m_instance, &count, nullptr), "Enumerate physical devices");
  std::vector<VkPhysicalDevice> devices(count);
  detail::Check(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()), "Enumerate physical devices");
  int best_score{-1};
  for (VkPhysicalDevice candidate : devices) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(candidate, &properties);
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan13Features features13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &features12,
      .robustImageAccess = {},
      .inlineUniformBlock = {},
      .descriptorBindingInlineUniformBlockUpdateAfterBind = {},
      .pipelineCreationCacheControl = {},
      .privateData = {},
      .shaderDemoteToHelperInvocation = {},
      .shaderTerminateInvocation = {},
      .subgroupSizeControl = {},
      .computeFullSubgroups = {},
      .synchronization2 = {},
      .textureCompressionASTC_HDR = {},
      .shaderZeroInitializeWorkgroupMemory = {},
      .dynamicRendering = {},
      .shaderIntegerDotProduct = {},
      .maintenance4 = {},
    };
    VkPhysicalDeviceFeatures2 features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &features13,
      .features = {},
    };
    vkGetPhysicalDeviceFeatures2(candidate, &features);
    // API 版本满足要求仍需逐项检查动态渲染、同步及调用者请求的能力。
    if (properties.apiVersion < VK_API_VERSION_1_3 || features13.dynamicRendering != VK_TRUE ||
        features13.synchronization2 != VK_TRUE ||
        (desc.enable_sampler_anisotropy && features.features.samplerAnisotropy != VK_TRUE) ||
        (desc.enable_multi_draw_indirect && features.features.multiDrawIndirect != VK_TRUE) ||
        (desc.enable_draw_indirect_count && features12.drawIndirectCount != VK_TRUE) ||
        !HasExtensions(candidate, desc)) {
      continue;
    }
    const auto queues = FindQueues(candidate, m_surface);
    if (!queues) {
      continue;
    }
    uint32_t formats{};
    uint32_t modes{};
    detail::Check(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, m_surface, &formats, nullptr),
                  "Query surface formats");
    detail::Check(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, m_surface, &modes, nullptr),
                  "Query present modes");
    if (formats == 0 || modes == 0) {
      continue;
    }
    const int score = (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 10) +
                      (queues->graphics_family == queues->present_family ? 1 : 0);
    if (score > best_score) {
      best_score = score;
      m_physical_device = candidate;
      m_info.properties = properties;
      m_info.supported_features = features.features;
      m_info.graphics_family = queues->graphics_family;
      m_info.present_family = queues->present_family;
    }
  }
  detail::Require(m_physical_device != VK_NULL_HANDLE,
                  "No suitable device: Vulkan 1.3, dynamicRendering, synchronization2, graphics+compute, "
                  "presentation and requested extensions/features are required");
}

void Device::CreateLogicalDevice(const DeviceDesc& desc) {
  const float priority{1.0F};
  std::vector<VkDeviceQueueCreateInfo> queues{};
  const std::array families{m_info.graphics_family, m_info.present_family};
  for (uint32_t family : families) {
    if (std::ranges::any_of(
          queues, [family](const VkDeviceQueueCreateInfo& queue) { return queue.queueFamilyIndex == family; })) {
      continue;
    }
    VkDeviceQueueCreateInfo queue{
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = nullptr,
      .flags = {},
      .queueFamilyIndex = family,
      .queueCount = 1,
      .pQueuePriorities = &priority,
    };
    queues.push_back(queue);
  }
  VkPhysicalDeviceVulkan12Features features12{};
  features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  features12.drawIndirectCount = desc.enable_draw_indirect_count ? VK_TRUE : VK_FALSE;
  VkPhysicalDeviceVulkan13Features features13{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    .pNext = desc.enable_draw_indirect_count ? &features12 : nullptr,
    .robustImageAccess = {},
    .inlineUniformBlock = {},
    .descriptorBindingInlineUniformBlockUpdateAfterBind = {},
    .pipelineCreationCacheControl = {},
    .privateData = {},
    .shaderDemoteToHelperInvocation = {},
    .shaderTerminateInvocation = {},
    .subgroupSizeControl = {},
    .computeFullSubgroups = {},
    .synchronization2 = VK_TRUE,
    .textureCompressionASTC_HDR = {},
    .shaderZeroInitializeWorkgroupMemory = {},
    .dynamicRendering = VK_TRUE,
    .shaderIntegerDotProduct = {},
    .maintenance4 = {},
  };
  VkPhysicalDeviceFeatures2 features{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    .pNext = &features13,
    .features = {},
  };
  features.features.samplerAnisotropy = desc.enable_sampler_anisotropy ? VK_TRUE : VK_FALSE;
  features.features.multiDrawIndirect = desc.enable_multi_draw_indirect ? VK_TRUE : VK_FALSE;
  // 支持的特性不会自动启用，只向逻辑设备申请框架和调用者实际需要的特性。
  const auto extensions = GetDeviceExtensions(desc);
  VkDeviceCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext = &features,
    .flags = {},
    .queueCreateInfoCount = static_cast<uint32_t>(queues.size()),
    .pQueueCreateInfos = queues.data(),
    .enabledLayerCount = {},
    .ppEnabledLayerNames = nullptr,
    .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
    .ppEnabledExtensionNames = extensions.data(),
    .pEnabledFeatures = nullptr,
  };
  detail::Check(vkCreateDevice(m_physical_device, &info, nullptr, &m_device), "Create logical device");
  vkGetDeviceQueue(m_device, m_info.graphics_family, 0, &m_graphics_queue);
  vkGetDeviceQueue(m_device, m_info.present_family, 0, &m_present_queue);
  m_info.is_sampler_anisotropy_enabled = desc.enable_sampler_anisotropy;
  m_info.is_multi_draw_indirect_enabled = desc.enable_multi_draw_indirect;
}

Device::~Device() noexcept {
  if (m_device != VK_NULL_HANDLE) {
    // 调用者须先释放所有子资源；这里的等待也覆盖设备部分初始化后的清理路径。
    vkDeviceWaitIdle(m_device);
    if (m_allocator != nullptr) {
      vmaDestroyAllocator(m_allocator);
    }
    vkDestroyDevice(m_device, nullptr);
  }
  if (m_surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
  }
  if (m_debug_messenger != VK_NULL_HANDLE) {
    const auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy_debug != nullptr) {
      destroy_debug(m_instance, m_debug_messenger, nullptr);
    }
  }
  if (m_instance != VK_NULL_HANDLE) {
    vkDestroyInstance(m_instance, nullptr);
  }
}

void Device::WaitIdle() const { detail::Check(vkDeviceWaitIdle(m_device), "Wait for device idle"); }

void Device::SubmitImmediate(const std::function<void(VkCommandBuffer)>& record) const {
  // 局部所有者负责分配和录制失败时的清理，已提交的命令必须等待完成后才能释放命令池。
  struct SubmissionState {
    explicit SubmissionState(VkDevice device_handle) noexcept : device{device_handle} {}
    ~SubmissionState() noexcept {
      if (is_submitted) {
        vkWaitForFences(device, 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
      }
      vkDestroyFence(device, fence, nullptr);
      vkDestroyCommandPool(device, pool, nullptr);
    }
    SubmissionState(const SubmissionState&) = delete;
    SubmissionState& operator=(const SubmissionState&) = delete;
    SubmissionState(SubmissionState&&) = delete;
    SubmissionState& operator=(SubmissionState&&) = delete;

    VkDevice device{};
    VkCommandPool pool{};
    VkFence fence{};
    bool is_submitted{false};
  };
  SubmissionState submission{m_device};
  VkCommandPoolCreateInfo pool{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
    .queueFamilyIndex = m_info.graphics_family,
  };
  detail::Check(vkCreateCommandPool(m_device, &pool, nullptr, &submission.pool), "Create upload command pool");
  VkCommandBufferAllocateInfo allocate{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext = nullptr,
    .commandPool = submission.pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1,
  };
  VkCommandBuffer command_buffer{};
  detail::Check(vkAllocateCommandBuffers(m_device, &allocate, &command_buffer), "Allocate upload command buffer");
  VkCommandBufferBeginInfo begin{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext = nullptr,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    .pInheritanceInfo = nullptr,
  };
  detail::Check(vkBeginCommandBuffer(command_buffer, &begin), "Begin upload command buffer");
  record(command_buffer);
  detail::Check(vkEndCommandBuffer(command_buffer), "End upload command buffer");
  VkFenceCreateInfo fence{
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .pNext = nullptr,
    .flags = {},
  };
  detail::Check(vkCreateFence(m_device, &fence, nullptr, &submission.fence), "Create upload fence");
  VkCommandBufferSubmitInfo command{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
    .pNext = nullptr,
    .commandBuffer = command_buffer,
    .deviceMask = {},
  };
  VkSubmitInfo2 submit{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .pNext = nullptr,
    .flags = {},
    .waitSemaphoreInfoCount = {},
    .pWaitSemaphoreInfos = nullptr,
    .commandBufferInfoCount = 1,
    .pCommandBufferInfos = &command,
    .signalSemaphoreInfoCount = {},
    .pSignalSemaphoreInfos = nullptr,
  };
  detail::Check(vkQueueSubmit2(m_graphics_queue, 1, &submit, submission.fence), "Submit upload batch");
  submission.is_submitted = true;
  detail::Check(vkWaitForFences(m_device, 1, &submission.fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
                "Wait for upload batch");
  submission.is_submitted = false;
}

VkInstance Device::GetInstance() const noexcept { return m_instance; }
VkPhysicalDevice Device::GetPhysicalDevice() const noexcept { return m_physical_device; }
VkDevice Device::GetHandle() const noexcept { return m_device; }
VkSurfaceKHR Device::GetSurface() const noexcept { return m_surface; }
VkQueue Device::GetGraphicsQueue() const noexcept { return m_graphics_queue; }
VkQueue Device::GetPresentQueue() const noexcept { return m_present_queue; }
VmaAllocator_T* Device::GetAllocator() const noexcept { return m_allocator; }
const DeviceInfo& Device::GetInfo() const noexcept { return m_info; }

VKAPI_ATTR VkBool32 VKAPI_CALL Device::DebugCallback([[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                     [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT types,
                                                     const VkDebugUtilsMessengerCallbackDataEXT* callback,
                                                     [[maybe_unused]] void* user_data) noexcept {
  try {
    std::cerr << "[Vulkan] " << callback->pMessage << '\n';
  } catch (...) {
    // C++ 异常不得穿过 Vulkan 的 C 回调边界。
  }
  return VK_FALSE;
}
}  // namespace fw
