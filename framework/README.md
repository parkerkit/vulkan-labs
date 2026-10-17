# Vulkan 渲染框架

C++20 / Vulkan 1.3 静态库，提供窗口、设备、资源封装、帧调度、相机和 ImGui 集成。
CMake target 为 `Framework::Framework`，公开类型位于 `fw` 命名空间，头文件通过 `<framework/application.h>` 等路径包含。

[工程组织](../docs/project-layout.md) · [构建环境](../docs/getting-started.md)

## 目录与依赖

```text
framework/
├── CMakeLists.txt
├── include/framework/           按类型平铺的公开头文件
├── src/                         实现与私有 Vulkan 辅助函数
└── examples/interactive_cube/   立方体程序、ImGui 面板与 GLSL
```

`include/framework/` 提供公共包含路径前缀。库直接公开链接 Vulkan、GLM 和 ImGui，直接私有链接 GLFW 和 VMA。
交互立方体链接框架；根目录 `samples/` 中的独立技术示例自行组织运行流程，不依赖框架。

设备初始化要求 Vulkan 1.3 loader 与物理设备、Dynamic Rendering、Synchronization2、支持 Graphics + Compute 的队列，以及窗口呈现能力。
Debug 默认启用验证层，Release 默认关闭，可通过 `ApplicationDesc::device.enable_validation` 配置。
请求的验证层或必需设备能力不可用时，初始化抛出异常。

## 构建与运行

在仓库根目录、已初始化 MSVC x64 的开发终端中执行：

```powershell
cmake --preset msvc-debug-tidy -DBUILD_SAMPLES=OFF
cmake --build --preset msvc-debug-tidy --target framework_interactive_cube
.\build\msvc-debug-tidy\bin\framework_interactive_cube.exe
```

| 选项 | 默认值 | 作用 |
| --- | --- | --- |
| `BUILD_FRAMEWORK` | `ON` | 构建框架静态库 |
| `BUILD_FRAMEWORK_EXAMPLES` | `ON` | 在启用框架时构建立方体 |
| `BUILD_SAMPLES` | `ON` | 构建根目录 `samples/` 中的独立示例 |

立方体的 Shader 随 target 编译，目标环境为 `vulkan1.3`，输出到构建目录的 `bin/shaders/`：

- `framework_interactive_cube_vert.spv`
- `framework_interactive_cube_frag.spv`

CMake 通过 `target_name` 生成输出文件名，并向程序传入 `PROJECT_SHADER_DIR`。
立方体在该目录中按上述文件名加载 Shader，源码中的文件名与 CMake 输出共同构成加载约定。
Shader 路径为编译时绝对路径，程序无需外部模型或纹理。

## 交互立方体

立方体使用顶点与索引缓冲、逐帧 Uniform、描述符、深度附件和 Dynamic Rendering。
`Cube Controls` 面板显示 GPU 名称、帧率和输出尺寸，提供自转开关、速度、角度、背景色、相机复位与整体复位。

| 操作 | 效果 |
| --- | --- |
| 左键拖动 | 围绕目标旋转相机 |
| 右键拖动 | 平移相机目标 |
| 滚轮 | 拉近或拉远 |
| `W/A/S/D`、`Q/E` | 移动相机目标 |
| `Space` | 暂停或恢复自转 |
| `Home` | 重置相机 |
| `R` | 重置相机、旋转与背景色等参数 |
| `Esc` | 退出程序 |

ImGui 捕获鼠标或键盘时，相应输入会从传给示例的快照中清除。`Esc` 由应用统一处理。
窗口缩放触发交换链与深度附件重建，颜色格式变化时重建相关图形管线；最小化时暂停提交并等待窗口事件。
相机采用右手坐标、+Y 向上、深度范围 `[0, 1]`，投影矩阵进行一次 Y 翻转。

## 公开接口

| 类型 | 职责 |
| --- | --- |
| `Window`、`InputState` | GLFW 生命周期、窗口、输入快照和 Surface 创建 |
| `OrbitCamera` | 相机矩阵与输入控制 |
| `Device` | Vulkan 实例、物理设备、逻辑设备、队列与 VMA 分配器 |
| `Swapchain` | 呈现交换链、图像与图像视图 |
| `Buffer`、`Image`、`ShaderModule` | 资源所有权、映射内存操作和离线 SPIR-V 加载 |
| `DescriptorSetLayout`、`DescriptorPool` | 描述符布局、集合分配与池生命周期 |
| `Pipeline` | 图形或计算管线及其 Pipeline Layout |
| `Application`、`ISample` | 应用运行环境与示例回调 |
| `FrameScheduler`、`FrameContext` | 飞行帧、命令缓冲、提交、呈现与交换链重建 |
| `ImGuiLayer` | ImGui context、GLFW/Vulkan backend、UI 录制与输入过滤 |

资源工厂直接返回对象或 `std::unique_ptr`，失败通过 C++ 异常报告。
`FrameScheduler::BeginFrame()` 返回 `std::optional<FrameContext>`，空值表示窗口暂停输出或交换链刚完成重建。

## 应用与回调

