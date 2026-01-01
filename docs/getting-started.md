# 构建与运行

[项目首页](../README.md) · [示例目录](samples.md) · [开发指南](development.md)

## 环境

公共预设使用 **Windows、MSVC x64 与 Ninja**。安装 Visual Studio 2026 的 C++ 桌面开发工具和 Windows SDK，并准备以下工具：

| 依赖 | 要求 |
| --- | --- |
| CMake | 3.25+ |
| 编译器 | 支持 C++20 的 MSVC x64 工具链 |
| Ninja | 位于 `PATH` |
| Vulkan SDK | Vulkan 头文件、Loader、`glslc`；Debug 示例所用的 Validation Layers |
| clang-tidy | 位于 `PATH`，默认参与自有 C/C++ target 的编译 |
| GPU 与驱动 | 支持所选示例请求的 API、特性、扩展和图像格式 |

Vulkan 开发工具可从 [LunarG](https://vulkan.lunarg.com/sdk/home#windows) 获取。安装 SDK 不会改变设备本身支持的特性。

CMake 预设不初始化 MSVC 环境。以下命令在仓库根目录、已初始化 MSVC x64 的开发终端中执行；也可以使用[辅助脚本](#辅助脚本)。

## 获取代码与资源

克隆仓库时带上 `--recurse-submodules`。对于已有工作副本，执行：

```powershell
git submodule update --init --recursive
```

`assets/` 是 [graphics-assets](https://github.com/parkerkit/graphics-assets) 子模块；第三方 C/C++ 库随主仓库保存，构建期间不下载依赖。

模型、纹理等按源码中的 `PROJECT_ASSETS_DIR` 路径加载。例如，模型及后处理示例使用 `models/viking_room/`，Cubemap 使用 `textures/skybox/`。资源子模块应与主仓库记录的提交匹配，许可见[资源说明](../assets/README.md)。

## 构建第一个程序

```powershell
cmake --preset msvc-debug-tidy
cmake --build --preset msvc-debug-tidy --target 01_01_hello_triangle
.\build\msvc-debug-tidy\bin\01_01_hello_triangle.exe
```

CMake 为目标编译 GLSL、生成 SPIR-V，并编译和链接程序。选择其他主题时替换 target：

```powershell
cmake --build --preset msvc-debug-tidy --target 02_08_deferred_shading
.\build\msvc-debug-tidy\bin\02_08_deferred_shading.exe
```

全部名称见[示例目录](samples.md)。省略 `--target` 会构建全部启用的目标。

## 选择配置

| Preset | 类型 | 编译器 / 生成器 | clang-tidy |
| --- | --- | --- | --- |
| `msvc-debug` | Debug | MSVC x64 / Ninja | 开启 |
| `msvc-debug-tidy` | Debug | MSVC x64 / Ninja | 开启，显式声明 |
| `msvc-release` | Release | MSVC x64 / Ninja | 开启 |

配置和构建使用同名 preset。输出位于各自的 `build/<preset>/` 下：

| 输出 | 路径 |
| --- | --- |
| 可执行程序 | `bin/<target>.exe` |
| 库 | `lib/` |
| SPIR-V | `bin/shaders/` |
| 编译数据库 | `compile_commands.json` |

根构建选项为 `BUILD_SAMPLES=ON` 和 `ENABLE_CLANG_TIDY=ON`。需要关闭静态检查时，在配置阶段显式覆盖：

```powershell
cmake --preset msvc-debug -DENABLE_CLANG_TIDY=OFF
```

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
Set-Location build/msvc-debug-tidy/bin
.\04_01_render.exe
```

部分示例提供多个独立程序：`main.cpp` 对应原 target，`mainN.cpp` 对应原 target 名直接追加数字 `N`。例如，`02_04_pushconstants2` 使用 `main2.cpp`；选择对应 target 构建、运行即可，见[多个入口与目标](samples.md#多个入口与目标)。

## 常见问题

| 现象 | 处理方式 |
| --- | --- |
| 找不到 `cl` 或标准库头文件 | 使用 MSVC x64 开发终端，或调用自动初始化环境的脚本 |
| 配置时找不到 Vulkan 或 `glslc` | 安装 SDK，确认 `VULKAN_SDK` 与工具路径 |
| 配置时找不到 `clang-tidy` | 将工具加入 `PATH`；该工具在默认配置中是必需项 |
| 模型或纹理加载失败 | 初始化资源子模块，核对源码引用和文件路径 |
| 无法选出设备或创建特定功能 | 对照源码中的设备选择、扩展和 feature 请求，确认 GPU 与驱动支持 |
| 改动 Shader 后画面未变化 | 构建对应 target，并运行同一构建目录的可执行文件 |

开发工具配置和提交 hook 的行为见[开发指南](development.md)。
