// gltf

#pragma warning(disable : 4996)

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_USE_CPP14
#include <tiny_gltf/tiny_gltf.h>

#define GLFW_INCLUDE_VULKAN  // 定义这个宏之后 glfw3.h 文件就会包含 Vulkan 的头文件
#include <GLFW/glfw3.h>

// clang-format off
#define GLM_FORCE_RADIANS            // glm函数的参数使用弧度
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // 透视矩阵深度值范围 [-1, 1] => [0, 1]
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
// clang-format on

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace lvk_tidy {
template <typename T>
[[nodiscard]] T& GetRequiredValue(std::optional<T>& value) {
  if (!value.has_value()) {
    throw std::bad_optional_access{};
  }

  return *value;
}

template <typename T>
[[nodiscard]] const T& GetRequiredValue(const std::optional<T>& value) {
  if (!value.has_value()) {
    throw std::bad_optional_access{};
  }

  return *value;
}

template <typename T>
[[nodiscard]] T LoadInstanceProcAddress(VkInstance instance, const char* name) noexcept {
  return std::bit_cast<T>(vkGetInstanceProcAddr(instance, name));
}

template <typename T>
[[nodiscard]] T LoadDeviceProcAddress(VkDevice device, const char* name) noexcept {
  return std::bit_cast<T>(vkGetDeviceProcAddr(device, name));
}
}  // namespace lvk_tidy

// ImGui 的文本接口统一接收已格式化的字符串，避免 C 风格可变参数。
namespace viewer_ui {
void Text(std::string_view text) {
  const auto* begin = text.empty() ? "" : text.data();
  ImGui::TextUnformatted(begin, begin + text.size());
}

void TextWrapped(std::string_view text) {
  ImGui::PushTextWrapPos(0.F);
  Text(text);
  ImGui::PopTextWrapPos();
}

void TextDisabled(std::string_view text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  Text(text);
  ImGui::PopStyleColor();
}

void SetTooltip(std::string_view text) {
  if (ImGui::BeginTooltip()) {
    Text(text);
    ImGui::EndTooltip();
  }
}
}  // namespace viewer_ui

// 窗口默认大小
constexpr uint32_t k_width = 1120;
constexpr uint32_t k_height = 760;

// 同时并行处理的帧数
constexpr int k_max_frames_in_flight = 2;

// 需要开启的校验层的名称
const std::vector<const char*> k_validation_layers = {"VK_LAYER_KHRONOS_validation"};
// 交换链扩展
const std::vector<const char*> k_device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

// 是否启用校验层
#ifdef NDEBUG
const bool k_enable_validation_layers = false;
#else
const bool k_enable_validation_layers = true;
#endif  // NDEBUG

struct Sampler {
  VkSampler sampler{nullptr};
};

struct Image {
  std::string name{};

  VkImage image{nullptr};
  VkImageView imageView{nullptr};
  VkDeviceMemory imageMemory{nullptr};
};

struct DescriptorSets {
  std::vector<VkDescriptorSet> descriptorSets{};
};

struct Texture {
  std::string sampler;
  std::string image;
};

struct Uniform {
  std::vector<void*> mappeds{};
  std::vector<VkBuffer> buffers{};
  std::vector<VkDeviceMemory> memorys{};

  std::unique_ptr<DescriptorSets> descriptorSets{std::make_unique<DescriptorSets>()};

  void UpdateUniform(size_t current_frame, const void* data, size_t size) {
    std::memcpy(mappeds[current_frame], data, size);
  }
};

enum class ShadingMode : int { Unlit, Studio, PBR };
enum class ColorSource : int { Material, Texture, Single, Vertex };
enum class FaceDisplay : int { DoubleSided, Material, SingleSided };
enum class PipelineType : uint8_t { Opaque, Blend };

// vec4 字段的大小和偏移匹配 viewer.frag 的 std140 布局，下方 static_assert 约束布局。
// 数据通过 memcpy 上传，无需额外约束 CPU 对象地址对齐，避免包含它的 Material 产生隐式填充。
struct MaterialUniform {
  glm::vec4 baseColor{1.F};
  glm::vec4 emissive{0.F};
  glm::vec4 surface{1.F, 1.F, 1.F, 1.F};  // roughness, metallic, normal scale, AO strength
  glm::vec4 tint{1.F};
  glm::vec4 cameraPosition{};
  glm::vec4 lightSettings{3.F, 0.03F, 1.F, 0.F};  // studio strength, ambient, exposure, sRGB attachment
  glm::ivec4 options{2, 0, 0, 0};                 // shading, color source, texture flags, alpha mode
  glm::ivec4 uvSets{};                            // base color, metallic/roughness, normal, AO
  glm::ivec4 materialOptions{};                   // emissive UV, unlit, double sided, reserved
  glm::vec4 alphaSettings{0.5F, 0.F, 0.F, 0.F};
};

struct Material {
  std::string name{};
  MaterialUniform original{};
  std::array<std::string, 5> textures{};
  std::unique_ptr<Uniform> uniform{};
};

struct Buffer {
  VkBuffer buffer{nullptr};
  VkDeviceMemory buffer_memory{nullptr};
};

struct DisplaySettings {
  ShadingMode shading{ShadingMode::PBR};
  ColorSource colorSource{ColorSource::Material};
  FaceDisplay faces{FaceDisplay::DoubleSided};
  glm::vec3 singleColor{0.8F};  // UI 使用 sRGB，上传时转换到线性空间。
  glm::vec3 tint{1.F};
  bool tintEnabled{false};
  bool respectUnlit{true};
  bool overrideRoughness{false};
  bool overrideMetallic{false};
  float roughness{0.5F};
  float metallic{0.F};
  float lightStrength{3.F};
  float ambient{0.03F};
  float exposure{1.F};
};

struct Bounds {
  glm::vec3 minimum{std::numeric_limits<float>::max()};
  glm::vec3 maximum{std::numeric_limits<float>::lowest()};

  bool IsValid() const { return minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z; }

  void Include(const glm::vec3& point) {
    if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z)) {
      minimum = glm::min(minimum, point);
      maximum = glm::max(maximum, point);
    }
  }

  void Include(const Bounds& bounds, const glm::mat4& matrix) {
    if (!bounds.IsValid()) {
      return;
    }
    for (int corner = 0; corner < 8; ++corner) {
      const glm::vec3 point{(corner & 1) ? bounds.maximum.x : bounds.minimum.x,
                            (corner & 2) ? bounds.maximum.y : bounds.minimum.y,
                            (corner & 4) ? bounds.maximum.z : bounds.minimum.z};
      Include(glm::vec3(matrix * glm::vec4(point, 1.F)));
    }
  }
};

struct Primitive {
  std::string vertexBuffer{};
  std::string indexBuffer{};
  uint32_t indexCount{};
  Material* material{};  // Model 持有并共享 glTF 材质，Primitive 只引用。
  Bounds bounds{};
  std::array<Bounds, 2> morphBounds{};
  bool hasSkin{false};
  bool hasMorph{false};
  bool hasVertexColor{false};
  int uvMask{};
};

struct Mesh {
  std::string name{};

  std::vector<std::unique_ptr<Primitive>> primitives{};
};

struct Camera {
  std::string name{};
  std::string type{};
  tinygltf::PerspectiveCamera perspective{};
  tinygltf::OrthographicCamera orthographic{};
};

struct Skin {
  std::unique_ptr<Uniform> matrix{};
  std::vector<glm::mat4> inverseBindMatrices{};
  std::vector<int> joints{};  // 元素是 gltf 中 Node 的索引，骨骼动画的关节
};

struct Node {
  std::string name{};
  int index{};

  Node* parent{};

  std::map<std::string, glm::mat4> matrices{};

  glm::mat4 GetLocalMatrix() const {
    if (matrices.contains("matrix")) {
      return matrices.at("matrix");
    }

    auto translation = matrices.contains("translation") ? matrices.at("translation") : glm::mat4(1.F);
    auto rotation = matrices.contains("rotation") ? matrices.at("rotation") : glm::mat4(1.F);
    auto scale = matrices.contains("scale") ? matrices.at("scale") : glm::mat4(1.F);

    return translation * rotation * scale;
  }

  std::unique_ptr<Uniform> modelUniform{};

  std::unique_ptr<Skin> skin{};

  std::unique_ptr<Mesh> mesh{};
  std::unique_ptr<Camera> camera{};

  std::vector<int> children{};
  glm::vec2 morphWeights{0.F};
};

struct Scene {
  std::string name{};

  std::vector<int> nodes{};
};

struct DescriptorSetLayout {
  VkDescriptorSetLayout descriptorSetLayout{nullptr};
};

struct Pipeline {
  VkPipeline pipeline{nullptr};
  VkPipelineLayout pipelineLayout{nullptr};
  std::vector<std::string> descriptorSetLayouts{};
};

struct ModelAttributes {
  bool visibility{true};
  bool animation{false};

  double animationStartTime{};
  double animationTime{};
  int activeScene{};
  DisplaySettings display{};

  std::string infomation{};
};

struct AnimationTarget {
  int node{};
  std::string path{};
};

struct AnimationChannel {
  int sampler{};
  AnimationTarget target{};
};

struct AnimationSampler {
  std::vector<float> input{};
  std::vector<float> output{};
  std::string interpolation{"LINEAR"};
};

struct Animation {
  std::vector<std::unique_ptr<AnimationChannel>> channels{};
  std::vector<std::unique_ptr<AnimationSampler>> samplers{};
};

struct Model {
  ModelAttributes attributes{};
  tinygltf::Model gltfModel{};
  std::vector<std::unique_ptr<Material>> materials{};
  std::unique_ptr<Uniform> identitySkin{};

  // 节点与 glTF 索引一一对应，场景和子节点仅引用索引，资源只创建和销毁一次。
  std::vector<std::unique_ptr<Node>> nodes{};
  std::vector<std::unique_ptr<Scene>> scenes{};

  std::vector<std::unique_ptr<Animation>> animations{};

  std::unordered_map<std::string, std::unique_ptr<Image>> images;
  std::unordered_map<std::string, std::unique_ptr<Buffer>> buffers;
  std::unordered_map<std::string, std::unique_ptr<Sampler>> samplers;
  std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
};

struct Context {
  std::unordered_map<PipelineType, std::unique_ptr<Pipeline>> pipelines;
  std::unordered_map<std::string, std::unique_ptr<DescriptorSetLayout>> descriptorSetLayouts;
};

struct PushConstantVP {
  alignas(16) glm::mat4 view{glm::mat4(1.F)};
  alignas(16) glm::mat4 proj{glm::mat4(1.F)};
};

struct alignas(16) NodeUniform {
  glm::mat4 model{1.F};
  glm::vec4 morphWeights{};
  glm::ivec4 animation{};  // skin enabled, morph enabled, UV mask, reserved
};

static_assert(sizeof(MaterialUniform) == 160 && offsetof(MaterialUniform, options) == 96);
static_assert(sizeof(NodeUniform) == 96 && offsetof(NodeUniform, animation) == 80);

struct DrawItem {
  const Model* model{};
  const Node* node{};
  const Primitive* primitive{};
  float depth{};
};

struct Vertex {
  glm::vec3 position{};
  glm::vec3 normal{};
  glm::vec2 uv0{};
  glm::vec2 uv1{};
  glm::vec4 color{1.F};
  glm::vec4 weights{1.F, 0.F, 0.F, 0.F};
  glm::vec4 joints{};
  glm::vec3 morphPosition0{};
  glm::vec3 morphPosition1{};
  glm::vec3 morphNormal0{};
  glm::vec3 morphNormal1{};
  uint32_t flags{};

  static std::vector<VkVertexInputBindingDescription> GetBindingDescriptions() {
    return {{0, static_cast<uint32_t>(sizeof(Vertex)), VK_VERTEX_INPUT_RATE_VERTEX}};
  }

  static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions() {
    return {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv0)},
            {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv1)},
            {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)},
            {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weights)},
            {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, joints)},
            {7, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, morphPosition0)},
            {8, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, morphPosition1)},
            {9, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, morphNormal0)},
            {10, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, morphNormal1)},
            {11, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, flags)}};
  }
};

/// @brief 支持图形和呈现的队列族
struct QueueFamilyIndices {
  std::optional<uint32_t> graphicsFamily{};
  std::optional<uint32_t> presentFamily{};

  constexpr bool IsComplete() const noexcept { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

/// @brief 交换链属性信息
struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities{};
  std::vector<VkSurfaceFormatKHR> foramts;
  std::vector<VkPresentModeKHR> presentModes;
};

class HelloTriangleApplication {
public:
  void Run() {
    InitWindow();
    InitVulkan();
    MainLoop();
    Cleanup();
  }

private:
  void InitWindow() {
    if (GLFW_FALSE == glfwInit()) {
      throw std::runtime_error("failed to init GLFW");
    }

    // 禁止 glfw 创建 OpenGL 上下文
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_window = glfwCreateWindow(k_width, k_height, "Vulkan", nullptr, nullptr);

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, FramebufferResizeCallback);
  }

  void InitVulkan() {
    CreateInstance();
    SetupDebugCallback();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateCommandPool();
    CreateDepthResources();
    CreateFramebuffers();
    LoadModels();
    CreateImGuiDescriptorPool();
    CreateCommandBuffers();
    CreateSyncObjects();
    InitImGui();
  }

  void MainLoop() {
    while (!glfwWindowShouldClose(m_window)) {
      glfwPollEvents();
      PrepareImGui();
      DrawFrame();
    }

    // 等待逻辑设备的操作结束执行
    // DrawFrame 函数中的操作是异步执行的，关闭窗口跳出while循环时，绘制操作和呈现操作可能仍在执行，不能进行清除操作
    vkDeviceWaitIdle(m_device);
  }

  void DestroyUniform(const std::unique_ptr<Uniform>& uniform) noexcept {
    if (!uniform) {
      return;
    }
    for (size_t i = 0; i < k_max_frames_in_flight; ++i) {
      vkDestroyBuffer(m_device, uniform->buffers[i], nullptr);
      vkFreeMemory(m_device, uniform->memorys[i], nullptr);
    }
  }

  void DestroyNode(const std::unique_ptr<Node>& node) noexcept {
    DestroyUniform(node->modelUniform);
    if (node->skin) {
      DestroyUniform(node->skin->matrix);
    }
  }

  void DestroyModel() noexcept {
    for (const auto& [_, model] : m_models) {
      for (const auto& node : model->nodes) {
        DestroyNode(node);
      }
      for (const auto& material : model->materials) {
        DestroyUniform(material->uniform);
      }
      DestroyUniform(model->identitySkin);

      for (const auto& [_2, buffer] : model->buffers) {
        vkDestroyBuffer(m_device, buffer->buffer, nullptr);
        vkFreeMemory(m_device, buffer->buffer_memory, nullptr);
      }

      for (const auto& [_3, image] : model->images) {
        vkDestroyImageView(m_device, image->imageView, nullptr);
        vkDestroyImage(m_device, image->image, nullptr);
        vkFreeMemory(m_device, image->imageMemory, nullptr);
      }

      for (const auto& [_4, sampler] : model->samplers) {
        vkDestroySampler(m_device, sampler->sampler, nullptr);
      }
    }

    for (const auto& [_, pipeline] : m_context->pipelines) {
      vkDestroyPipeline(m_device, pipeline->pipeline, nullptr);
      vkDestroyPipelineLayout(m_device, pipeline->pipelineLayout, nullptr);
    }

    for (const auto& [_, descriptorSetLayout] : m_context->descriptorSetLayouts) {
      vkDestroyDescriptorSetLayout(m_device, descriptorSetLayout->descriptorSetLayout, nullptr);
    }
  }

  void Cleanup() noexcept {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    CleanupSwapChain();
    DestroyModel();

    vkDestroyRenderPass(m_device, m_render_pass, nullptr);
    vkDestroyDescriptorPool(m_device, m_imgui_descriptor_pool, nullptr);
    vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);

    for (size_t i = 0; i < k_max_frames_in_flight; ++i) {
      vkDestroySemaphore(m_device, m_image_available_semaphores.at(i), nullptr);
      vkDestroyFence(m_device, m_in_flight_fences.at(i), nullptr);
    }

    for (auto semaphore : m_render_finished_semaphores) {
      vkDestroySemaphore(m_device, semaphore, nullptr);
    }

    vkDestroyCommandPool(m_device, m_command_pool, nullptr);
    vkDestroyDevice(m_device, nullptr);

    if (k_enable_validation_layers) {
      DestroyDebugUtilsMessengerEXT(m_instance, m_debug_messenger, nullptr);
    }

    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);  // 清理表面对象，必须在 Vulkan 实例清理之前
    vkDestroyInstance(m_instance, nullptr);

    glfwDestroyWindow(m_window);
    glfwTerminate();
  }