`Application::Run()` 根据 `ApplicationDesc` 创建窗口、设备、帧调度器和可选 ImGui 层，再通过工厂创建 `ISample`。
初始化成功后进入主循环，退出时等待 GPU 并释放资源。实际入口见 [main.cpp](examples/interactive_cube/main.cpp)，回调实现见 [cube_sample.cpp](examples/interactive_cube/cube_sample.cpp)。

| 回调 | 调用时机与职责 |
| --- | --- |
| `OnInitialize(context, swapchain)` | 创建示例资源；`context` 中的服务为借用关系 |
| `OnResize(swapchain)` | 交换链重建后更新尺寸或格式相关资源 |
| `OnGui()` | 在已开始的 ImGui 帧中构建控件；禁用 ImGui 时不调用 |
| `OnUpdate(frame, input, delta)` | 使用过滤后的输入更新状态，并写入当前飞行帧的数据 |
| `OnRender(frame)` | 向当前命令缓冲录制场景渲染 |

初始尺寸由 `OnInitialize()` 的 `swapchain` 参数提供。立方体在初始化中主动调用自身 `OnResize()` 创建附件；
应用不会额外发送一次初始 `OnResize()`。

每个有效帧按以下顺序执行：

1. `BeginFrame()` 等待帧槽位和交换链图像可用，开始命令录制，并转换颜色图像布局。
2. 启用 ImGui 时开始 UI 帧，执行 `OnGui()`、完成 UI 数据构建，并过滤输入。
3. 执行 `OnUpdate()`，再执行 `OnRender()`。
4. 启用 ImGui 时录制颜色叠加绘制。
5. `EndFrame()` 转换呈现布局、提交命令并呈现。

`OnGui()` 中的参数调整在同一帧的 `OnUpdate()` 生效。示例不调用 ImGui 的 `NewFrame()` 或 `Render()`。
`OnRender()` 返回前必须结束自身渲染作用域，并将交换链颜色图像保留在 `COLOR_ATTACHMENT_OPTIMAL`。
帧调度器负责该图像的首尾布局转换，深度与离屏附件的布局和同步由示例负责。

## 帧同步与资源寿命

`FrameSchedulerDesc` 默认配置两个飞行帧并启用垂直同步。
`FrameContext::frame_index` 标识飞行帧槽位，`image_index` 标识交换链图像，两者不能互换。
立方体按飞行帧分配 Uniform、描述符集合与深度图；`OnUpdate()` 执行前，该槽位的 fence 已完成。

Acquire semaphore 按飞行帧分配，Present wait semaphore 按交换链图像分配。
帧 fence 在提交前重置；交换链重建与退出使用 Device Idle，正常逐帧提交不调用 Device Idle。
提交失败后应停止渲染并销毁调度器。

所有窗口、队列和 ImGui 操作在主线程执行。公开裸句柄是借用关系，`Device` 必须比其子资源存活更久。
`Swapchain` 管理呈现图像及视图，不管理深度附件、RenderPass 或 framebuffer。

`Buffer`、`Image`、`Pipeline` 等资源包装的析构和移动赋值不自动等待 GPU。
销毁资源、重置描述符池或覆盖仍在使用的数据前，调用者必须保证相关 GPU 使用已完成。
运行时替换的资源可以在有效帧内交给 `FrameScheduler::Retire()`，由当前帧 fence 保持其生命周期。
资源的最后一次 GPU 使用必须由该完成信号覆盖，后续提交不得再次引用该资源。

`Buffer::Write()` 检查范围并 Flush，`Read()` 在读取前 Invalidate；CPU/GPU 访问顺序仍由调用者负责。
`Image` 不隐式转换布局，使用 `CmdBarrier` 显式指定 stage、access 与子资源范围。
`SubmitImmediate()` 将初始化命令录制到一次提交中，并等待该提交的 fence；staging 资源必须存活到调用返回。

## ImGui 与异常边界

`ApplicationDesc::enable_imgui` 默认为 `true`。设为 `false` 会关闭 UI 初始化、回调与绘制，CMake 链接依赖仍包含 ImGui。
示例可在 `OnGui()` 中直接包含 `<imgui.h>` 并创建控件。

`ImGuiLayer` 管理 context 和 GLFW/Vulkan backend。GLFW backend 链接输入回调，关闭时恢复回调；
字体纹理由 ImGui 1.93 backend 管理。UI 使用独立的颜色叠加渲染作用域，场景与 UI 之间设置显式颜色附件依赖。
sRGB 交换链上的 UI 顶点 RGB 先转换为线性值。交换链格式或图像数量要求变化时，重建 Vulkan backend 并保留 context。

框架 C++ 接口中的异常传播到可执行程序的 `main()`，由入口捕获、打印并返回失败状态。
`Application` 在正常退出和异常展开时先等待 GPU，再销毁示例、UI、帧调度器、设备与窗口；部分初始化资源由 RAII 清理。
C 接口回调、`noexcept` 函数和析构函数不传播异常。ImGui 的 Vulkan 结果回调只记录错误，返回框架调用边界后再报告异常。
