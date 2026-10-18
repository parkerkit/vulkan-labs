# 构建与运行

[项目首页](../README.md) · [示例目录](samples.md) · [开发指南](development.md)

## 环境

### Windows

Windows 预设使用 **MSVC x64 与 Ninja**。安装 Visual Studio 2026 的 C++ 桌面开发工具和 Windows SDK，并准备以下工具：

| 依赖 | 要求 |
| --- | --- |
| CMake | 3.25+ |
| 编译器 | 支持 C++20 的 MSVC x64 工具链 |
| Ninja | 位于 `PATH` |
| Vulkan SDK | Vulkan 头文件、Loader、`glslc`；Debug 示例所用的 Validation Layers |
| clang-tidy | 使用 `msvc-debug-tidy` 时需要，加入 `PATH` |
| GPU 与驱动 | 支持所选示例请求的 API、特性、扩展和图像格式 |

Vulkan 开发工具可从 [LunarG](https://vulkan.lunarg.com/sdk/home#windows) 获取。安装 SDK 不会改变设备本身支持的特性。

在已初始化 MSVC x64 环境的开发终端中运行 CMake，也可以使用[辅助脚本](#辅助脚本)。

### Linux

Linux 预设使用 Ninja 和 CMake 检测到的本机编译器。构建需要 CMake 3.25+、支持项目所用 C++20 特性的编译器及标准库（例如 GCC 13）、Vulkan 头文件与 Loader、`glslc`，以及 GLFW 的 X11 开发依赖。运行图形示例需要可用的 X11 显示服务、Vulkan 驱动和 Debug 示例请求的 Validation Layers。

在 Ubuntu 24.04 上可安装以下软件包：

```bash
sudo apt install build-essential cmake ninja-build libvulkan-dev glslc \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev \
  vulkan-validationlayers
```

使用 `linux-debug-tidy` 时另需安装 `clang-tidy`，并确保它位于 `PATH`。

## 获取代码与资源

克隆仓库时带上 `--recurse-submodules`。对于已有工作副本，执行：

```powershell
git submodule update --init --recursive
```

`assets/` 是 [graphics-assets](https://github.com/parkerkit/graphics-assets) 子模块；第三方 C/C++ 库随主仓库保存，构建期间不下载依赖。

模型、纹理等按源码中的 `PROJECT_ASSETS_DIR` 路径加载。以下路径均相对于 `assets/`：

| 示例 | 资源路径 |
| --- | --- |
| OBJ 模型加载与后处理 | `models/viking_room/` |
| Mesh Shader | `models/apple.obj` |
| Cubemap | `textures/skybox/` |

资源子模块应与主仓库记录的提交匹配，运行前需准备对应示例使用的文件。资源许可见[资源说明](../assets/README.md)。

## 构建第一个程序

在仓库根目录执行对应平台的命令。Windows 使用 MSVC x64 开发终端。

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug --target 01_01_hello_triangle
.\build\msvc-debug\bin\01_01_hello_triangle.exe
```

Linux 使用以下命令：

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --target 01_01_hello_triangle
./build/linux-debug/bin/01_01_hello_triangle
```

CMake 为目标编译 GLSL、生成 SPIR-V，并编译和链接程序。构建其他示例时，从[示例目录](samples.md)选择 target。省略 `--target` 会构建全部启用的目标。

## 运行 Mesh Shader 示例

`06_04_mesh_shader` 的运行与编译要求如下：

| 项目 | 要求 |
| --- | --- |
| Vulkan API | 1.2 或更高版本 |
| 设备扩展 | `VK_EXT_mesh_shader` |
| 设备特性 | `meshShader`；不要求 `taskShader` |
| 设备上限 | 支持 32 个 invocation 的工作组、64 个输出顶点、128 个输出三角形及所需的描述符与输出内存 |
| Shader 编译器 | SDK 中的 `glslc` 支持 `GL_EXT_mesh_shader` |

设备筛选条件见[主程序](../samples/06_extensions/04_mesh_shader/main.cpp)中的 `IsDeviceSuitable()` 和 `SupportsMeshShaderLimits()`。准备上述资源后，在已初始化 MSVC x64 环境的终端中执行：

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug --target 06_04_mesh_shader
.\build\msvc-debug\bin\06_04_mesh_shader.exe
```

模型分块效果与相机操作见[效果展示](gallery.md#mesh-shader)，数据流见[Mesh Shader 与 meshlet](rendering-techniques.md#mesh-shader-与-meshlet)，着色器产物规则见[Shader 构建](project-layout.md#shader-构建)。

## 选择配置

| 类型 | Windows（MSVC x64） | Linux（本机编译器） | clang-tidy |
| --- | --- | --- | --- |
| Debug | `msvc-debug` | `linux-debug` | 关闭 |
| Debug + 静态检查 | `msvc-debug-tidy` | `linux-debug-tidy` | 开启 |
| Release | `msvc-release` | `linux-release` | 关闭 |

运行 `cmake --list-presets=all` 可以查看当前宿主系统的配置和构建预设。日常调试使用 Debug，构建优化版本使用 Release，需要随编译运行静态检查时使用 Debug + 静态检查。

配置和构建使用同名 preset。输出位于各自的 `build/<preset>/` 下：

| 输出 | 路径 |
| --- | --- |
| 可执行程序 | Windows 为 `bin/<target>.exe`，Linux 为 `bin/<target>` |
| 库 | `lib/` |
| SPIR-V | `bin/shaders/` |
| 编译数据库 | `compile_commands.json` |

个人路径与配置可以保存在已忽略的 `CMakeUserPresets.json` 中。

## 辅助脚本

### 自动初始化 MSVC 并构建

[build_debug_tidy.bat](../scripts/build_debug_tidy.bat) 优先寻找 VS 2026，再寻找 VS 2022，初始化 x64 环境并构建：

```powershell
.\scripts\build_debug_tidy.bat 01_01_hello_triangle
```

参数为可选 target；不提供参数时构建全部目标。脚本每次使用 `--fresh` 重新配置 `msvc-debug-tidy`，会重建 CMake 缓存。

### 生成 Visual Studio 解决方案

[generate_vs_sln.bat](../scripts/generate_vs_sln.bat) 默认使用 VS 2026：

```powershell
.\scripts\generate_vs_sln.bat
```

VS 2026 生成器需要 **CMake 4.2+**，见 [CMake 生成器文档](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html)。也可以传入 `vs2022` 选择 VS 2022。

解决方案分别写入 `build/vs2026/` 或 `build/vs2022/`；程序输出为 `bin/<配置>/`，Shader 输出为 `bin/shaders/`。构建时 clang-tidy 和提交检查所需的编译数据库使用 Ninja 配置；Visual Studio 生成器不提供这套检查流程。

## 资源路径与运行方式

CMake 将资源绝对路径写入编译定义：

| 定义 | 位置 |
| --- | --- |
| `PROJECT_ASSETS_DIR` | 源码目录的 `assets/` |
| `PROJECT_SHADER_DIR` | 当前构建目录的 `bin/shaders/` |

因此，读取这些资源不依赖启动目录；移动源码或构建目录后需要重新配置、构建。

Headless、部分 Vulkan-Hpp、Blit 和管线缓存示例会向进程工作目录写入图像或文件。运行它们时可以先进入输出目录：

```powershell
Set-Location build/msvc-debug/bin
.\04_01_render.exe
```

部分示例提供多个独立程序：`main.cpp` 对应原 target，`mainN.cpp` 对应原 target 名直接追加数字 `N`。例如，`02_04_pushconstants2` 使用 `main2.cpp`；选择对应 target 构建、运行即可，见[多个入口与目标](samples.md#多个入口与目标)。

## 常见问题

| 现象 | 处理方式 |
| --- | --- |
| 找不到 `cl` 或标准库头文件 | 使用 MSVC x64 开发终端，或调用自动初始化环境的脚本 |
| 配置时找不到 Vulkan 或 `glslc` | 安装 SDK，确认 `VULKAN_SDK` 与工具路径 |
| 配置时找不到 `clang-tidy` | 启用 `ENABLE_CLANG_TIDY` 时，将工具加入 `PATH` |
| 模型或纹理加载失败 | 初始化资源子模块，核对源码引用和文件路径 |
| 无法选出设备或创建特定功能 | 对照源码中的设备选择、扩展和 feature 请求，确认 GPU 与驱动支持 |
| 改动 Shader 后画面未变化 | 构建对应 target，并运行同一构建目录的可执行文件 |

开发工具配置和提交 hook 的行为见[开发指南](development.md)。