private:
  void LoadModels() {
    CreateDescriptorSetLayouts();
    CreatePipelines();

    m_models.try_emplace(PROJECT_ASSETS_DIR "models/morph.gltf", std::make_unique<Model>());
    m_models.try_emplace(PROJECT_ASSETS_DIR "models/rectangle_y10.gltf", std::make_unique<Model>());
    m_models.try_emplace(PROJECT_ASSETS_DIR "models/sphere.gltf", std::make_unique<Model>());
    m_models.try_emplace(PROJECT_ASSETS_DIR "models/skin.gltf", std::make_unique<Model>());
    m_models.try_emplace(PROJECT_ASSETS_DIR "models/C919-gltf/C919.gltf", std::make_unique<Model>());
    m_models.try_emplace(PROJECT_ASSETS_DIR "models/teacup/teacup.gltf", std::make_unique<Model>());
    m_models.try_emplace(PROJECT_ASSETS_DIR "models/skybox/skybox.gltf", std::make_unique<Model>());

    for (const auto& [name, model] : m_models) {
      LoadModel(name, model);
    }
    // 先读取所有 glTF 元数据，再按实际节点、材质和贴图槽位分配描述符。
    CreateDescriptorPool();
    for (const auto& [_, model] : m_models) {
      ParseModel(model);
    }
  }

  void LoadModel(const std::string& file_name, const std::unique_ptr<Model>& model) {
    std::string err{};
    std::string warn{};

    bool res{false};
    std::filesystem::path path(file_name);
    if (path.has_extension()) {
      auto ex = path.extension();
      if (path.extension() == ".glb") {
        res = m_gltf_loader.LoadBinaryFromFile(&model->gltfModel, &err, &warn, path.string());
      }
      if (path.extension() == ".gltf") {
        res = m_gltf_loader.LoadASCIIFromFile(&model->gltfModel, &err, &warn, path.string());
      }
    }

    if (!warn.empty()) {
      std::cout << "WARN: " << warn << '\n';
    }

    if (!err.empty()) {
      std::cout << "ERR: " << err << '\n';
    }

    if (!res) {
      throw std::runtime_error("failed to load gltf model");
    }
  }

  double ReadComponent(const unsigned char* data, int type, bool normalized) const {
    auto read = [&]<typename T>() {
      T value{};
      std::memcpy(&value, data, sizeof(value));
      return static_cast<double>(value);
    };
    switch (type) {
      case TINYGLTF_COMPONENT_TYPE_BYTE:
        return normalized ? std::max(read.operator()<int8_t>() / 127.0, -1.0) : read.operator()<int8_t>();
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return read.operator()<uint8_t>() / (normalized ? 255.0 : 1.0);
      case TINYGLTF_COMPONENT_TYPE_SHORT:
        return normalized ? std::max(read.operator()<int16_t>() / 32767.0, -1.0) : read.operator()<int16_t>();
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return read.operator()<uint16_t>() / (normalized ? 65535.0 : 1.0);
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        return read.operator()<uint32_t>();
      case TINYGLTF_COMPONENT_TYPE_FLOAT:
        return read.operator()<float>();
      case TINYGLTF_COMPONENT_TYPE_DOUBLE:
        return read.operator()<double>();
      default:
        throw std::runtime_error("Unsupported glTF accessor component type");
    }
  }

  std::vector<double> ReadAccessor(const Model& model, const tinygltf::Accessor& accessor) const {
    const int components = tinygltf::GetNumComponentsInType(accessor.type);
    const int component_bytes = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (components <= 0 || component_bytes <= 0) {
      throw std::runtime_error("Invalid glTF accessor layout");
    }
    const size_t element_bytes = static_cast<size_t>(components * component_bytes);
    std::vector<double> values(accessor.count * components, 0.0);
    auto read_at = [&](int view_index, size_t offset, int type, bool normalized) {
      const auto& view = model.gltfModel.bufferViews.at(view_index);
      const auto& buffer = model.gltfModel.buffers.at(view.buffer).data;
      const size_t bytes = static_cast<size_t>(tinygltf::GetComponentSizeInBytes(type));
      if (offset > view.byteLength || bytes > view.byteLength - offset || view.byteOffset > buffer.size() ||
          offset > buffer.size() - view.byteOffset || bytes > buffer.size() - view.byteOffset - offset) {
        throw std::runtime_error("glTF accessor exceeds its buffer view");
      }
      return ReadComponent(buffer.data() + view.byteOffset + offset, type, normalized);
    };
    if (accessor.bufferView >= 0) {
      const auto& view = model.gltfModel.bufferViews.at(accessor.bufferView);
      const size_t stride = view.byteStride > 0 ? view.byteStride : element_bytes;
      if (stride < element_bytes) {
        throw std::runtime_error("Invalid glTF accessor stride");
      }
      for (size_t i = 0; i < accessor.count; ++i) {
        for (int component = 0; component < components; ++component) {
          values[i * components + component] =
            read_at(accessor.bufferView, accessor.byteOffset + i * stride + component * component_bytes,
                    accessor.componentType, accessor.normalized);
        }
      }
    }
    if (accessor.sparse.isSparse) {
      const auto& sparse = accessor.sparse;
      const int index_bytes = tinygltf::GetComponentSizeInBytes(sparse.indices.componentType);
      for (int i = 0; i < sparse.count; ++i) {
        const auto index = static_cast<size_t>(read_at(sparse.indices.bufferView,
                                                       sparse.indices.byteOffset + static_cast<size_t>(i) * index_bytes,
                                                       sparse.indices.componentType, false));
        if (index >= accessor.count) {
          throw std::runtime_error("Invalid glTF sparse accessor index");
        }
        for (int component = 0; component < components; ++component) {
          values[index * components + component] =
            read_at(sparse.values.bufferView,
                    sparse.values.byteOffset + static_cast<size_t>(i) * element_bytes + component * component_bytes,
                    accessor.componentType, accessor.normalized);
        }
      }
    }
    return values;
  }

  std::vector<float> ParseAnimationBuffer(const std::unique_ptr<Model>& model, const tinygltf::Accessor& accessor) {
    const auto values = ReadAccessor(*model, accessor);
    std::vector<float> result;
    result.reserve(values.size());
    for (double value : values) {
      result.emplace_back(static_cast<float>(value));
    }
    return result;
  }

  void ParseModel(const std::unique_ptr<Model>& model) {
    CreateDefaultTexture(model);
    for (const auto& material : model->gltfModel.materials) {
      LoadMaterial(model, material);
    }
    LoadMaterial(model, tinygltf::Material{});
    const glm::mat4 identity{1.F};
    model->identitySkin = CreateStorageBuffer(
      &identity, sizeof(identity), m_context->descriptorSetLayouts.at("uniform_animation_skin")->descriptorSetLayout);
    model->attributes.activeScene = model->gltfModel.defaultScene >= 0 ? model->gltfModel.defaultScene : 0;
    for (const auto& animation : model->gltfModel.animations) {
      auto temp_animation = std::make_unique<Animation>();

      for (const auto& sampler : animation.samplers) {
        auto temp_sampler = std::make_unique<AnimationSampler>();
        temp_sampler->interpolation = sampler.interpolation;
        if (sampler.input >= 0) {
          const auto& accessor = model->gltfModel.accessors[sampler.input];
          temp_sampler->input = ParseAnimationBuffer(model, accessor);
        }
        if (sampler.output >= 0) {
          const auto& accessor = model->gltfModel.accessors[sampler.output];
          temp_sampler->output = ParseAnimationBuffer(model, accessor);
        }

        temp_animation->samplers.emplace_back(std::move(temp_sampler));
      }

      for (const auto& channel : animation.channels) {
        auto temp_channel = std::make_unique<AnimationChannel>();
        temp_channel->sampler = channel.sampler;
        temp_channel->target = {channel.target_node, channel.target_path};

        temp_animation->channels.emplace_back(std::move(temp_channel));
      }

      model->animations.emplace_back(std::move(temp_animation));
    }

    // 节点索引全局有效；父子关系建立后再初始化模型矩阵，支持跨根节点的骨骼引用。
    for (size_t i = 0; i < model->gltfModel.nodes.size(); ++i) {
      auto node = std::make_unique<Node>();
      node->index = static_cast<int>(i);
      model->nodes.emplace_back(std::move(node));
    }
    for (size_t i = 0; i < model->nodes.size(); ++i) {
      LoadNode(model, static_cast<int>(i));
    }
    for (const auto& node : model->nodes) {
      for (int child : node->children) {
        model->nodes.at(child)->parent = node.get();
      }
    }
    for (const auto& node : model->nodes) {
      if (node->mesh) {
        node->modelUniform = CreateUniforms(NodeUniform{.model = GetNodeMatrix(node.get())},
                                            m_context->descriptorSetLayouts.at("uniform_model")->descriptorSetLayout);
      }
    }

    uint32_t imgui_scene{0};
    for (const auto& scene : model->gltfModel.scenes) {
      model->attributes.infomation += std::format("Scene {} : {}\n", imgui_scene++, scene.name);

      auto temp_scene = std::make_unique<Scene>();
      temp_scene->name = scene.name;

      temp_scene->nodes = scene.nodes;

      model->scenes.emplace_back(std::move(temp_scene));
    }
  }

  void LoadNode(const std::unique_ptr<Model>& model, int node_index) {
    const tinygltf::Node& node = model->gltfModel.nodes[node_index];
    model->attributes.infomation += std::format("  Node {} :\n", node.name);

    auto& temp_node = model->nodes.at(node_index);
    temp_node->name = node.name;
    temp_node->index = node_index;
    temp_node->children = node.children;
    const auto& weights = !node.weights.empty() ? node.weights
                          : node.mesh >= 0      ? model->gltfModel.meshes.at(node.mesh).weights
                                                : node.weights;
    for (size_t i = 0; i < std::min(weights.size(), size_t{2}); ++i) {
      temp_node->morphWeights[static_cast<int>(i)] = static_cast<float>(weights[i]);
    }

    if (node.matrix.size() == 16) {
      temp_node->matrices["matrix"] = glm::make_mat4(node.matrix.data());
    } else {
      if (node.translation.size() == 3) {
        temp_node->matrices["translation"] =
          glm::translate(glm::mat4(1.F), glm::vec3(glm::make_vec3(node.translation.data())));
      }
      if (node.rotation.size() == 4) {
        temp_node->matrices["rotation"] = glm::toMat4(glm::make_quat(node.rotation.data()));
      }
      if (node.scale.size() == 3) {
        temp_node->matrices["scale"] = glm::scale(glm::mat4(1.F), glm::vec3(glm::make_vec3(node.scale.data())));
      }
    }

    if (node.skin >= 0) {
      auto temp_skin = std::make_unique<Skin>();

      const tinygltf::Skin& skin = model->gltfModel.skins[node.skin];
      if (skin.joints.empty()) {
        throw std::runtime_error("A glTF skin must contain joints");
      }
      temp_skin->joints = skin.joints;
      temp_skin->inverseBindMatrices.resize(skin.joints.size(), glm::mat4(1.F));

      if (skin.inverseBindMatrices >= 0) {
        const auto& accessor = model->gltfModel.accessors[skin.inverseBindMatrices];
        const auto values = ReadAccessor(*model, accessor);
        if (values.size() < skin.joints.size() * 16) {
          throw std::runtime_error("Insufficient inverse bind matrices");
        }
        for (size_t i = 0; i < skin.joints.size(); ++i) {
          temp_skin->inverseBindMatrices[i] = glm::make_mat4(values.data() + i * 16);
        }
      }

      temp_skin->matrix =
        CreateStorageBuffer(temp_skin->inverseBindMatrices.data(), sizeof(glm::mat4) * skin.joints.size(),
                            m_context->descriptorSetLayouts.at("uniform_animation_skin")->descriptorSetLayout);
      temp_node->skin = std::move(temp_skin);
    }

    if (node.mesh >= 0) {
      temp_node->mesh = std::make_unique<Mesh>();
      ParseMesh(model, temp_node->mesh, model->gltfModel.meshes[node.mesh]);
    }

    if (node.camera >= 0) {
      const auto& camera = model->gltfModel.cameras[node.camera];

      temp_node->camera = std::make_unique<Camera>();
      temp_node->camera->name = camera.name;
      temp_node->camera->type = camera.type;
      temp_node->camera->perspective = camera.perspective;
      temp_node->camera->orthographic = camera.orthographic;
    }
  }

  std::string LoadImage(const std::unique_ptr<Model>& model, int image_index, bool srgb) {
    const auto key = std::to_string(image_index) + (srgb ? "/srgb" : "/linear");
    if (model->images.contains(key)) {
      return key;
    }
    const auto& source = model->gltfModel.images.at(image_index);
    if (source.as_is || source.width <= 0 || source.height <= 0 || source.component < 1 || source.component > 4 ||
        (source.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
         source.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)) {
      throw std::runtime_error("Only decoded 8/16-bit glTF images are supported");
    }
    const size_t pixels = static_cast<size_t>(source.width) * source.height;
    const size_t component_bytes = source.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2 : 1;
    if (source.image.size() < pixels * source.component * component_bytes) {
      throw std::runtime_error("Incomplete glTF image data");
    }
    // 使用普遍支持的 RGBA8，颜色贴图与数据贴图各自缓存，不能按图片索引混用色彩空间。
    std::vector<uint8_t> rgba(pixels * 4, 255);
    for (size_t i = 0; i < pixels; ++i) {
      auto channel = [&](int c) {
        const auto* data = source.image.data() + (i * source.component + c) * component_bytes;
        if (component_bytes == 1) {
          return *data;
        }
        uint16_t value{};
        std::memcpy(&value, data, sizeof(value));
        return static_cast<uint8_t>((static_cast<uint32_t>(value) + 128U) / 257U);
      };
      for (int c = 0; c < 3; ++c) {
        rgba[i * 4 + c] = channel(source.component < 3 ? 0 : c);
      }
      if (source.component == 2 || source.component == 4) {
        rgba[i * 4 + 3] = channel(source.component - 1);
      }
    }
    auto image = CreateTextureImage(rgba.data(), rgba.size(), static_cast<uint32_t>(source.width),
                                    static_cast<uint32_t>(source.height),
                                    srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM);
    image->name = source.name;
    model->images.try_emplace(key, std::move(image));
    return key;
  }

  std::string LoadSampler(const std::unique_ptr<Model>& model, int sampler_index) {
    const auto key = std::to_string(sampler_index);
    if (!model->samplers.contains(key)) {
      auto sampler = std::make_unique<Sampler>();
      sampler->sampler =
        CreateTextureSampler(sampler_index >= 0 ? &model->gltfModel.samplers.at(sampler_index) : nullptr);
      model->samplers.try_emplace(key, std::move(sampler));
    }
    return key;
  }

  std::string LoadTexture(const std::unique_ptr<Model>& model, int texture_index, bool srgb) {
    const auto key = std::to_string(texture_index) + (srgb ? "/srgb" : "/linear");
    if (!model->textures.contains(key)) {
      const auto& source = model->gltfModel.textures.at(texture_index);
      auto texture = std::make_unique<Texture>();
      texture->image = LoadImage(model, source.source, srgb);
      texture->sampler = LoadSampler(model, source.sampler);
      model->textures.try_emplace(key, std::move(texture));
    }
    return key;
  }

  void CreateDefaultTexture(const std::unique_ptr<Model>& model) {
    constexpr std::array<uint8_t, 4> white{255, 255, 255, 255};
    model->images.try_emplace("white", CreateTextureImage(white.data(), white.size(), 1, 1, VK_FORMAT_R8G8B8A8_UNORM));
    auto texture = std::make_unique<Texture>();
    texture->image = "white";
    texture->sampler = LoadSampler(model, -1);
    model->textures.try_emplace("white", std::move(texture));
  }

  void LoadMaterial(const std::unique_ptr<Model>& model, const tinygltf::Material& source) {
    auto material = std::make_unique<Material>();
    material->name = source.name.empty() ? "Default Material" : source.name;
    auto& data = material->original;
    const auto& pbr = source.pbrMetallicRoughness;
    data.baseColor = glm::vec4(glm::make_vec4(pbr.baseColorFactor.data()));
    data.emissive = glm::vec4(glm::vec3(glm::make_vec3(source.emissiveFactor.data())), 0.F);
    data.surface = {static_cast<float>(pbr.roughnessFactor), static_cast<float>(pbr.metallicFactor),
                    static_cast<float>(source.normalTexture.scale),
                    static_cast<float>(source.occlusionTexture.strength)};
    data.options.w = source.alphaMode == "BLEND" ? 2 : source.alphaMode == "MASK" ? 1 : 0;
    data.alphaSettings.x = static_cast<float>(source.alphaCutoff);
    data.materialOptions.y = source.extensions.contains("KHR_materials_unlit") ? 1 : 0;
    data.materialOptions.z = source.doubleSided ? 1 : 0;
    material->textures.fill("white");
    auto load = [&](size_t slot, int index, int uv, bool srgb) {
      if (index < 0) {
        return;
      }
      if (uv < 0 || uv > 1) {
        throw std::runtime_error("Only TEXCOORD_0 and TEXCOORD_1 are supported");
      }
      material->textures[slot] = LoadTexture(model, index, srgb);
      data.options.z |= 1 << slot;
      if (slot < 4) {
        data.uvSets[static_cast<int>(slot)] = uv;
      } else {
        data.materialOptions.x = uv;
      }
    };
    load(0, pbr.baseColorTexture.index, pbr.baseColorTexture.texCoord, true);
    load(1, pbr.metallicRoughnessTexture.index, pbr.metallicRoughnessTexture.texCoord, false);
    load(2, source.normalTexture.index, source.normalTexture.texCoord, false);
    load(3, source.occlusionTexture.index, source.occlusionTexture.texCoord, false);
    load(4, source.emissiveTexture.index, source.emissiveTexture.texCoord, true);
    material->uniform =
      CreateUniforms(data, m_context->descriptorSetLayouts.at("uniform_material")->descriptorSetLayout);
    for (size_t frame = 0; frame < k_max_frames_in_flight; ++frame) {
      std::array<VkDescriptorImageInfo, 5> images{};
      std::array<VkWriteDescriptorSet, 5> writes{};
      for (size_t slot = 0; slot < images.size(); ++slot) {
        const auto& texture = model->textures.at(material->textures[slot]);
        images[slot] = {model->samplers.at(texture->sampler)->sampler, model->images.at(texture->image)->imageView,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[slot].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[slot].dstSet = material->uniform->descriptorSets->descriptorSets[frame];
        writes[slot].dstBinding = static_cast<uint32_t>(slot + 1);
        writes[slot].descriptorCount = 1;
        writes[slot].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[slot].pImageInfo = &images[slot];
      }
      vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    model->materials.emplace_back(std::move(material));
  }

  void ParseMesh(const std::unique_ptr<Model>& model, const std::unique_ptr<Mesh>& mesh, const tinygltf::Mesh& source) {
    mesh->name = source.name;
    const auto mesh_index = &source - model->gltfModel.meshes.data();
    for (size_t primitive_index = 0; primitive_index < source.primitives.size(); ++primitive_index) {
      const auto& input = source.primitives[primitive_index];
      if (!input.attributes.contains("POSITION")) {
        continue;
      }
      auto primitive = std::make_unique<Primitive>();
      primitive->material =
        model->materials.at(input.material >= 0 ? static_cast<size_t>(input.material) : model->materials.size() - 1)
          .get();
      const auto& position_accessor = model->gltfModel.accessors.at(input.attributes.at("POSITION"));
      const size_t count = position_accessor.count;
      if (count == 0 || position_accessor.type != TINYGLTF_TYPE_VEC3) {
        throw std::runtime_error("Invalid glTF POSITION accessor");
      }
      std::vector<Vertex> vertices(count);
      auto attribute = [&](const char* name, int components, auto assign) {
        if (!input.attributes.contains(name)) {
          return false;
        }
        const auto& accessor = model->gltfModel.accessors.at(input.attributes.at(name));
        if (accessor.count != count || tinygltf::GetNumComponentsInType(accessor.type) != components) {
          throw std::runtime_error(std::format("Invalid glTF {} accessor", name));
        }
        const auto values = ReadAccessor(*model, accessor);
        for (size_t i = 0; i < count; ++i) {
          assign(vertices[i], values.data() + i * components);
        }
        return true;
      };
      attribute("POSITION", 3, [](Vertex& v, const double* p) { v.position = glm::vec3(glm::make_vec3(p)); });
      const bool has_normals =
        attribute("NORMAL", 3, [](Vertex& v, const double* p) { v.normal = glm::vec3(glm::make_vec3(p)); });
      if (attribute("TEXCOORD_0", 2, [](Vertex& v, const double* p) { v.uv0 = glm::vec2(glm::make_vec2(p)); })) {
        primitive->uvMask |= 1;
      }
      if (attribute("TEXCOORD_1", 2, [](Vertex& v, const double* p) { v.uv1 = glm::vec2(glm::make_vec2(p)); })) {
        primitive->uvMask |= 2;
      }
      if (input.attributes.contains("COLOR_0")) {
        const auto& accessor = model->gltfModel.accessors.at(input.attributes.at("COLOR_0"));
        const int channels = tinygltf::GetNumComponentsInType(accessor.type);
        if (accessor.count != count || (channels != 3 && channels != 4)) {
          throw std::runtime_error("Invalid glTF COLOR_0 accessor");
        }
        const auto colors = ReadAccessor(*model, accessor);
        for (size_t i = 0; i < count; ++i) {
          vertices[i].color = {static_cast<float>(colors[i * channels]), static_cast<float>(colors[i * channels + 1]),
                               static_cast<float>(colors[i * channels + 2]),
                               channels == 4 ? static_cast<float>(colors[i * channels + 3]) : 1.F};
        }
        primitive->hasVertexColor = true;
      }
      const bool has_joints =
        attribute("JOINTS_0", 4, [](Vertex& v, const double* p) { v.joints = glm::vec4(glm::make_vec4(p)); });
      const bool has_weights =
        attribute("WEIGHTS_0", 4, [](Vertex& v, const double* p) { v.weights = glm::vec4(glm::make_vec4(p)); });
      primitive->hasSkin = has_joints && has_weights;
      for (auto& vertex : vertices) {
        const float total = vertex.weights.x + vertex.weights.y + vertex.weights.z + vertex.weights.w;
        vertex.weights = total > 0.F ? vertex.weights / total : glm::vec4(1.F, 0.F, 0.F, 0.F);
      }
      std::vector<uint32_t> source_indices;
      if (input.indices >= 0) {
        const auto& accessor = model->gltfModel.accessors.at(input.indices);
        for (double index : ReadAccessor(*model, accessor)) {
          if (index < 0.0 || index >= static_cast<double>(count)) {
            throw std::runtime_error("Invalid glTF vertex index");
          }
          source_indices.emplace_back(static_cast<uint32_t>(index));
        }
      } else {
        for (size_t i = 0; i < count; ++i) {
          source_indices.emplace_back(static_cast<uint32_t>(i));
        }
      }
      std::vector<uint32_t> indices;
      if (input.mode == TINYGLTF_MODE_TRIANGLES || input.mode < 0) {
        indices = std::move(source_indices);
      } else if (input.mode == TINYGLTF_MODE_TRIANGLE_STRIP || input.mode == TINYGLTF_MODE_TRIANGLE_FAN) {
        for (size_t i = 2; i < source_indices.size(); ++i) {
          const uint32_t first =
            input.mode == TINYGLTF_MODE_TRIANGLE_FAN ? source_indices[0] : source_indices[i - (i % 2 == 0 ? 2 : 1)];
          const uint32_t second = source_indices[i - (input.mode == TINYGLTF_MODE_TRIANGLE_FAN || i % 2 == 0 ? 1 : 2)];
          indices.insert(indices.end(), {first, second, source_indices[i]});
        }
      } else {
        throw std::runtime_error("The model viewer supports triangle primitives only");
      }
      if (indices.empty()) {
        continue;
      }
      if (indices.size() % 3 != 0) {
        throw std::runtime_error("Incomplete glTF triangle");
      }
      auto normals = [&](int target) {
        std::vector<glm::vec3> result(count, glm::vec3(0.F));
        auto position = [&](uint32_t i) {
          return vertices[i].position + (target == 0   ? vertices[i].morphPosition0
                                         : target == 1 ? vertices[i].morphPosition1
                                                       : glm::vec3(0.F));
        };
        for (size_t i = 0; i < indices.size(); i += 3) {
          const auto normal = glm::cross(position(indices[i + 1]) - position(indices[i]),
                                         position(indices[i + 2]) - position(indices[i]));
          result[indices[i]] += normal;
          result[indices[i + 1]] += normal;
          result[indices[i + 2]] += normal;
        }
        for (auto& normal : result) {
          normal = glm::dot(normal, normal) > 1e-12F ? glm::normalize(normal) : glm::vec3(0.F, 1.F, 0.F);
        }
        return result;
      };
      if (!has_normals) {
        const auto generated = normals(-1);
        for (size_t i = 0; i < count; ++i) {
          vertices[i].normal = generated[i];
        }
      }
      if (input.targets.size() > 2) {
        throw std::runtime_error("This sample supports up to two morph targets per primitive");
      }
      for (size_t target_index = 0; target_index < input.targets.size(); ++target_index) {
        const auto& target = input.targets[target_index];
        primitive->hasMorph = true;
        auto load_delta = [&](const char* name, auto assign) {
          if (!target.contains(name)) {
            return false;
          }
          const auto& accessor = model->gltfModel.accessors.at(target.at(name));
          if (accessor.count != count || accessor.type != TINYGLTF_TYPE_VEC3) {
            throw std::runtime_error("Invalid glTF morph accessor");
          }
          const auto values = ReadAccessor(*model, accessor);
          for (size_t i = 0; i < count; ++i) {
            assign(vertices[i], glm::vec3(glm::make_vec3(values.data() + i * 3)));
          }
          return true;
        };
        load_delta("POSITION", [&](Vertex& vertex, const glm::vec3& delta) {
          (target_index == 0 ? vertex.morphPosition0 : vertex.morphPosition1) = delta;
        });
        const bool has_delta_normal = load_delta("NORMAL", [&](Vertex& vertex, const glm::vec3& delta) {
          (target_index == 0 ? vertex.morphNormal0 : vertex.morphNormal1) = delta;
        });
        if (!has_normals && !has_delta_normal) {
          const auto generated = normals(static_cast<int>(target_index));
          for (size_t i = 0; i < count; ++i) {
            (target_index == 0 ? vertices[i].morphNormal0 : vertices[i].morphNormal1) =
              generated[i] - vertices[i].normal;
          }
        }
      }
      for (auto& vertex : vertices) {
        vertex.flags = static_cast<uint32_t>(primitive->uvMask) | (primitive->hasSkin ? 4U : 0U) |
                       (primitive->hasMorph ? 8U : 0U) | (primitive->hasVertexColor ? 16U : 0U);
        primitive->bounds.Include(vertex.position);
        primitive->morphBounds[0].Include(vertex.morphPosition0);
        primitive->morphBounds[1].Include(vertex.morphPosition1);
      }
      const auto key = std::format("mesh/{}/{}", mesh_index, primitive_index);
      primitive->vertexBuffer = key + "/vertices";
      primitive->indexBuffer = key + "/indices";
      primitive->indexCount = static_cast<uint32_t>(indices.size());
      if (!model->buffers.contains(primitive->vertexBuffer)) {
        model->buffers.try_emplace(primitive->vertexBuffer,
                                   CreateVertexBuffer(vertices.size() * sizeof(Vertex), vertices.data()));
        model->buffers.try_emplace(primitive->indexBuffer,
                                   CreateIndexBuffer(indices.size() * sizeof(uint32_t), indices.data()));
      }
      mesh->primitives.emplace_back(std::move(primitive));
    }
  }

  void DrawPrimitive(const DrawItem& item, VkCommandBuffer command_buffer) const {
    const auto& model = *item.model;
    const auto& node = *item.node;
    const auto& primitive = *item.primitive;
    const auto type = primitive.material->original.options.w == 2 ? PipelineType::Blend : PipelineType::Opaque;
    const auto& pipeline = *m_context->pipelines.at(type);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    const std::array descriptors{
      node.modelUniform->descriptorSets->descriptorSets[m_current_frame],
      primitive.material->uniform->descriptorSets->descriptorSets[m_current_frame],
      (node.skin ? node.skin->matrix : model.identitySkin)->descriptorSets->descriptorSets[m_current_frame]};
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipelineLayout, 0,
                            static_cast<uint32_t>(descriptors.size()), descriptors.data(), 0, nullptr);
    const auto viewport = GetSceneViewport();
    const float aspect = static_cast<float>(viewport.extent.width) / static_cast<float>(viewport.extent.height);
    const float distance = glm::distance(m_eye_pos, m_scene_center);
    const float near_plane = std::max(m_scene_radius * 0.0001F, distance - m_scene_radius * 1.2F);
    const float far_plane = std::max(near_plane * 2.F, distance + m_scene_radius * 1.2F);
    PushConstantVP pc{.view = glm::lookAt(m_eye_pos, m_look_at, m_view_up),
                      .proj = glm::perspective(glm::radians(m_field_of_view), aspect, near_plane, far_plane)};
    pc.proj[1][1] *= -1.F;
    vkCmdPushConstants(command_buffer, pipeline.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    const auto vertex = model.buffers.at(primitive.vertexBuffer)->buffer;
    constexpr VkDeviceSize offset{};
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex, &offset);
    vkCmdBindIndexBuffer(command_buffer, model.buffers.at(primitive.indexBuffer)->buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command_buffer, primitive.indexCount, 1, 0, 0, 0);
  }

  glm::mat4 GetNodeMatrix(const Node* node) const {
    auto matrix = node->GetLocalMatrix();
    auto parent = node->parent;
    while (parent) {
      matrix = parent->GetLocalMatrix() * matrix;
      parent = parent->parent;
    }
    return matrix;
  }

  glm::mat4 GetJointMatrix(const Model& model, const Skin& skin, size_t joint) const {
    return GetNodeMatrix(model.nodes.at(skin.joints[joint]).get()) * skin.inverseBindMatrices[joint];
  }

  void UpdateNodeJoint(const Model& model, const Node& node) const {
    if (!node.skin) {
      return;
    }
    std::vector<glm::mat4> matrices(node.skin->joints.size());
    const auto inverse_transform = glm::inverse(GetNodeMatrix(&node));
    for (size_t i = 0; i < matrices.size(); ++i) {
      matrices[i] = inverse_transform * GetJointMatrix(model, *node.skin, i);
    }
    node.skin->matrix->UpdateUniform(m_current_frame, matrices.data(), sizeof(glm::mat4) * matrices.size());
  }

  void UpdateModelAnimation(Model& model) const {
    model.attributes.animationTime = glfwGetTime() - model.attributes.animationStartTime;
    for (const auto& animation : model.animations) {
      float duration{};
      for (const auto& sampler : animation->samplers) {
        if (!sampler->input.empty()) {
          duration = std::max(duration, sampler->input.back());
        }
      }
      const float animation_time =
        duration > 0.F ? static_cast<float>(std::fmod(model.attributes.animationTime, duration)) : 0.F;
      for (const auto& channel : animation->channels) {
        const auto& target = channel->target;
        if (target.node < 0 || static_cast<size_t>(target.node) >= model.nodes.size()) {
          continue;
        }
        auto& node = *model.nodes[target.node];
        const auto& sampler = *animation->samplers.at(channel->sampler);
        const auto& input = sampler.input;
        const auto& output = sampler.output;
        if (input.empty()) {
          continue;
        }
        const float time = std::clamp(animation_time, input.front(), input.back());
        const auto upper = std::upper_bound(input.begin(), input.end(), time);
        const size_t next = std::min(static_cast<size_t>(upper - input.begin()), input.size() - 1);
        const size_t previous = next > 0 ? next - 1 : 0;
        const float interval = input[next] - input[previous];
        const float alpha = interval > 0.F ? (time - input[previous]) / interval : 0.F;
        size_t components = target.path == "rotation" ? 4 : 3;
        if (target.path == "weights") {
          components = 0;
          const int mesh_index = model.gltfModel.nodes.at(target.node).mesh;
          if (mesh_index >= 0) {
            for (const auto& primitive : model.gltfModel.meshes.at(mesh_index).primitives) {
              components = std::max(components, primitive.targets.size());
            }
          }
          if (components == 0 || components > 2) {
            continue;
          }
        }
        const bool cubic = sampler.interpolation == "CUBICSPLINE";
        const size_t stride = components * (cubic ? 3 : 1);
        if (output.size() < input.size() * stride) {
          continue;
        }
        auto value = [&](size_t component) {
          const float first = output[previous * stride + (cubic ? components : 0) + component];
          const float second = output[next * stride + (cubic ? components : 0) + component];
          if (sampler.interpolation == "STEP") {
            return time >= input[next] ? second : first;
          }
          if (cubic) {
            const float a2 = alpha * alpha;
            const float a3 = a2 * alpha;
            const float out_tangent = output[previous * stride + components * 2 + component];
            const float in_tangent = output[next * stride + component];
            return (2.F * a3 - 3.F * a2 + 1.F) * first + (a3 - 2.F * a2 + alpha) * interval * out_tangent +
                   (-2.F * a3 + 3.F * a2) * second + (a3 - a2) * interval * in_tangent;
          }
          return glm::mix(first, second, alpha);
        };
        if (target.path == "rotation") {
          glm::quat rotation{value(3), value(0), value(1), value(2)};
          if (!cubic && sampler.interpolation != "STEP") {
            rotation =
              glm::slerp(glm::make_quat(output.data() + previous * 4), glm::make_quat(output.data() + next * 4), alpha);
          }
          node.matrices["rotation"] = glm::toMat4(
            glm::dot(rotation, rotation) > 1e-12F ? glm::normalize(rotation) : glm::quat(1.F, 0.F, 0.F, 0.F));
        } else if (target.path == "translation") {
          node.matrices["translation"] = glm::translate(glm::mat4(1.F), glm::vec3(value(0), value(1), value(2)));
        } else if (target.path == "scale") {
          node.matrices["scale"] = glm::scale(glm::mat4(1.F), glm::vec3(value(0), value(1), value(2)));
        } else if (target.path == "weights" && node.mesh) {
          node.morphWeights = {value(0), components > 1 ? value(1) : 0.F};
        }
      }
    }
  }

  glm::vec3 ToLinear(const glm::vec3& srgb) const {
    glm::vec3 linear{};
    for (int i = 0; i < 3; ++i) {
      linear[i] = srgb[i] <= 0.04045F ? srgb[i] / 12.92F : std::pow((srgb[i] + 0.055F) / 1.055F, 2.4F);
    }
    return linear;
  }

  void UpdateModelUniforms(const Model& model) const {
    const auto& settings = model.attributes.display;
    for (const auto& material : model.materials) {
      auto data = material->original;
      data.options.x = static_cast<int>(settings.shading);
      data.options.y = static_cast<int>(settings.colorSource);
      // 查看器默认显示两面，原材质的单/双面设置保留供用户选择。
      if (settings.faces != FaceDisplay::Material) {
        data.materialOptions.z = settings.faces == FaceDisplay::DoubleSided ? 1 : 0;
      }
      if (!settings.respectUnlit) {
        data.materialOptions.y = 0;
      }
      data.tint = glm::vec4(ToLinear(settings.colorSource == ColorSource::Single ? settings.singleColor
                                     : settings.tintEnabled                      ? settings.tint
                                                                                 : glm::vec3(1.F)),
                            1.F);
      if (settings.overrideRoughness) {
        data.surface.x = settings.roughness;
        data.options.z |= 64;
      }
      if (settings.overrideMetallic) {
        data.surface.y = settings.metallic;
        data.options.z |= 128;
      }
      data.cameraPosition = glm::vec4(m_eye_pos, 1.F);
      data.lightSettings = {settings.lightStrength, settings.ambient, settings.exposure,
                            HasSrgbAttachment() ? 1.F : 0.F};
      material->uniform->UpdateUniform(m_current_frame, &data, sizeof(data));
    }
    for (const auto& node : model.nodes) {
      if (node->mesh) {
        NodeUniform data{.model = GetNodeMatrix(node.get()),
                         .morphWeights = glm::vec4(node->morphWeights, 0.F, 0.F),
                         .animation = {node->skin ? 1 : 0, 1, 0, 0}};
        node->modelUniform->UpdateUniform(m_current_frame, &data, sizeof(data));
      }
      UpdateNodeJoint(model, *node);
    }
  }

  void CollectDrawItems(const Model& model, int node_index, std::vector<DrawItem>& opaque,
                        std::vector<DrawItem>& transparent) const {
    const auto& node = *model.nodes.at(node_index);
    if (node.mesh) {
      const auto forward = glm::normalize(m_look_at - m_eye_pos);
      for (const auto& primitive : node.mesh->primitives) {
        const auto bounds = GetPrimitiveBounds(model, node, *primitive);
        const auto center = bounds.IsValid() ? (bounds.minimum + bounds.maximum) * 0.5F : glm::vec3(0.F);
        DrawItem item{&model, &node, primitive.get(), glm::dot(center - m_eye_pos, forward)};
        (primitive->material->original.options.w == 2 ? transparent : opaque).emplace_back(item);
      }
    }
    for (int child : node.children) {
      CollectDrawItems(model, child, opaque, transparent);
    }
  }

  std::unique_ptr<Image> CreateTextureImage(const void* data_pointer, size_t data_size, uint32_t width, uint32_t height,
                                            VkFormat format) {
    auto image = std::make_unique<Image>();

    VkBuffer staging_buffer{};
    VkDeviceMemory staging_buffer_memory{};

    CreateBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer,
                 staging_buffer_memory);

    void* data{nullptr};
    vkMapMemory(m_device, staging_buffer_memory, 0, data_size, 0, &data);
    std::memcpy(data, data_pointer, data_size);
    vkUnmapMemory(m_device, staging_buffer_memory);

    CreateImage(width, height, format, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                image->image, image->imageMemory);

    TransitionImageLayout(image->image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(staging_buffer, image->image, width, height);
    TransitionImageLayout(image->image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(m_device, staging_buffer, nullptr);
    vkFreeMemory(m_device, staging_buffer_memory, nullptr);

    image->imageView = CreateImageView(image->image, format, VK_IMAGE_ASPECT_COLOR_BIT);

    return image;
  }

  VkSampler CreateTextureSampler(const tinygltf::Sampler* source = nullptr) {
    VkSampler sampler{};

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy = 1.0;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.F;
    sampler_info.minLod = 0.F;
    sampler_info.maxLod = 0.F;
    if (source) {
      sampler_info.magFilter =
        source->magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
      const bool nearest = source->minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST ||
                           source->minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
                           source->minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR;
      sampler_info.minFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
      auto address_mode = [](int wrap) {
        switch (wrap) {
          case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
          case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
          default:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
      };
      sampler_info.addressModeU = address_mode(source->wrapS);
      sampler_info.addressModeV = address_mode(source->wrapT);
    }

    if (VK_SUCCESS != vkCreateSampler(m_device, &sampler_info, nullptr, &sampler)) {
      throw std::runtime_error("failed to create texture sampler");
    }

    return sampler;
  }

  void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t widht, uint32_t height) {
    VkCommandBuffer command_buffer = BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {widht, height, 1};

    vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(command_buffer);
  }

  void TransitionImageLayout(VkImage image, VkFormat /*format*/, VkImageLayout old_layout, VkImageLayout new_layout) {
    VkCommandBuffer command_buffer = BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;

    // 指定变换屏障掩码
    VkPipelineStageFlags source_stage{};
    VkPipelineStageFlags destination_stage{};
    if (VK_IMAGE_LAYOUT_UNDEFINED == old_layout && VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL == new_layout) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

      source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL == old_layout &&
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL == new_layout) {
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
      throw std::invalid_argument("unsupported layout transition");
    }

    vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(command_buffer);
  }

  VkCommandBuffer BeginSingleTimeCommands() const noexcept {
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = m_command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer{};
    vkAllocateCommandBuffers(m_device, &alloc_info, &command_buffer);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;  // 指定如何使用这个指令缓冲

    vkBeginCommandBuffer(command_buffer, &begin_info);
    return command_buffer;
  }

  void EndSingleTimeCommands(VkCommandBuffer command_buffer) const noexcept {
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(m_graphics_queue, 1, &submit_info, nullptr);
    vkQueueWaitIdle(m_graphics_queue);

    vkFreeCommandBuffers(m_device, m_command_pool, 1, &command_buffer);
  }

  /// @brief 创建顶点缓冲
  std::unique_ptr<Buffer> CreateVertexBuffer(const VkDeviceSize buffer_size, const void* data_pointer) {
    return CreateDrawableBuffer(buffer_size, data_pointer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  }

  /// @brief 创建索引缓冲
  std::unique_ptr<Buffer> CreateIndexBuffer(const VkDeviceSize buffer_size, const void* data_pointer) {
    return CreateDrawableBuffer(buffer_size, data_pointer, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  }

  std::unique_ptr<Buffer> CreateDrawableBuffer(const VkDeviceSize buffer_size, const void* data_pointer,
                                               VkBufferUsageFlagBits usage) {
    auto buffer = std::make_unique<Buffer>();

    VkBuffer staging_buffer{};
    VkDeviceMemory staging_buffer_memory{};

    CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer,
                 staging_buffer_memory);

    void* data{nullptr};
    vkMapMemory(m_device, staging_buffer_memory, 0, buffer_size, 0, &data);
    std::memcpy(data, data_pointer, buffer_size);
    vkUnmapMemory(m_device, staging_buffer_memory);

    CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 buffer->buffer, buffer->buffer_memory);

    CopyBuffer(staging_buffer, buffer->buffer, buffer_size);

    vkDestroyBuffer(m_device, staging_buffer, nullptr);
    vkFreeMemory(m_device, staging_buffer_memory, nullptr);

    return buffer;
  }

  void ImguiNode(const Model& model, int node_index) {
    const auto& node = model.nodes.at(node_index);
    const auto label =
      std::format("Node {}: {}###node_{}", node_index, node->name.empty() ? "Unnamed" : node->name, node_index);
    if (!ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
      return;
    }
    if (node->mesh && ImGui::TreeNode("Mesh")) {
      viewer_ui::TextWrapped(node->mesh->name.empty() ? "Unnamed Mesh" : node->mesh->name);
      for (size_t i = 0; i < node->mesh->primitives.size(); ++i) {
        const auto& primitive = node->mesh->primitives[i];
        if (ImGui::TreeNodeEx(std::format("Primitive {}", i).c_str(), ImGuiTreeNodeFlags_None)) {
          viewer_ui::TextWrapped(std::format("Material: {}", primitive->material->name));
          viewer_ui::Text(std::format("Triangles: {}", primitive->indexCount / 3));
          viewer_ui::Text(std::format("UV0: {} | UV1: {}", (primitive->uvMask & 1) ? "yes" : "no",
                                      (primitive->uvMask & 2) ? "yes" : "no"));
          viewer_ui::Text(std::format("Vertex color: {}", primitive->hasVertexColor ? "yes" : "no"));
          viewer_ui::Text(
            std::format("Skin: {} | Morph: {}", primitive->hasSkin ? "yes" : "no", primitive->hasMorph ? "yes" : "no"));
          ImGui::TreePop();
        }
      }
      ImGui::TreePop();
    }
    if (node->camera && ImGui::TreeNode("Camera")) {
      const auto& camera = *node->camera;
      viewer_ui::TextWrapped(std::format("Name: {}", camera.name.empty() ? "Unnamed" : camera.name));
      viewer_ui::Text(std::format("Type: {}", camera.type));
      const auto position = glm::vec3(GetNodeMatrix(node.get())[3]);
      viewer_ui::Text(std::format("Position: {:.3f}, {:.3f}, {:.3f}", position.x, position.y, position.z));
      if (camera.type == "perspective") {
        viewer_ui::Text(std::format("Vertical FOV: {:.1f} deg", glm::degrees(camera.perspective.yfov)));
        viewer_ui::Text(std::format("Aspect ratio: {:.3f} (0 = viewport)", camera.perspective.aspectRatio));
        viewer_ui::Text(std::format("Near: {:.4f}", camera.perspective.znear));
        if (camera.perspective.zfar > 0.0) {
          viewer_ui::Text(std::format("Far: {:.4f}", camera.perspective.zfar));
        } else {
          viewer_ui::Text("Far: infinite");
        }
      } else if (camera.type == "orthographic") {
        viewer_ui::Text(
          std::format("X / Y magnification: {:.3f} / {:.3f}", camera.orthographic.xmag, camera.orthographic.ymag));
        viewer_ui::Text(
          std::format("Near / Far: {:.4f} / {:.4f}", camera.orthographic.znear, camera.orthographic.zfar));
      }
      ImGui::TreePop();
    }
    for (int child : node->children) {
      ImguiNode(model, child);
    }
    ImGui::TreePop();
  }

  void ImguiModelDisplay(Model& model) {
    auto& settings = model.attributes.display;
    viewer_ui::TextDisabled("Quick view");
    if (ImGui::Button("Material")) {
      settings = DisplaySettings{};
    }
    ImGui::SameLine();
    if (ImGui::Button("Solid")) {
      settings.shading = ShadingMode::Studio;
      settings.colorSource = ColorSource::Single;
    }
    ImGui::SameLine();
    if (ImGui::Button("Texture")) {
      settings.shading = ShadingMode::Unlit;
      settings.colorSource = ColorSource::Texture;
      settings.tintEnabled = false;
    }
    ImGui::SetNextItemWidth(-80.F);
    int shading = static_cast<int>(settings.shading);
    if (ImGui::Combo("Lighting", &shading, "Off (Unlit)\0Studio\0PBR\0")) {
      settings.shading = static_cast<ShadingMode>(shading);
    }
    ImGui::SetNextItemWidth(-80.F);
    int source = static_cast<int>(settings.colorSource);
    if (ImGui::Combo("Color", &source, "Material\0Texture\0Single\0Vertex\0")) {
      settings.colorSource = static_cast<ColorSource>(source);
    }
    if (ImGui::IsItemHovered()) {
      viewer_ui::SetTooltip(
        "Material: original factors, textures and vertex colors.\n"
        "Texture: base-color image; missing images use material color.\n"
        "Single: one color for the whole model.\n"
        "Vertex: COLOR_0; missing colors use material color.");
    }
    ImGui::SetNextItemWidth(-80.F);
    int faces = static_cast<int>(settings.faces);
    if (ImGui::Combo("Faces", &faces, "Double-sided\0Material\0Single-sided\0")) {
      settings.faces = static_cast<FaceDisplay>(faces);
    }
    bool has_texture{};
    bool has_vertex_color{};
    for (const auto& node : model.nodes) {
      if (node->mesh) {
        for (const auto& primitive : node->mesh->primitives) {
          const auto& material = primitive->material->original;
          has_texture |= (material.options.z & 1) != 0 && (primitive->uvMask & (1 << material.uvSets.x)) != 0;
          has_vertex_color |= primitive->hasVertexColor;
        }
      }
    }
    if ((settings.colorSource == ColorSource::Texture && !has_texture) ||
        (settings.colorSource == ColorSource::Vertex && !has_vertex_color)) {
      viewer_ui::TextWrapped("No matching colors available; using material colors.");
    }
    if (settings.colorSource == ColorSource::Single) {
      ImGui::ColorEdit3("Color##Single", glm::value_ptr(settings.singleColor));
    } else {
      ImGui::Checkbox("Tint model", &settings.tintEnabled);
      if (settings.tintEnabled) {
        ImGui::ColorEdit3("Tint", glm::value_ptr(settings.tint));
      }
    }
    ImGui::Separator();
    viewer_ui::TextDisabled("PBR material overrides");
    ImGui::BeginDisabled(settings.shading != ShadingMode::PBR);
    ImGui::Checkbox("Override roughness", &settings.overrideRoughness);
    if (settings.overrideRoughness) {
      ImGui::SetNextItemWidth(-80.F);
      ImGui::SliderFloat("Roughness", &settings.roughness, 0.045F, 1.F, "%.3f");
    }
    ImGui::Checkbox("Override metallic", &settings.overrideMetallic);
    if (settings.overrideMetallic) {
      ImGui::SetNextItemWidth(-80.F);
      ImGui::SliderFloat("Metallic", &settings.metallic, 0.F, 1.F, "%.2f");
    }
    if (std::any_of(model.materials.begin(), model.materials.end(),
                    [](const auto& material) { return material->original.materialOptions.y != 0; })) {
      ImGui::Checkbox("Respect unlit materials", &settings.respectUnlit);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      viewer_ui::SetTooltip("Unchecked overrides preserve each material's original factors and maps.");
    }
    if (ImGui::TreeNode("Studio lighting")) {
      ImGui::BeginDisabled(settings.shading == ShadingMode::Unlit);
      ImGui::SetNextItemWidth(-80.F);
      ImGui::SliderFloat("Strength", &settings.lightStrength, 0.F, 10.F, "%.2f");
      ImGui::SetNextItemWidth(-80.F);
      ImGui::SliderFloat("Exposure", &settings.exposure, 0.1F, 4.F, "%.2f");
      ImGui::SetNextItemWidth(-80.F);
      ImGui::SliderFloat("Ambient", &settings.ambient, 0.F, 0.5F, "%.3f");
      ImGui::EndDisabled();
      ImGui::TreePop();
    }
    if (ImGui::Button("Restore Materials", ImVec2(-1.F, 0.F))) {
      settings = DisplaySettings{};
    }
  }

  bool HasSrgbAttachment() const {
    return m_swap_chain_image_format == VK_FORMAT_B8G8R8A8_SRGB || m_swap_chain_image_format == VK_FORMAT_R8G8B8A8_SRGB;
  }

  void PrepareImGuiColors() const {
    if (!HasSrgbAttachment()) {
      return;
    }
    auto* data = ImGui::GetDrawData();
    for (int list = 0; list < data->CmdListsCount; ++list) {
      for (auto& vertex : data->CmdLists[list]->VtxBuffer) {
        auto color = ImGui::ColorConvertU32ToFloat4(vertex.col);
        const auto linear = ToLinear(glm::vec3(color.x, color.y, color.z));
        color.x = linear.x;
        color.y = linear.y;
        color.z = linear.z;
        vertex.col = ImGui::ColorConvertFloat4ToU32(color);
      }
    }
  }

  /// @brief 添加需要渲染的 GUI 控件，左侧面板和右侧模型视口分别占用空间。
  void PrepareImGui() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const auto& io = ImGui::GetIO();
    const float available_width = std::max(io.DisplaySize.x - 20.F, 1.F);
    const float available_height = std::max(io.DisplaySize.y - 20.F, 1.F);
    ImGui::SetNextWindowPos(ImVec2(10.F, 10.F));
    ImGui::SetNextWindowSize(ImVec2(340.F, available_height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
      ImVec2(std::min(240.F, available_width), std::min(160.F, available_height)),
      ImVec2(std::min(available_width, std::max(240.F, io.DisplaySize.x * 0.55F)), available_height));
    const bool expanded = ImGui::Begin("Model Viewer", nullptr, ImGuiWindowFlags_NoMove);
    m_controls_width = ImGui::IsWindowCollapsed() ? 0.F : ImGui::GetWindowPos().x + ImGui::GetWindowSize().x + 10.F;
    if (expanded) {
      if (ImGui::Button("Reset Camera (R)", ImVec2(-1.F, 30.F))) {
        m_reset_camera_requested = true;
        m_focus_model = nullptr;
      }
      if (ImGui::CollapsingHeader("Camera & navigation")) {
        ImGui::TextUnformatted("Left drag: pan | Right drag: orbit");
        ImGui::TextUnformatted("Wheel: zoom | R: frame visible models");
        ImGui::SetNextItemWidth(-65.F);
        if (ImGui::SliderFloat("FOV", &m_field_of_view, 15.F, 90.F, "%.0f deg")) {
          m_reset_camera_requested = true;
          m_focus_model = nullptr;
        }
        viewer_ui::TextDisabled(std::format("Distance: {:.3f}", glm::distance(m_eye_pos, m_look_at)));
      }
      ImGui::Separator();
      if (ImGui::Button("Show All")) {
        for (const auto& [_, model] : m_models) {
          model->attributes.visibility = true;
        }
        m_reset_camera_requested = true;
        m_focus_model = nullptr;
      }
      ImGui::SameLine();
      if (ImGui::Button("Hide All")) {
        for (const auto& [_, model] : m_models) {
          model->attributes.visibility = false;
        }
      }
      size_t visible{};
      for (const auto& [_, model] : m_models) {
        visible += model->attributes.visibility ? 1 : 0;
      }
      viewer_ui::TextDisabled(std::format("{} / {} models visible", visible, m_models.size()));
      ImGui::BeginChild("Models", ImVec2(0.F, 0.F), true);
      for (const auto& [name, model] : m_models) {
        const auto filename = std::filesystem::path(name).filename().string();
        ImGui::PushID(name.c_str());
        if (ImGui::Checkbox("##Visible", &model->attributes.visibility)) {
          m_reset_camera_requested = true;
          m_focus_model = nullptr;
        }
        ImGui::SameLine();
        const bool open = ImGui::TreeNodeEx(filename.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        if (ImGui::IsItemHovered()) {
          viewer_ui::SetTooltip(name);
        }
        if (open) {
          if (ImGui::Button("Frame")) {
            model->attributes.visibility = true;
            m_reset_camera_requested = true;
            m_focus_model = model.get();
          }
          ImGui::SameLine();
          if (ImGui::Button("Solo")) {
            for (const auto& [_, other] : m_models) {
              other->attributes.visibility = other.get() == model.get();
            }
            m_reset_camera_requested = true;
            m_focus_model = model.get();
          }
          ImGui::BeginDisabled(model->animations.empty());
          if (ImGui::Checkbox("Play animation", &model->attributes.animation) && model->attributes.animation) {
            model->attributes.animationStartTime = glfwGetTime() - model->attributes.animationTime;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Restart")) {
            model->attributes.animationTime = 0.0;
            model->attributes.animationStartTime = glfwGetTime();
            UpdateModelAnimation(*model);
          }
          ImGui::EndDisabled();
          if (model->scenes.size() > 1) {
            const auto& current = model->scenes.at(model->attributes.activeScene);
            ImGui::SetNextItemWidth(-80.F);
            if (ImGui::BeginCombo("Scene", current->name.empty() ? "Unnamed Scene" : current->name.c_str())) {
              for (size_t i = 0; i < model->scenes.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                const auto& scene = model->scenes[i];
                if (ImGui::Selectable(scene->name.empty() ? "Unnamed Scene" : scene->name.c_str(),
                                      static_cast<int>(i) == model->attributes.activeScene)) {
                  model->attributes.activeScene = static_cast<int>(i);
                  m_reset_camera_requested = true;
                }
                ImGui::PopID();
              }
              ImGui::EndCombo();
            }
          }
          if (ImGui::BeginTabBar("Model settings")) {
            if (ImGui::BeginTabItem("Appearance")) {
              ImguiModelDisplay(*model);
              ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Details")) {
              viewer_ui::TextDisabled(std::format("{} materials", model->materials.size() - 1));
              if (!model->scenes.empty()) {
                for (int node : model->scenes.at(model->attributes.activeScene)->nodes) {
                  ImguiNode(*model, node);
                }
              }
              ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
          }
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      ImGui::EndChild();
    }
    ImGui::End();
    for (const auto& [_, model] : m_models) {
      if (model->attributes.visibility && model->attributes.animation) {
        UpdateModelAnimation(*model);
      }
    }
    UpdateSceneBounds();
    UpdateCameraInput();
    ImGui::Render();
    PrepareImGuiColors();
  }

  /// @brief 创建 Vulkan 实例
  void CreateInstance() {
    if (k_enable_validation_layers && !CheckValidationLayerSupport()) {
      throw std::runtime_error("validation layers requested, but not available");
    }

    // 应用程序的信息，这些信息可能会作为驱动程序的优化依据，让驱动做一些特殊的优化
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pNext = nullptr;
    app_info.pApplicationName = "Hello Triangle";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    // 指定驱动程序需要使用的全局扩展和校验层，全局是指对整个应用程序都有效，而不仅仅是某一个设备
    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

    // 指定需要的全局扩展
    auto required_extensions = GetRequiredExtensions();
    create_info.enabledExtensionCount = static_cast<uint32_t>(required_extensions.size());
    create_info.ppEnabledExtensionNames = required_extensions.data();

    // 指定全局校验层
    if (k_enable_validation_layers) {
      create_info.enabledLayerCount = static_cast<uint32_t>(k_validation_layers.size());
      create_info.ppEnabledLayerNames = k_validation_layers.data();

      VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {};
      PopulateDebugMessengerCreateInfo(debug_create_info);

      create_info.pNext = &debug_create_info;
    } else {
      create_info.enabledLayerCount = 0;
      create_info.pNext = nullptr;
    }

    // 创建 Vulkan 实例，用来初始化 Vulkan 库
    // 1.包含创建信息的结构体指针
    // 2.自定义的分配器回调函数
    // 3.指向实例句柄存储位置的指针
    if (VK_SUCCESS != vkCreateInstance(&create_info, nullptr, &m_instance)) {
      throw std::runtime_error("failed to create instance");
    }
  }

  /// @brief 检查需要开启的校验层是否被支持
  /// @return
  bool CheckValidationLayerSupport() const noexcept {
    // 获取所有可用的校验层列表
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
    std::cout << "-------------------------------------------\n"
              << "All available layers:\n";
    for (const auto& layer : available_layers) {
      std::cout << static_cast<const char*>(layer.layerName) << '\n';
    }

    // 检查需要开启的校验层是否可以在所有可用的校验层列表中找到
    for (const char* layer_name : k_validation_layers) {
      bool layer_found{false};

      for (const auto& layer_properties : available_layers) {
        if (0 == std::strcmp(layer_name, static_cast<const char*>(layer_properties.layerName))) {
          layer_found = true;
          break;
        }
      }

      if (!layer_found) {
        return false;
      }
    }

    return true;
  }

  /// @brief 获取所有需要开启的扩展
  /// @return
  std::vector<const char*> GetRequiredExtensions() const noexcept {
    // 获取 Vulkan 支持的所有扩展
    uint32_t extension_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extensions(extension_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());
    std::cout << "-------------------------------------------\n"
              << "All supported extensions:\n";
    for (const auto& e : extensions) {
      std::cout << static_cast<const char*>(e.extensionName) << '\n';
    }

    // Vulkan 是一个与平台无关的 API ，所以需要一个和窗口系统交互的扩展
    // 使用 glfw 获取这个扩展
    uint32_t glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    std::cout << "-------------------------------------------\n"
              << "GLFW extensions:\n";
    for (size_t i = 0; i < glfw_extension_count; ++i) {
      std::cout << glfw_extensions[i] << '\n';
    }

    // 将需要开启的所有扩展添加到列表并返回
    std::vector<const char*> required_extensions(glfw_extensions, glfw_extensions + glfw_extension_count);
    if (k_enable_validation_layers) {
      // 根据需要开启调试报告相关的扩展
      required_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return required_extensions;
  }

  /// @brief 设置回调函数来接受调试信息
  void SetupDebugCallback() {
    if (!k_enable_validation_layers) {
      return;
    }

    VkDebugUtilsMessengerCreateInfoEXT create_info = {};
    PopulateDebugMessengerCreateInfo(create_info);

    if (VK_SUCCESS != CreateDebugUtilsMessengerEXT(m_instance, &create_info, nullptr, &m_debug_messenger)) {
      throw std::runtime_error("failed to set up debug callback");
    }
  }

  /// @brief 选择一个满足需求的物理设备（显卡）
  /// @details 可以选择任意数量的显卡并同时使用它们
  void PickPhysicalDevice() {
    // 获取支持 Vulkan 的显卡数量
    uint32_t device_count{0};
    vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr);

    if (0 == device_count) {
      throw std::runtime_error("failed to find GPUs with Vulkan support");
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data());

    for (const auto& device : devices) {
      if (IsDeviceSuitable(device)) {
        m_physical_device = device;
        break;
      }
    }

    if (nullptr == m_physical_device) {
      throw std::runtime_error("failed to find a suitable GPU");
    }
  }

  /// @brief 检查显卡是否满足需求
  /// @param device
  /// @return
  bool IsDeviceSuitable(VkPhysicalDevice device) const noexcept {
    // 获取基本的设置属性，name、type以及Vulkan版本等等
    // VkPhysicalDeviceProperties deviceProperties;
    // vkGetPhysicalDeviceProperties(device, &deviceProperties);

    // 获取对纹理的压缩、64位浮点数和多视图渲染等可选功能的支持
    // VkPhysicalDeviceFeatures deviceFeatures;
    // vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    auto indices = FindQueueFamilies(device);
    auto extensions_supported = CheckDeviceExtensionSupported(device);

    bool swap_chain_adequate{false};
    if (extensions_supported) {
      auto swap_chain_support = QuerySwapChainSupport(device);
      swap_chain_adequate = !swap_chain_support.foramts.empty() && !swap_chain_support.presentModes.empty();
    }

    return indices.IsComplete() && extensions_supported && swap_chain_adequate;
  }

  /// @brief 查找满足需求的队列族
  /// @details 不同的队列族支持不同的类型的指令，例如计算、内存传输、绘图等指令
  /// @param device
  /// @return
  QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const noexcept {
    QueueFamilyIndices indices;

    // 获取物理设备支持的队列族列表
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

    for (uint32_t i = 0; i < queue_families.size(); ++i) {
      // 图形队列族
      if (queue_families.at(i).queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        indices.graphicsFamily = i;
      }

      // 呈现队列族
      VkBool32 present_support = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &present_support);

      if (present_support) {
        indices.presentFamily = i;
      }

      if (indices.IsComplete()) {
        break;
      }
    }

    return indices;
  }

  /// @brief 创建逻辑设备作为和物理设备交互的接口
  void CreateLogicalDevice() {
    auto indices = FindQueueFamilies(m_physical_device);

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    std::set<uint32_t> unique_queue_families{lvk_tidy::GetRequiredValue(indices.graphicsFamily),
                                             lvk_tidy::GetRequiredValue(indices.presentFamily)};

    // 控制指令缓存执行顺序的优先级，即使只有一个队列也要显示指定优先级，范围：[0.0, 1.0]
    float queue_priority{1.F};
    for (auto queue_family : unique_queue_families) {
      // 描述队列簇中预要申请使用的队列数量
      // 当前可用的驱动程序所提供的队列簇只允许创建少量的队列，并且很多时候没有必要创建多个队列
      // 因为可以在多个线程上创建所有命令缓冲区，然后在主线程一次性的以较低开销的调用提交队列
      VkDeviceQueueCreateInfo queue_create_info{};
      queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queue_create_info.queueFamilyIndex = queue_family;
      queue_create_info.queueCount = 1;
      queue_create_info.pQueuePriorities = &queue_priority;
      queue_create_infos.push_back(queue_create_info);
    }

    // 指定应用程序使用的设备特性（例如几何着色器）
    VkPhysicalDeviceFeatures device_features = {};

    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
    create_info.pQueueCreateInfos = queue_create_infos.data();
    create_info.pEnabledFeatures = &device_features;
    create_info.enabledExtensionCount = static_cast<uint32_t>(k_device_extensions.size());
    create_info.ppEnabledExtensionNames = k_device_extensions.data();

    // 根据需要对设备和 Vulkan 实例使用相同的校验层
    if (k_enable_validation_layers) {
      create_info.enabledLayerCount = static_cast<uint32_t>(k_validation_layers.size());
      create_info.ppEnabledLayerNames = k_validation_layers.data();
    } else {
      create_info.enabledLayerCount = 0;
    }

    // 创建逻辑设备
    if (VK_SUCCESS != vkCreateDevice(m_physical_device, &create_info, nullptr, &m_device)) {
      throw std::runtime_error("failed to create logical device");
    }

    // 获取指定队列族的队列句柄，设备队列在逻辑设备被销毁时隐式清理
    // 此处的队列簇可能相同，也就是以相同的参数调用了两次这个函数
    // 1.逻辑设备对象
    // 2.队列族索引
    // 3.队列索引，因为只创建了一个队列，所以此处使用索引0
    // 4.用来存储返回的队列句柄的内存地址
    vkGetDeviceQueue(m_device, lvk_tidy::GetRequiredValue(indices.graphicsFamily), 0, &m_graphics_queue);
    vkGetDeviceQueue(m_device, lvk_tidy::GetRequiredValue(indices.presentFamily), 0, &m_present_queue);
  }

  /// @brief 创建表面，需要在程序退出前清理
  /// @details 不同平台创建表面的方式不一样，这里使用 GLFW 统一创建
  void CreateSurface() {
    // Vulkan 不能直接与窗口系统进行交互（是一个与平台特性无关的API集合），surface是 Vulkan 与窗体系统的连接桥梁
    // 需要在instance创建之后立即创建窗体surface，因为它会影响物理设备的选择
    // 窗体surface本身对于 Vulkan 也是非强制的，不需要同 OpenGL 一样必须要创建窗体surface
    if (VK_SUCCESS != glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface)) {
      throw std::runtime_error("failed to create window surface");
    }
  }

  /// @brief 检测所需的扩展是否支持
  /// @param device
  /// @return
  bool CheckDeviceExtensionSupported(VkPhysicalDevice device) const noexcept {
    uint32_t extension_count{0};
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

    std::set<std::string> required_extensions(std::begin(k_device_extensions), std::end(k_device_extensions));

    for (const auto& extension : available_extensions) {
      required_extensions.erase(static_cast<const char*>(extension.extensionName));
    }

    // 如果为空，则支持
    return required_extensions.empty();
  }

  /// @brief 查询交换链支持的细节信息
  /// @param device
  /// @return
  SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const noexcept {
    SwapChainSupportDetails details;

    // 查询基础表面特性
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    // 查询表面支持的格式，确保集合对于所有有效的格式可扩充
    uint32_t format_count{0};
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &format_count, nullptr);
    if (0 != format_count) {
      details.foramts.resize(format_count);
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &format_count, details.foramts.data());
    }

    // 查询表面支持的呈现模式
    uint32_t present_mode_count{0};
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &present_mode_count, nullptr);
    if (0 != present_mode_count) {
      details.presentModes.resize(present_mode_count);
      vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &present_mode_count, details.presentModes.data());
    }

    return details;
  }

  /// @brief 选择合适的表面格式
  /// @param availableFormats
  /// @return
  VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available_formats) const noexcept {
    if (available_formats.size() == 1 && available_formats.front().format == VK_FORMAT_UNDEFINED) {
      return {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    // sRGB 附件使 PBR 在线性空间混合透明表面；UNORM 回退由片段着色器编码。
    for (VkFormat preferred :
         {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
      for (const auto& format : available_formats) {
        if (format.format == preferred && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
          return format;
        }
      }
    }
    return available_formats.front();
  }

  /// @brief 查找最佳的可用呈现模式，模式是非常重要的，因为它代表了在屏幕呈现图像的条件
  /// @param availablePresentModes
  /// @return
  VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& available_present_modes) const noexcept {
    // 以下4种模式可以使用
    // 1.VK_PRESENT_MODE_IMMEDIATE_KHR: 应用程序提交的图像被立即传输到屏幕呈现，这种模式可能会造成撕裂效果。
    // 2.VK_PRESENT_MODE_FIFO_KHR:
    // 交换链被看作一个队列，当显示内容需要刷新的时候，显示设备从队列的前面获取图像，并且程序将渲染完成的图像插入队列的后面。
    // 如果队列是满的程序会等待。这种规模与视频游戏的垂直同步很类似。显示设备的刷新时刻被成为“垂直中断”。
    // 3.VK_PRESENT_MODE_FIFO_RELAXED_KHR:
    // 该模式与上一个模式略有不同的地方为，如果应用程序存在延迟，即接受最后一个垂直同步信号时队列空了，
    // 将不会等待下一个垂直同步信号，而是将图像直接传送。这样做可能导致可见的撕裂效果。
    // 4.VK_PRESENT_MODE_MAILBOX_KHR:
    // 这是第二种模式的变种。当交换链队列满的时候，选择新的替换旧的图像，从而替代阻塞应用程序的情形。
    // 这种模式通常用来实现三重缓冲区，与标准的垂直同步双缓冲相比，它可以有效避免延迟带来的撕裂效果。

    VkPresentModeKHR best_mode = VK_PRESENT_MODE_FIFO_KHR;

    for (const auto& available_present_mode : available_present_modes) {
      if (VK_PRESENT_MODE_MAILBOX_KHR == available_present_mode) {
        return available_present_mode;
      }
      if (VK_PRESENT_MODE_IMMEDIATE_KHR == available_present_mode) {
        best_mode = available_present_mode;
      }
    }

    return best_mode;
  }

  /// @brief 设置交换范围，交换范围是交换链中图像的分辨率
  /// @param capabilities
  /// @return
  VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const noexcept {
    // 如果是最大值则表示允许我们自己选择对于窗口最合适的交换范围
    if (std::numeric_limits<uint32_t>::max() != capabilities.currentExtent.width) {
      return capabilities.currentExtent;
    }

    int width{0};

    int height{0};
    glfwGetFramebufferSize(m_window, &width, &height);

    // 使用GLFW窗口的大小来设置交换链中图像的分辨率
    VkExtent2D actual_extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    actual_extent.width =
      std::clamp(actual_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actual_extent.height =
      std::clamp(actual_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actual_extent;
  }

  /// @brief 创建交换链
  void CreateSwapChain() {
    SwapChainSupportDetails swap_chain_support = QuerySwapChainSupport(m_physical_device);
    VkSurfaceFormatKHR surface_format = ChooseSwapSurfaceFormat(swap_chain_support.foramts);
    VkPresentModeKHR present_mode = ChooseSwapPresentMode(swap_chain_support.presentModes);
    VkExtent2D extent = ChooseSwapExtent(swap_chain_support.capabilities);

    // 交换链中的图像数量，可以理解为队列的长度，指定运行时图像的最小数量
    // maxImageCount数值为0代表除了内存之外没有限制
    m_min_image_count = swap_chain_support.capabilities.minImageCount + 1;
    if (swap_chain_support.capabilities.maxImageCount > 0 &&
        m_min_image_count > swap_chain_support.capabilities.maxImageCount) {
      m_min_image_count = swap_chain_support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = m_surface;
    create_info.minImageCount = m_min_image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;  // 图像包含的层次，通常为1，3d图像大于1
    create_info.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;  // 指定在图像上进行怎样的操作，比如显示（颜色附件）、后处理等

    auto indices = FindQueueFamilies(m_physical_device);
    const std::array<uint32_t, 2> queue_family_indices{
      static_cast<uint32_t>(lvk_tidy::GetRequiredValue(indices.graphicsFamily)),
      static_cast<uint32_t>(lvk_tidy::GetRequiredValue(indices.presentFamily))};

    // 在多个队列族使用交换链图像的方式
    if (indices.graphicsFamily != indices.presentFamily) {
      // 图形队列和呈现队列不是同一个队列
      // VK_SHARING_MODE_CONCURRENT 表示图像可以在多个队列族间使用，不需要显式地改变图像所有权
      create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      create_info.queueFamilyIndexCount = 2;
      create_info.pQueueFamilyIndices = queue_family_indices.data();
    } else {
      // VK_SHARING_MODE_EXCLUSIVE 表示一张图像同一时间只能被一个队列族所拥有，这种模式性能最佳
      create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      create_info.queueFamilyIndexCount = 0;
      create_info.pQueueFamilyIndices = nullptr;
    }

    // 指定一个固定的变换操作（需要交换链具有supportedTransforms特性），此处不进行任何变换
    create_info.preTransform = swap_chain_support.capabilities.currentTransform;
    // 指定 alpha 通道是否被用来和窗口系统中的其他窗口进行混合操作
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // 设置呈现模式
    create_info.presentMode = present_mode;
    // 设置为true，不关心被遮蔽的像素数据，比如由于其他的窗体置于前方时或者渲染的部分内容存在于可视区域之外，
    // 除非真的需要读取这些像素获数据进行处理（设置为true回读窗口像素可能会有问题），否则可以开启裁剪获得最佳性能。
    create_info.clipped = VK_TRUE;
    // 交换链重建
    // Vulkan运行时，交换链可能在某些条件下被替换，比如窗口调整大小或者交换链需要重新分配更大的图像队列。
    // 在这种情况下，交换链实际上需要重新分配创建，并且必须在此字段中指定对旧的引用，用以回收资源
    create_info.oldSwapchain = VK_NULL_HANDLE;

    if (VK_SUCCESS != vkCreateSwapchainKHR(m_device, &create_info, nullptr, &m_swap_chain)) {
      throw std::runtime_error("failed to create swap chain");
    }

    // 获取交换链中图像，图像会在交换链销毁的同时自动清理
    // 之前给VkSwapchainCreateInfoKHR 设置了期望的图像数量，但是实际运行允许创建更多的图像数量，因此需要重新获取数量
    vkGetSwapchainImagesKHR(m_device, m_swap_chain, &m_min_image_count, nullptr);
    m_swap_chain_images.resize(m_min_image_count);
    vkGetSwapchainImagesKHR(m_device, m_swap_chain, &m_min_image_count, m_swap_chain_images.data());

    m_swap_chain_image_format = surface_format.format;
    m_swap_chain_extent = extent;
  }

  /// @brief 创建图像视图
  /// @details 任何 VkImage 对象都需要通过 VkImageView 来绑定访问，包括处于交换链中的、处于渲染管线中的
  void CreateImageViews() {
    m_swap_chain_image_views.resize(m_swap_chain_images.size());

    for (size_t i = 0; i < m_swap_chain_images.size(); ++i) {
      VkImageViewCreateInfo create_info = {};
      create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      create_info.image = m_swap_chain_images.at(i);
      create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;  // 1d 2d 3d cube纹理
      create_info.format = m_swap_chain_image_format;
      create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;  // 图像颜色通过的映射
      create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
      create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
      create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
      create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;  // 指定图像的用途，图像的哪一部分可以被访问
      create_info.subresourceRange.baseMipLevel = 0;
      create_info.subresourceRange.levelCount = 1;
      create_info.subresourceRange.baseArrayLayer = 0;
      create_info.subresourceRange.layerCount = 1;

      if (VK_SUCCESS != vkCreateImageView(m_device, &create_info, nullptr, &m_swap_chain_image_views.at(i))) {
        throw std::runtime_error("failed to create image views");
      }
    }
  }

  void CreatePipelines() {
    for (auto type : {PipelineType::Opaque, PipelineType::Blend}) {
      m_context->pipelines.try_emplace(type, CreateGraphicsPipeline(type, PROJECT_SHADER_DIR "01_06_viewer_vert.spv",
                                                                    PROJECT_SHADER_DIR "01_06_viewer_frag.spv"));
    }
  }

  /// @brief 创建图形管线
  /// @details 在 Vulkan 中几乎不允许对图形管线进行动态设置，也就意味着每一种状态都需要提前创建一个图形管线
  std::unique_ptr<Pipeline> CreateGraphicsPipeline(PipelineType pipeline_type, const std::string& vert_path,
                                                   const std::string& frag_path) {
    auto pipeline = std::make_unique<Pipeline>();

    auto vert_shader_code = ReadFile(vert_path);
    auto frag_shader_code = ReadFile(frag_path);

    VkShaderModule vert_shader_module = CreateShaderModule(vert_shader_code);
    VkShaderModule frag_shader_module = CreateShaderModule(frag_shader_code);

    VkPipelineShaderStageCreateInfo vert_shader_stage_info = {};
    vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = vert_shader_module;
    vert_shader_stage_info.pName = "main";                 // 指定调用的着色器函数，同一份代码可以实现多个着色器
    vert_shader_stage_info.pSpecializationInfo = nullptr;  // 设置着色器常量

    VkPipelineShaderStageCreateInfo frag_shader_stage_info = {};
    frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = frag_shader_module;
    frag_shader_stage_info.pName = "main";
    frag_shader_stage_info.pSpecializationInfo = nullptr;

    auto binding_descriptions = Vertex::GetBindingDescriptions();
    auto attribute_descriptions = Vertex::GetAttributeDescriptions();

    // 顶点信息
    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = static_cast<uint32_t>(binding_descriptions.size());
    vertex_input_info.pVertexBindingDescriptions = binding_descriptions.data();
    vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

    // 拓扑信息
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;  // 指定绘制的图元类型：点、线、三角形
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // 视口
    VkViewport viewport = {};
    viewport.x = 0.F;
    viewport.y = 0.F;
    viewport.width = static_cast<float>(m_swap_chain_extent.width);
    viewport.height = static_cast<float>(m_swap_chain_extent.height);
    viewport.minDepth = 0.F;  // 深度值范围，必须在 [0.F, 1.F]之间
    viewport.maxDepth = 1.F;

    // 裁剪
    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = m_swap_chain_extent;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    // 光栅化
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;  // 设置为true会禁止一切片段输出到帧缓冲
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.F;
    rasterizer.cullMode = VK_CULL_MODE_NONE;                 // 表面剔除类型，正面、背面、双面剔除
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;  // 指定顺时针的顶点序是正面还是反面
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 1.F;
    rasterizer.depthBiasClamp = 0.F;
    rasterizer.depthBiasSlopeFactor = 0.F;

    // 多重采样
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.F;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // 深度和模板测试
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;  // 是否启用深度测试
    depth_stencil.depthWriteEnable = pipeline_type == PipelineType::Blend ? VK_FALSE : VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;  // 深度值比较方式
    depth_stencil.depthBoundsTestEnable = VK_FALSE;     // 指定可选的深度范围测试
    depth_stencil.minDepthBounds = 0.F;
    depth_stencil.maxDepthBounds = 1.F;
    depth_stencil.stencilTestEnable = VK_FALSE;  // 模板测试
    depth_stencil.front = {};
    depth_stencil.back = {};

    // 颜色混合，可以对指定帧缓冲单独设置，也可以设置全局颜色混合方式
    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = pipeline_type == PipelineType::Blend ? VK_TRUE : VK_FALSE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    color_blending.blendConstants[0] = 0.0F;
    color_blending.blendConstants[1] = 0.0F;
    color_blending.blendConstants[2] = 0.0F;
    color_blending.blendConstants[3] = 0.0F;

    // 动态状态，视口大小、线宽、混合常量等
    std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    std::vector<VkPushConstantRange> push_constant_ranges{};

    VkPushConstantRange push_constant_range_vp = {};
    push_constant_range_vp.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range_vp.offset = 0;
    push_constant_range_vp.size = sizeof(PushConstantVP);

    push_constant_ranges.emplace_back(push_constant_range_vp);

    std::vector<VkDescriptorSetLayout> descriptor_set_layouts{};
    descriptor_set_layouts.emplace_back(m_context->descriptorSetLayouts.at("uniform_model")->descriptorSetLayout);

    descriptor_set_layouts.emplace_back(m_context->descriptorSetLayouts.at("uniform_material")->descriptorSetLayout);
    descriptor_set_layouts.emplace_back(
      m_context->descriptorSetLayouts.at("uniform_animation_skin")->descriptorSetLayout);

    // 管线布局，在着色器中使用 uniform 变量
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = static_cast<uint32_t>(descriptor_set_layouts.size());
    pipeline_layout_info.pSetLayouts = descriptor_set_layouts.data();
    pipeline_layout_info.pushConstantRangeCount = static_cast<uint32_t>(push_constant_ranges.size());
    pipeline_layout_info.pPushConstantRanges = push_constant_ranges.data();

    if (VK_SUCCESS != vkCreatePipelineLayout(m_device, &pipeline_layout_info, nullptr, &pipeline->pipelineLayout)) {
      throw std::runtime_error("failed to create pipeline layout");
    }

    const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{vert_shader_stage_info, frag_shader_stage_info};

    // 完整的图形管线包括：着色器阶段、固定功能状态、管线布局、渲染流程
    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline->pipelineLayout;
    pipeline_info.renderPass = m_render_pass;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = nullptr;
    pipeline_info.basePipelineIndex = -1;

    if (VK_SUCCESS !=
        vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->pipeline)) {
      throw std::runtime_error("failed to create graphics pipeline");
    }

    vkDestroyShaderModule(m_device, frag_shader_module, nullptr);
    vkDestroyShaderModule(m_device, vert_shader_module, nullptr);

    return pipeline;
  }

  /// @brief 使用着色器字节码数组创建 VkShaderModule 对象
  /// @param code
  /// @return
  VkShaderModule CreateShaderModule(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shader_module = nullptr;
    if (VK_SUCCESS != vkCreateShaderModule(m_device, &create_info, nullptr, &shader_module)) {
      throw std::runtime_error("failed to create shader module");
    }

    return shader_module;
  }

  /// @brief 创建渲染流程对象，用于渲染的帧缓冲附着，指定颜色和深度缓冲以及采样数
  void CreateRenderPass() {
    // 附着描述
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = m_swap_chain_image_format;                 // 颜色缓冲附着的格式
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;                    // 采样数
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;               // 渲染之前对附着中的数据（颜色和深度）进行操作
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;             // 渲染之后对附着中的数据（颜色和深度）进行操作
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;    // 渲染之前对模板缓冲的操作
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // 渲染之后对模板缓冲的操作
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;          // 渲染流程开始前的图像布局方式
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;      // 渲染流程结束后的图像布局方式

    VkAttachmentDescription depth_attachment = {};
    depth_attachment.format = FindDepthFormat();
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // 绘制结束后不需要从深度缓冲复制深度数据
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // 不需要读取之前深度图像数据
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // 子流程引用的附着
    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;  // 索引，对应于片段着色器中的 layout(location = 0) out
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref = {};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // 子流程
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;  // 图形渲染子流程
    subpass.colorAttachmentCount = 1;                             // 颜色附着个数
    subpass.pColorAttachments = &color_attachment_ref;            // 指定颜色附着
    subpass.pDepthStencilAttachment =
      &depth_attachment_ref;  // 指定深度模板附着，深度模板附着只能是一个，所以不用设置数量

    // 渲染流程使用的依赖信息
    VkSubpassDependency dependency = {};
    dependency.srcSubpass =
      VK_SUBPASS_EXTERNAL;      // 渲染流程开始前的子流程，为了避免出现循环依赖，dst的值必须大于src的值
    dependency.dstSubpass = 0;  // 渲染流程结束后的子流程
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;  // 指定需要等待的管线阶段
    dependency.srcAccessMask = 0;                                          // 指定子流程将进行的操作类型
    dependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments{color_attachment, depth_attachment};

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    if (VK_SUCCESS != vkCreateRenderPass(m_device, &render_pass_info, nullptr, &m_render_pass)) {
      throw std::runtime_error("failed to create render pass");
    }
  }

  /// @brief 创建帧缓冲对象，附着需要绑定到帧缓冲对象上使用
  void CreateFramebuffers() {
    m_swap_chain_framebuffers.resize(m_swap_chain_image_views.size());

    for (size_t i = 0; i < m_swap_chain_image_views.size(); ++i) {
      std::array<VkImageView, 2> attachments{m_swap_chain_image_views.at(i), m_depth_image_view};

      VkFramebufferCreateInfo framebuffer_info = {};
      framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebuffer_info.renderPass = m_render_pass;
      framebuffer_info.attachmentCount = static_cast<uint32_t>(attachments.size());
      framebuffer_info.pAttachments = attachments.data();
      framebuffer_info.width = m_swap_chain_extent.width;
      framebuffer_info.height = m_swap_chain_extent.height;
      framebuffer_info.layers = 1;

      if (VK_SUCCESS != vkCreateFramebuffer(m_device, &framebuffer_info, nullptr, &m_swap_chain_framebuffers.at(i))) {
        throw std::runtime_error("failed to create framebuffer");
      }
    }
  }

  /// @brief 初始化ImGui
  void InitImGui() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2((float)(k_width), (float)(k_height));

    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 6.F;
    style.FrameRounding = 4.F;
    style.FramePadding = ImVec2(6.F, 5.F);
    style.ItemSpacing = ImVec2(8.F, 7.F);
    ImGui_ImplGlfw_InitForVulkan(m_window, true);

    const auto queue_family_indices = FindQueueFamilies(m_physical_device);
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_0;
    init_info.Instance = m_instance;
    init_info.PhysicalDevice = m_physical_device;
    init_info.Device = m_device;
    init_info.QueueFamily = lvk_tidy::GetRequiredValue(queue_family_indices.graphicsFamily);
    init_info.Queue = m_graphics_queue;
    init_info.DescriptorPool = m_imgui_descriptor_pool;
    init_info.MinImageCount = m_min_image_count;
    init_info.ImageCount = static_cast<uint32_t>(m_swap_chain_images.size());
    init_info.PipelineInfoMain.RenderPass = m_render_pass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
      throw std::runtime_error("failed to initialize ImGui Vulkan backend");
    }
  }

  /// @brief 创建指令池，用于管理指令缓冲对象使用的内存，并负责指令缓冲对象的分配
  void CreateCommandPool() {
    auto indices = FindQueueFamilies(m_physical_device);

    // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
    // 指定从Pool中分配的CommandBuffer将是短暂的，意味着它们将在相对较短的时间内被重置或释放
    // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT 允许从Pool中分配的任何CommandBuffer被单独重置到inital状态
    // 没有设置这个flag则不能使用 vkResetCommandBuffer
    // VK_COMMAND_POOL_CREATE_PROTECTED_BIT 指定从Pool中分配的CommandBuffer是受保护的CommandBuffer
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = lvk_tidy::GetRequiredValue(indices.graphicsFamily);

    if (VK_SUCCESS != vkCreateCommandPool(m_device, &pool_info, nullptr, &m_command_pool)) {
      throw std::runtime_error("failed to create command pool");
    }
  }

  /// @brief 为交换链中的每一个图像创建指令缓冲对象，使用它记录绘制指令
  void CreateCommandBuffers() {
    m_command_buffers.resize(m_swap_chain_framebuffers.size());

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = m_command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;  // 指定是主要还是辅助指令缓冲对象
    alloc_info.commandBufferCount = static_cast<uint32_t>(m_command_buffers.size());

    if (VK_SUCCESS != vkAllocateCommandBuffers(m_device, &alloc_info, m_command_buffers.data())) {
      throw std::runtime_error("failed to allocate command buffers");
    }
  }

  /// @brief 记录指令到指令缓冲
  void RecordCommandBuffer(VkCommandBuffer command_buffer, VkFramebuffer framebuffer) const {
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;  // 指定怎样使用指令缓冲
    begin_info.pInheritanceInfo = nullptr;  // 只用于辅助指令缓冲，指定从调用它的主要指令缓冲继承的状态

    if (VK_SUCCESS != vkBeginCommandBuffer(command_buffer, &begin_info)) {
      throw std::runtime_error("failed to begin recording command buffer");
    }

    // 清除色，相当于背景色
    std::array<VkClearValue, 2> clear_values{};
    const auto background = HasSrgbAttachment() ? ToLinear(glm::vec3(0.1F, 0.2F, 0.3F)) : glm::vec3(0.1F, 0.2F, 0.3F);
    clear_values.at(0).color = {{background.r, background.g, background.b, 1.F}};
    clear_values.at(1).depthStencil = {1.F, 0};

    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = m_render_pass;  // 指定使用的渲染流程对象
    render_pass_info.framebuffer = framebuffer;   // 指定使用的帧缓冲对象
    render_pass_info.renderArea.offset = {0, 0};  // 指定用于渲染的区域
    render_pass_info.renderArea.extent = m_swap_chain_extent;
    render_pass_info.clearValueCount =
      static_cast<uint32_t>(clear_values.size());  // 指定使用 VK_ATTACHMENT_LOAD_OP_CLEAR 标记后使用的清除值
    render_pass_info.pClearValues = clear_values.data();

    // 所有可以记录指令到指令缓冲的函数，函数名都带有一个 vkCmd 前缀

    // 开始一个渲染流程
    // 1.用于记录指令的指令缓冲对象
    // 2.使用的渲染流程的信息
    // 3.指定渲染流程如何提供绘制指令的标记
    vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    const auto scene_viewport = GetSceneViewport();
    viewport.x = static_cast<float>(scene_viewport.offset.x);
    viewport.y = 0.F;
    viewport.width = static_cast<float>(scene_viewport.extent.width);
    viewport.height = static_cast<float>(scene_viewport.extent.height);
    viewport.minDepth = 0.F;
    viewport.maxDepth = 1.F;

    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = scene_viewport.offset;
    scissor.extent = scene_viewport.extent;

    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    // 当前帧 fence 已完成。材质和节点在此上传，UI 仅修改 CPU 状态。
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> transparent;
    for (const auto& [_, model] : m_models) {
      if (model->attributes.visibility && !model->scenes.empty()) {
        UpdateModelUniforms(*model);
        for (int node : model->scenes.at(model->attributes.activeScene)->nodes) {
          CollectDrawItems(*model, node, opaque, transparent);
        }
      }
    }
    for (const auto& item : opaque) {
      DrawPrimitive(item, command_buffer);
    }
    std::stable_sort(transparent.begin(), transparent.end(),
                     [](const DrawItem& a, const DrawItem& b) { return a.depth > b.depth; });
    for (const auto& item : transparent) {
      DrawPrimitive(item, command_buffer);
    }

    // 绘制ImGui
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);

    // 结束渲染流程
    vkCmdEndRenderPass(command_buffer);

    // 结束记录指令到指令缓冲
    if (VK_SUCCESS != vkEndCommandBuffer(command_buffer)) {
      throw std::runtime_error("failed to record command buffer");
    }
  }

  /// @brief 绘制每一帧
  /// @details 流程：从交换链获取一张图像、对帧缓冲附着执行指令缓冲中的渲染指令、返回渲染后的图像到交换链进行呈现操作
  void DrawFrame() {
    // 等待一组栅栏中的一个或全部栅栏发出信号，即上一次提交的指令结束执行
    // 使用栅栏可以进行CPU与GPU之间的同步，防止超过 k_max_frames_in_flight 帧的指令同时被提交执行
    vkWaitForFences(m_device, 1, &m_in_flight_fences.at(m_current_frame), VK_TRUE,
                    std::numeric_limits<uint64_t>::max());

    // 从交换链获取一张图像
    uint32_t image_index{0};
    // 3.获取图像的超时时间，此处禁用图像获取超时
    // 4.通知的同步对象
    // 5.输出可用的交换链图像的索引
    VkResult result =
      vkAcquireNextImageKHR(m_device, m_swap_chain, std::numeric_limits<uint64_t>::max(),
                            m_image_available_semaphores.at(m_current_frame), VK_NULL_HANDLE, &image_index);

    // VK_ERROR_OUT_OF_DATE_KHR 交换链不能继续使用，通常发生在窗口大小改变后
    // VK_SUBOPTIMAL_KHR 交换链仍然可以使用，但表面属性已经不能准确匹配
    if (VK_ERROR_OUT_OF_DATE_KHR == result) {
      RecreateSwapChain();
      return;
    }
    if (VK_SUCCESS != result && VK_SUBOPTIMAL_KHR != result) {
      throw std::runtime_error("failed to acquire swap chain image");
    }

    // 手动将栅栏重置为未发出信号的状态（必须手动设置）
    vkResetFences(m_device, 1, &m_in_flight_fences.at(m_current_frame));
    vkResetCommandBuffer(m_command_buffers.at(image_index), 0);
    RecordCommandBuffer(m_command_buffers.at(image_index), m_swap_chain_framebuffers.at(image_index));

    const std::array<VkPipelineStageFlags, 1> wait_stages{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores =
      &m_image_available_semaphores.at(m_current_frame);  // 指定队列开始执行前需要等待的信号量
    submit_info.pWaitDstStageMask = wait_stages.data();   // 指定需要等待的管线阶段
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &m_command_buffers[image_index];  // 指定实际被提交执行的指令缓冲对象
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores =
      &m_render_finished_semaphores.at(image_index);  // 指定在指令缓冲执行结束后发出信号的信号量对象

    // 提交指令缓冲给图形指令队列
    // 如果不等待上一次提交的指令结束执行，可能会导致内存泄漏
    // 1.vkQueueWaitIdle 可以等待上一次的指令结束执行，2.也可以同时渲染多帧解决该问题（使用栅栏）
    // vkQueueSubmit 的最后一个参数用来指定在指令缓冲执行结束后需要发起信号的栅栏对象
    if (VK_SUCCESS != vkQueueSubmit(m_graphics_queue, 1, &submit_info, m_in_flight_fences.at(m_current_frame))) {
      throw std::runtime_error("failed to submit draw command buffer");
    }

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &m_render_finished_semaphores.at(image_index);  // 指定开始呈现操作需要等待的信号量
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &m_swap_chain;   // 指定用于呈现图像的交换链
    present_info.pImageIndices = &image_index;  // 指定需要呈现的图像在交换链中的索引
    present_info.pResults = nullptr;            // 可以通过该变量获取每个交换链的呈现操作是否成功的信息

    // 请求交换链进行图像呈现操作
    result = vkQueuePresentKHR(m_present_queue, &present_info);

    if (VK_ERROR_OUT_OF_DATE_KHR == result || VK_SUBOPTIMAL_KHR == result || m_framebuffer_resized) {
      m_framebuffer_resized = false;
      // 交换链不完全匹配时也重建交换链
      RecreateSwapChain();
    } else if (VK_SUCCESS != result) {
      throw std::runtime_error("failed to presend swap chain image");
    }

    // 更新当前帧索引
    m_current_frame = (m_current_frame + 1) % k_max_frames_in_flight;
  }

  /// @brief 创建同步对象，用于发出图像已经被获取可以开始渲染和渲染已经结束可以开始呈现的信号
  void CreateSyncObjects() {
    m_image_available_semaphores.resize(k_max_frames_in_flight);
    m_render_finished_semaphores.resize(m_swap_chain_images.size());
    m_in_flight_fences.resize(k_max_frames_in_flight);

    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // 初始状态设置为已发出信号，避免 vkWaitForFences 一直等待

    for (size_t i = 0; i < k_max_frames_in_flight; ++i) {
      if (VK_SUCCESS != vkCreateSemaphore(m_device, &semaphore_info, nullptr, &m_image_available_semaphores.at(i)) ||
          VK_SUCCESS != vkCreateFence(m_device, &fence_info, nullptr, &m_in_flight_fences.at(i))) {
        throw std::runtime_error("failed to create synchronization objects for a frame");
      }
    }

    for (auto& semaphore : m_render_finished_semaphores) {
      if (VK_SUCCESS != vkCreateSemaphore(m_device, &semaphore_info, nullptr, &semaphore)) {
        throw std::runtime_error("failed to create render finished semaphore");
      }
    }
  }

  /// @brief 重建交换链
  void RecreateSwapChain() {
    int width{0};
    int height{0};
    glfwGetFramebufferSize(m_window, &width, &height);
    while (0 == width || 0 == height) {
      glfwGetFramebufferSize(m_window, &width, &height);
      glfwWaitEvents();
    }

    // 等待设备处于空闲状态，避免在对象的使用过程中将其清除重建
    vkDeviceWaitIdle(m_device);

    // 在重建前清理之前使用的对象
    CleanupSwapChain();

    // 可以通过动态状态来设置视口和裁剪矩形来避免重建管线
    CreateSwapChain();
    CreateImageViews();
    CreateDepthResources();
    CreateFramebuffers();
  }

  /// @brief 清除交换链相关对象
  void CleanupSwapChain() noexcept {
    vkDestroyImageView(m_device, m_depth_image_view, nullptr);
    vkDestroyImage(m_device, m_depth_image, nullptr);
    vkFreeMemory(m_device, m_depth_image_memory, nullptr);

    for (auto framebuffer : m_swap_chain_framebuffers) {
      vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }

    for (auto image_view : m_swap_chain_image_views) {
      vkDestroyImageView(m_device, image_view, nullptr);
    }

    vkDestroySwapchainKHR(m_device, m_swap_chain, nullptr);
  }

  /// @brief 设置调试扩展信息
  /// @param createInfo
  void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& create_info) const noexcept {
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    // 设置回调函数处理的消息级别
    create_info.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    // 设置回调函数处理的消息类型
    create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    // 设置回调函数
    create_info.pfnUserCallback = DebugCallback;
    // 设置用户自定义数据，是可选的
    create_info.pUserData = nullptr;
  }

  /// @brief 创建指定类型的缓冲
  /// @details Vulkan 的缓冲是可以存储任意数据的可以被显卡读取的内存，不仅可以存储顶点数据
  /// @param size
  /// @param usage
  /// @param properties
  /// @param buffer
  /// @param bufferMemory
  void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
                    VkDeviceMemory& buffer_memory) const {
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;                              // 缓冲的字节大小
    buffer_info.usage = usage;                            // 缓冲中的数据使用目的，可以使用位或来指定多个目的
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // 缓冲可以被特定的队列族所拥有，也可以在多个队列族共享
    buffer_info.flags = 0;                                // 配置缓冲的内存稀疏程度，0表示使用默认值

    if (VK_SUCCESS != vkCreateBuffer(m_device, &buffer_info, nullptr, &buffer)) {
      throw std::runtime_error("failed to create vertex buffer");
    }

    // 缓冲创建好之后还需要分配内存，首先获取缓冲的内存需求
    // size: 缓冲需要的内存的字节大小，可能和bufferInfo.size的值不同
    // alignment: 缓冲在实际被分配的内存中的开始位置，依赖于bufferInfo的usage和flags
    // memoryTypeBits: 指示适合该缓冲使用的内存类型的位域
    VkMemoryRequirements mem_requirements{};
    vkGetBufferMemoryRequirements(m_device, buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = FindMemoryType(mem_requirements.memoryTypeBits, properties);

    if (VK_SUCCESS != vkAllocateMemory(m_device, &alloc_info, nullptr, &buffer_memory)) {
      throw std::runtime_error("failed to allocate buffer memory");
    }

    // 4. 偏移值，需要满足能够被 memRequirements.alighment 整除
    vkBindBufferMemory(m_device, buffer, buffer_memory, 0);
  }

  /// @brief 在缓冲之间复制数据
  /// @param srcBuffer
  /// @param dstBuffer
  /// @param size
  void CopyBuffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size) const noexcept {
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = m_command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer{};
    vkAllocateCommandBuffers(m_device, &alloc_info, &command_buffer);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;  // 指定如何使用这个指令缓冲

    vkBeginCommandBuffer(command_buffer, &begin_info);
    VkBufferCopy copy_region = {};
    copy_region.srcOffset = 0;
    copy_region.dstOffset = 0;
    copy_region.size = size;
    vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    // 提交到内存传输指令队列执行内存传输
    vkQueueSubmit(m_graphics_queue, 1, &submit_info, nullptr);
    // 等待传输操作完成，也可以使用栅栏，栅栏可以同步多个不同的内存传输操作，给驱动程序的优化空间也更大
    vkQueueWaitIdle(m_graphics_queue);

    vkFreeCommandBuffers(m_device, m_command_pool, 1, &command_buffer);
  }

  VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags) {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect_flags;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VkImageView image_view{};
    if (VK_SUCCESS != vkCreateImageView(m_device, &view_info, nullptr, &image_view)) {
      throw std::runtime_error("failed to create texture image view");
    }

    return image_view;
  }

  /// @brief 查找最合适的内存类型
  /// @details 不同类型的内存所允许进行的操作以及操作的效率有所不同
  /// @param type_filter 指定需要的内存类型的位域
  /// @param properties
  /// @return
  uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
    // 查找物理设备可用的内存类型
    // memoryHeaps 内存来源，比如显存以及显存用尽后的位与主存中的交换空间
    VkPhysicalDeviceMemoryProperties mem_properties{};
    vkGetPhysicalDeviceMemoryProperties(m_physical_device, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
      if (type_filter & (1 << i) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
        return i;
      }
    }

    throw std::runtime_error("failed to find suitable memory type");
  }

  void CreateDescriptorSetLayouts() {
    auto create = [&](const char* name, const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
      auto layout = std::make_unique<DescriptorSetLayout>();
      VkDescriptorSetLayoutCreateInfo info{};
      info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      info.bindingCount = static_cast<uint32_t>(bindings.size());
      info.pBindings = bindings.data();
      if (vkCreateDescriptorSetLayout(m_device, &info, nullptr, &layout->descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create viewer descriptor layout");
      }
      m_context->descriptorSetLayouts.try_emplace(name, std::move(layout));
    };
    create("uniform_model", {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}});
    create("uniform_animation_skin", {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}});
    std::vector<VkDescriptorSetLayoutBinding> material{
      {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    for (uint32_t binding = 1; binding <= 5; ++binding) {
      material.push_back(
        {binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    }
    create("uniform_material", material);
  }

  std::unique_ptr<Uniform> CreateStorageBuffer(const void* data, VkDeviceSize buffer_size,
                                               VkDescriptorSetLayout descriptor_set_layout) {
    auto uniform = std::make_unique<Uniform>();

    uniform->buffers.resize(k_max_frames_in_flight);
    uniform->memorys.resize(k_max_frames_in_flight);
    uniform->mappeds.resize(k_max_frames_in_flight);

    for (size_t i = 0; i < k_max_frames_in_flight; ++i) {
      CreateBuffer(buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniform->buffers[i],
                   uniform->memorys[i]);
      vkMapMemory(m_device, uniform->memorys[i], 0, buffer_size, 0, &uniform->mappeds[i]);
    }

    //---------------------------------------------------------------------
    for (size_t i = 0; i < k_max_frames_in_flight; ++i) {
      std::memcpy(uniform->mappeds[i], data, buffer_size);
    }

    //---------------------------------------------------------------------
    std::vector<VkDescriptorSetLayout> layouts(k_max_frames_in_flight, descriptor_set_layout);

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool;
    alloc_info.descriptorSetCount = static_cast<uint32_t>(k_max_frames_in_flight);
    alloc_info.pSetLayouts = layouts.data();

    uniform->descriptorSets->descriptorSets.resize(k_max_frames_in_flight);
    if (VK_SUCCESS != vkAllocateDescriptorSets(m_device, &alloc_info, uniform->descriptorSets->descriptorSets.data())) {
      throw std::runtime_error("failed to allocate descriptor sets");
    }

    for (size_t i = 0; i < k_max_frames_in_flight; ++i) {
      VkDescriptorBufferInfo buffer_info{};
      buffer_info.buffer = uniform->buffers[i];
      buffer_info.offset = 0;
      buffer_info.range = buffer_size;

      VkWriteDescriptorSet descriptor_write{};
      descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptor_write.dstSet = uniform->descriptorSets->descriptorSets[i];
      descriptor_write.dstBinding = 0;
      descriptor_write.dstArrayElement = 0;
      descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptor_write.descriptorCount = 1;
      descriptor_write.pBufferInfo = &buffer_info;
      descriptor_write.pImageInfo = nullptr;
      descriptor_write.pTexelBufferView = nullptr;

      vkUpdateDescriptorSets(m_device, 1, &descriptor_write, 0, nullptr);
    }

    return uniform;
  }

  template <typename T>
  std::unique_ptr<Uniform> CreateUniforms(const T& data, VkDescriptorSetLayout descriptor_set_layout) {
    auto uniform = std::make_unique<Uniform>();

    VkDeviceSize buffer_size{0};
    buffer_size = sizeof(T);

    uniform->buffers.resize(k_max_frames_in_flight);
    uniform->memorys.resize(k_max_frames_in_flight);
    uniform->mappeds.resize(k_max_frames_in_flight);

    for (size_t i = 0; i < k_max_frames_in_flight; ++i) {
      CreateBuffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniform->buffers[i],
                   uniform->memorys[i]);
      vkMapMemory(m_device, uniform->memorys[i], 0, buffer_size, 0, &uniform->mappeds[i]);
    }

    //---------------------------------------------------------------------
    for (size_t i = 0; i < k_max_frames_in_flight; ++i) {
      std::memcpy(uniform->mappeds[i], &data, sizeof(T));
    }

    //---------------------------------------------------------------------
    std::vector<VkDescriptorSetLayout> layouts(k_max_frames_in_flight, descriptor_set_layout);

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool;
    alloc_info.descriptorSetCount = static_cast<uint32_t>(k_max_frames_in_flight);
    alloc_info.pSetLayouts = layouts.data();

    uniform->descriptorSets->descriptorSets.resize(k_max_frames_in_flight);
    if (VK_SUCCESS != vkAllocateDescriptorSets(m_device, &alloc_info, uniform->descriptorSets->descriptorSets.data())) {
      throw std::runtime_error("failed to allocate descriptor sets");
    }

    for (size_t i = 0; i < k_max_frames_in_flight; ++i) {
      VkDescriptorBufferInfo buffer_info{};
      buffer_info.buffer = uniform->buffers[i];
      buffer_info.offset = 0;
      buffer_info.range = sizeof(T);

      VkWriteDescriptorSet descriptor_write{};
      descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptor_write.dstSet = uniform->descriptorSets->descriptorSets[i];
      descriptor_write.dstBinding = 0;
      descriptor_write.dstArrayElement = 0;
      descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptor_write.descriptorCount = 1;
      descriptor_write.pBufferInfo = &buffer_info;
      descriptor_write.pImageInfo = nullptr;
      descriptor_write.pTexelBufferView = nullptr;

      vkUpdateDescriptorSets(m_device, 1, &descriptor_write, 0, nullptr);
    }

    return uniform;
  }

  /// @brief 创建描述符池，描述符集需要通过描述符池来创建
  void CreateDescriptorPool() {
    uint64_t material_sets{};
    uint64_t node_sets{};
    uint64_t skin_sets{};
    for (const auto& [_, model] : m_models) {
      material_sets += model->gltfModel.materials.size() + 1;
      ++skin_sets;  // 每个模型的 identity skin fallback。
      for (const auto& node : model->gltfModel.nodes) {
        node_sets += node.mesh >= 0 ? 1 : 0;
        skin_sets += node.skin >= 0 ? 1 : 0;
      }
    }
    const uint64_t uniforms = (material_sets + node_sets) * k_max_frames_in_flight;
    const uint64_t samplers = material_sets * 5 * k_max_frames_in_flight;
    const uint64_t storage = skin_sets * k_max_frames_in_flight;
    if (uniforms + storage > std::numeric_limits<uint32_t>::max() || samplers > std::numeric_limits<uint32_t>::max()) {
      throw std::runtime_error("Too many glTF resources for the descriptor pool");
    }
    const std::array<VkDescriptorPoolSize, 3> sizes{
      {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(uniforms)},
       {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(samplers)},
       {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(storage)}}};
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets = static_cast<uint32_t>(uniforms + storage);
    info.poolSizeCount = static_cast<uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    if (vkCreateDescriptorPool(m_device, &info, nullptr, &m_descriptor_pool) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create viewer descriptor pool");
    }
  }

  /// @brief 创建指定格式的图像对象
  /// @param width
  /// @param height
  /// @param format
  /// @param tiling
  /// @param usage
  /// @param properties
  /// @param image
  /// @param imageMemory
  void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                   VkMemoryPropertyFlags /*properties*/, VkImage& image, VkDeviceMemory& image_memory)

  {
    // tiling成员变量可以是 VK_IMAGE_TILING_LINEAR 纹素以行主序的方式排列，可以直接访问图像
    //                     VK_IMAGE_TILING_OPTIMAL 纹素以一种对访问优化的方式排列
    // initialLayout成员变量可以是 VK_IMAGE_LAYOUT_UNDEFINED GPU不可用，纹素在第一次变换会被丢弃
    //                            VK_IMAGE_LAYOUT_PREINITIALIZED GPU不可用，纹素在第一次变换会被保留
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = static_cast<uint32_t>(width);
    image_info.extent.height = static_cast<uint32_t>(height);
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = tiling;                            // 设置之后不可修改
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // 图像数据是接收方，不需要保留第一次变换时的纹理数据
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // 只被一个队列族使用（支持传输操作的队列族），所以使用独占模式
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;          // 设置多重采样，只对用作附着的图像对象有效
    image_info.flags = 0;  // 可以用来设置稀疏图像的优化，比如体素地形没必要为“空气”部分分配内存

    if (VK_SUCCESS != vkCreateImage(m_device, &image_info, nullptr, &image)) {
      throw std::runtime_error("failed to create image");
    }

    VkMemoryRequirements mem_requirements{};
    vkGetImageMemoryRequirements(m_device, image, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = FindMemoryType(mem_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (VK_SUCCESS != vkAllocateMemory(m_device, &alloc_info, nullptr, &image_memory)) {
      throw std::runtime_error("failed to allocate image memory");
    }
    vkBindImageMemory(m_device, image, image_memory, 0);
  }

  /// @brief 配置深度图像需要的资源
  void CreateDepthResources() {
    auto depth_format = FindDepthFormat();

    CreateImage(m_swap_chain_extent.width, m_swap_chain_extent.height, depth_format, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depth_image,
                m_depth_image_memory);

    m_depth_image_view = CreateImageView(m_depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
  }

  /// @brief 查找一个符合要求又被设备支持的图像数据格式
  /// @param candidates
  /// @param tiling
  /// @param features
  /// @return
  VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                               VkFormatFeatureFlags features) const {
    for (auto format : candidates) {
      // 查找可用的深度图像格式
      VkFormatProperties props{};
      vkGetPhysicalDeviceFormatProperties(m_physical_device, format, &props);
      // props.bufferFeatures        表示数据格式支持缓冲
      // props.linearTilingFeatures  表示数据格式支持线性tiling模式
      // props.optimalTilingFeatures 表示数据格式支持优化tiling模式

      if (VK_IMAGE_TILING_LINEAR == tiling && (props.linearTilingFeatures & features) == features) {
        return format;
      }
      if (VK_IMAGE_TILING_OPTIMAL == tiling && (props.optimalTilingFeatures & features) == features) {
        return format;
      }
    }

    throw std::runtime_error("failed to find supported format");
  }

  /// @brief 查找适合作为深度附着的图像数据格式
  /// @return
  VkFormat FindDepthFormat() const {
    return FindSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                               VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  }

  /// @brief 创建ImGui的描述符池
  void CreateImGuiDescriptorPool() {
    std::vector<VkDescriptorPoolSize> pool_sizes{{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                                                 {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                                                 {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                                                 {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                                                 {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                                                 {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                                                 {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                                                 {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                                                 {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                                                 {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                                                 {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * static_cast<uint32_t>(pool_sizes.size());
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    if (VK_SUCCESS != vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_imgui_descriptor_pool)) {
      throw std::runtime_error("failed to create descriptor pool");
    }
  }

private:
  /// @brief 接受调试信息的回调函数
  /// @param message_severity 消息的级别：诊断、资源创建、警告、不合法或可能造成崩溃的操作
  /// @param message_type 发生了与规范和性能无关的事件、出现了违反规范的错误、进行了可能影响 Vulkan 性能的行为
  /// @param callback_data 包含了调试信息的字符串、存储有和消息相关的 Vulkan 对象句柄的数组、数组中的对象个数
  /// @param user_data 指向了设置回调函数时，传递的数据指针
  /// @return 引发校验层处理的 Vulkan API 调用是否中断，通常只在测试校验层本身时会返回true，其余都应该返回 VK_FALSE
  static VKAPI_ATTR VkBool32 VKAPI_CALL
  DebugCallback([[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT message_type,
                const VkDebugUtilsMessengerCallbackDataEXT* callback_data, [[maybe_unused]] void* user_data) noexcept {
    std::clog << "===========================================\n"
              << "Debug::validation layer: " << callback_data->pMessage << '\n';

    return VK_FALSE;
  }

  /// @brief 代理函数，用来加载 Vulkan 扩展函数 vkCreateDebugUtilsMessengerEXT
  /// @param instance
  /// @param pCreateInfo
  /// @param pAllocator
  /// @param pCallback
  /// @return
  static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
                                               const VkDebugUtilsMessengerCreateInfoEXT* p_create_info,
                                               const VkAllocationCallbacks* p_allocator,
                                               VkDebugUtilsMessengerEXT* p_callback) noexcept {
    // vkCreateDebugUtilsMessengerEXT是一个扩展函数，不会被 Vulkan 库自动加载，所以需要手动加载
    auto func =
      lvk_tidy::LoadInstanceProcAddress<PFN_vkCreateDebugUtilsMessengerEXT>(instance, "vkCreateDebugUtilsMessengerEXT");

    if (nullptr != func) {
      return func(instance, p_create_info, p_allocator, p_callback);
    }

    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }

  /// @brief 代理函数，用来加载 Vulkan 扩展函数 vkDestroyDebugUtilsMessengerEXT
  /// @param instance
  /// @param callback
  /// @param pAllocator
  static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT callback,
                                            const VkAllocationCallbacks* p_allocator) noexcept {
    auto func = lvk_tidy::LoadInstanceProcAddress<PFN_vkDestroyDebugUtilsMessengerEXT>(
      instance, "vkDestroyDebugUtilsMessengerEXT");

    if (nullptr != func) {
      func(instance, callback, p_allocator);
    }
  }

  /// @brief 读取二进制着色器文件
  /// @param fileName
  /// @return
  static std::vector<char> ReadFile(const std::string& file_name) {
    std::ifstream file(file_name, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
      throw std::runtime_error("failed to open file: " + file_name);
    }

    size_t file_size{0};
    file_size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(file_size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(file_size));
    file.close();
    return buffer;
  }

  static void FramebufferResizeCallback(GLFWwindow* window, int /*widht*/, int /*height*/) noexcept {
    if (auto app = reinterpret_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window))) {
      app->m_framebuffer_resized = true;
    }
  }

  VkRect2D GetSceneViewport() const {
    int window_width{};
    glfwGetWindowSize(m_window, &window_width, nullptr);
    const float scale =
      window_width > 0 ? static_cast<float>(m_swap_chain_extent.width) / static_cast<float>(window_width) : 1.F;
    const auto left = static_cast<uint32_t>(
      std::clamp(m_controls_width * scale, 0.F,
                 static_cast<float>(m_swap_chain_extent.width > 0 ? m_swap_chain_extent.width - 1 : 0)));
    return {{static_cast<int32_t>(left), 0},
            {std::max(m_swap_chain_extent.width - left, 1U), std::max(m_swap_chain_extent.height, 1U)}};
  }

  Bounds GetPrimitiveBounds(const Model& model, const Node& node, const Primitive& primitive) const {
    Bounds local = primitive.bounds;
    if (primitive.hasMorph && local.IsValid()) {
      for (size_t i = 0; i < primitive.morphBounds.size(); ++i) {
        const auto& delta = primitive.morphBounds[i];
        if (delta.IsValid()) {
          const auto first = delta.minimum * node.morphWeights[static_cast<int>(i)];
          const auto second = delta.maximum * node.morphWeights[static_cast<int>(i)];
          local.minimum += glm::min(first, second);
          local.maximum += glm::max(first, second);
        }
      }
    }
    Bounds bounds{};
    if (primitive.hasSkin && node.skin) {
      for (size_t i = 0; i < node.skin->joints.size(); ++i) {
        bounds.Include(local, GetJointMatrix(model, *node.skin, i));
      }
    } else {
      bounds.Include(local, GetNodeMatrix(&node));
    }
    return bounds;
  }

  Bounds GetModelBounds(const Model& model) const {
    Bounds bounds{};
    auto visit = [&](auto&& self, int index) -> void {
      const auto& node = *model.nodes.at(index);
      if (node.mesh) {
        for (const auto& primitive : node.mesh->primitives) {
          bounds.Include(GetPrimitiveBounds(model, node, *primitive), glm::mat4(1.F));
        }
      }
      for (int child : node.children) {
        self(self, child);
      }
    };
    if (!model.scenes.empty()) {
      for (int node : model.scenes.at(model.attributes.activeScene)->nodes) {
        visit(visit, node);
      }
    }
    return bounds;
  }

  void UpdateSceneBounds() {
    m_visible_bounds = Bounds{};
    for (const auto& [_, model] : m_models) {
      if (model->attributes.visibility) {
        m_visible_bounds.Include(GetModelBounds(*model), glm::mat4(1.F));
      }
    }
    if (m_visible_bounds.IsValid()) {
      m_scene_center = (m_visible_bounds.minimum + m_visible_bounds.maximum) * 0.5F;
      m_scene_radius = std::max(glm::length(m_visible_bounds.maximum - m_visible_bounds.minimum) * 0.5F, 0.0001F);
    }
  }

  void ResetCamera(const Model* focus = nullptr) {
    const auto bounds = focus ? GetModelBounds(*focus) : m_visible_bounds;
    if (!bounds.IsValid()) {
      return;
    }
    const auto viewport = GetSceneViewport();
    const float aspect = static_cast<float>(viewport.extent.width) / static_cast<float>(viewport.extent.height);
    const float half_vertical = glm::radians(m_field_of_view) * 0.5F;
    const float half_horizontal = std::atan(std::tan(half_vertical) * aspect);
    const float radius = std::max(glm::length(bounds.maximum - bounds.minimum) * 0.5F, 0.0001F);
    const float distance = radius * 1.1F / std::sin(std::min(half_vertical, half_horizontal));
    m_look_at = (bounds.minimum + bounds.maximum) * 0.5F;
    m_view_up = {0.F, 1.F, 0.F};
    // 斜上方观察同时覆盖 XY、XZ 等常见平面，避免重置后薄片恰好呈侧面。
    const auto view_direction = glm::normalize(glm::vec3(1.F, 0.75F, 1.F));
    m_eye_pos = m_look_at + view_direction * distance;
  }

  void UpdateCameraInput() {
    const auto& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
      m_reset_camera_requested = true;
      m_focus_model = nullptr;
    }
    if (m_reset_camera_requested) {
      ResetCamera(m_focus_model);
      m_reset_camera_requested = false;
      m_focus_model = nullptr;
      return;
    }
    // 在 NewFrame 更新输入归属后处理，UI 拖动不会影响相机，视口拖动经过 UI 时也不会跳变。
    if (io.WantCaptureMouse || !glfwGetWindowAttrib(m_window, GLFW_FOCUSED)) {
      return;
    }
    const auto offset = m_eye_pos - m_look_at;
    const float distance = glm::length(offset);
    if (distance <= 0.F) {
      return;
    }
    const auto forward = -offset / distance;
    const auto right = glm::normalize(glm::cross(forward, m_view_up));
    const auto up = glm::normalize(glm::cross(right, forward));
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      int window_height{};
      glfwGetWindowSize(m_window, nullptr, &window_height);
      if (window_height > 0) {
        // GLFW / ImGui 鼠标使用逻辑像素，投影视口高度转换到同一坐标系。
        const float units_per_pixel =
          2.F * distance * std::tan(glm::radians(m_field_of_view) * 0.5F) / static_cast<float>(window_height);
        const auto translation = (-right * io.MouseDelta.x + up * io.MouseDelta.y) * units_per_pixel;
        m_eye_pos += translation;
        m_look_at += translation;
      }
    } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
      constexpr float sensitivity = 0.005F;
      const auto rotation =
        glm::angleAxis(-io.MouseDelta.x * sensitivity, up) * glm::angleAxis(-io.MouseDelta.y * sensitivity, right);
      m_eye_pos = m_look_at + rotation * offset;
      m_view_up = glm::normalize(rotation * up);
    }
    if (io.MouseWheel != 0.F) {
      const float new_distance =
        std::clamp(distance * std::exp(-io.MouseWheel * 0.12F), m_scene_radius * 0.0001F, m_scene_radius * 10000.F);
      m_eye_pos = m_look_at + glm::normalize(m_eye_pos - m_look_at) * new_distance;
    }
  }

private:
  GLFWwindow* m_window{nullptr};
  VkInstance m_instance{nullptr};
  VkDebugUtilsMessengerEXT m_debug_messenger{nullptr};
  VkPhysicalDevice m_physical_device{nullptr};
  VkDevice m_device{nullptr};
  VkQueue m_graphics_queue{nullptr};  // 图形队列
  VkSurfaceKHR m_surface{nullptr};
  VkQueue m_present_queue{nullptr};  // 呈现队列
  VkSwapchainKHR m_swap_chain{nullptr};
  std::vector<VkImage> m_swap_chain_images{};
  VkFormat m_swap_chain_image_format{};
  VkExtent2D m_swap_chain_extent{};
  std::vector<VkImageView> m_swap_chain_image_views{};
  VkRenderPass m_render_pass{nullptr};
  VkDescriptorPool m_descriptor_pool{nullptr};
  std::vector<VkFramebuffer> m_swap_chain_framebuffers{};
  VkCommandPool m_command_pool{nullptr};
  std::vector<VkCommandBuffer> m_command_buffers{};
  std::vector<VkSemaphore> m_image_available_semaphores{};
  std::vector<VkSemaphore> m_render_finished_semaphores{};
  std::vector<VkFence> m_in_flight_fences{};
  size_t m_current_frame{0};
  bool m_framebuffer_resized{false};

  VkImage m_depth_image{nullptr};
  VkDeviceMemory m_depth_image_memory{nullptr};
  VkImageView m_depth_image_view{nullptr};

  glm::vec3 m_view_up{0.F, 1.F, 0.F};
  glm::vec3 m_eye_pos{3.F, 2.25F, 3.F};
  glm::vec3 m_look_at{0.F};
  glm::vec3 m_scene_center{0.F};
  Bounds m_visible_bounds{};
  float m_scene_radius{1.F};
  float m_field_of_view{45.F};
  float m_controls_width{320.F};
  bool m_reset_camera_requested{true};
  const Model* m_focus_model{nullptr};

  uint32_t m_min_image_count{0};
  VkDescriptorPool m_imgui_descriptor_pool{nullptr};

  std::unique_ptr<Context> m_context{std::make_unique<Context>()};
  std::map<std::string, std::unique_ptr<Model>> m_models{};

  tinygltf::TinyGLTF m_gltf_loader{};
};

int main() {
  HelloTriangleApplication app;

  try {
    app.Run();
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
