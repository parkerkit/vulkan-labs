# 第三方依赖

[项目首页](../README.md) · [工程组织](../docs/project-layout.md) · [开发指南](../docs/development.md)

本目录保存依赖源码与本地 CMake 适配，配置、构建期间不下载依赖。Vulkan 的头文件和 Loader 来自本地 SDK，通过 `Vulkan::Vulkan` 使用。

## 依赖清单

版本以源码声明为准；tinyobjloader 和 TinyGLTF 同时记录所使用的下载 tag。头文件版本与完整仓库提交是不同的标识。

| 依赖 | 版本 / tag | CMake target | 集成方式 |
| --- | --- | --- | --- |
| [GLFW](https://github.com/glfw/glfw) | 3.4.0 | `ThirdParty::glfw` | 静态库，窗口与输入 |
| [GLM](https://github.com/g-truc/glm) | 1.0.3 | `ThirdParty::glm` | 头文件，数学运算 |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.89.8 WIP | `ThirdParty::imgui` | 静态库，core / demo 与 GLFW、Vulkan backend |
| [meshoptimizer](https://github.com/zeux/meshoptimizer) | 1.2 | `ThirdParty::meshoptimizer` | 静态库，meshlet 构建与网格优化 |
| [stb](https://github.com/nothings/stb) | stb_image 2.30；stb_image_write 1.16 | `ThirdParty::stb_image`、`ThirdParty::stb_image_write` | 两个静态库，图像读写 |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) | `v2.0.0rc13` | `ThirdParty::tiny_obj` | 头文件接口，OBJ 解析 |
| [TinyGLTF](https://github.com/syoyo/tinygltf) | `v2.9.0` | `ThirdParty::tiny_gltf` | 头文件接口，glTF 解析 |
| [JSON for Modern C++](https://github.com/nlohmann/json) | 3.10.4 | `nlohmann_json::nlohmann_json`、`ThirdParty::nlohmann_json` | 头文件接口，JSON 解析 |

## 使用 target

在需要的 target 上声明依赖：

```cmake
target_link_libraries(my_target PRIVATE
    Vulkan::Vulkan
    ThirdParty::glfw
    ThirdParty::glm
    ThirdParty::imgui
)
```

`cmake/` 模块负责传播头文件与链接要求，调用方不拼接本地库文件路径。

| 头文件 | 对应 target |
| --- | --- |
| `<GLFW/glfw3.h>` | `ThirdParty::glfw` |
| `<glm/glm.hpp>` | `ThirdParty::glm` |
| `<imgui.h>` | `ThirdParty::imgui` |
| `<meshoptimizer.h>` | `ThirdParty::meshoptimizer` |
| `<stb_image.h>` | `ThirdParty::stb_image` |
| `<stb_image_write.h>` | `ThirdParty::stb_image_write` |
| `<tiny_obj/tiny_obj_loader.h>` | `ThirdParty::tiny_obj` |
| `<tiny_gltf/tiny_gltf.h>` | `ThirdParty::tiny_gltf` |
| `<nlohmann/json.hpp>` | `nlohmann_json::nlohmann_json` |

## 单头文件库与实现归属

stb 的头文件集中在 `stb/`。实现宏只在 `src/stb_image.cpp` 和 `src/stb_image_write.cpp` 中定义，各自编译一次；使用者链接静态库。

TinyGLTF 依赖共享的 stb 和 JSON target。JSON 唯一源码位于 `nlohmann/include/nlohmann/json.hpp`；TinyGLTF 上游的 `"json.hpp"` include 由适配模块提供兼容路径。项目代码使用 `<nlohmann/json.hpp>`。

tinyobjloader 和 TinyGLTF 的 implementation 宏由使用它们的程序在一个翻译单元中定义。每个可执行程序各自管理这些实现，避免在同一程序内重复定义。

## 构建边界

`third_party/CMakeLists.txt` 统一加载 `cmake/` 中各依赖的适配模块。调用方通过 `ThirdParty::` 别名链接，头文件路径和链接要求由 target 传播。

| 模块 | 构建职责 |
| --- | --- |
| [glfw.cmake](cmake/glfw.cmake) | 调用 `glfw/CMakeLists.txt`，编译平台窗口与输入实现 |
| [glm.cmake](cmake/glm.cmake) | 通过接口 target 提供 GLM 头文件 |
| [imgui.cmake](cmake/imgui.cmake) | 编译 ImGui core / demo 与 GLFW、Vulkan backend，声明 GLFW 和 Vulkan 链接依赖 |
| [meshoptimizer.cmake](cmake/meshoptimizer.cmake) | 显式列出 `meshoptimizer/src/` 源码并编译为静态库 |
| [stb.cmake](cmake/stb.cmake) | 分别编译图像读取与写入的共享实现 |
| [tiny_obj.cmake](cmake/tiny_obj.cmake)、[tiny_gltf.cmake](cmake/tiny_gltf.cmake)、[nlohmann_json.cmake](cmake/nlohmann_json.cmake) | 提供头文件接口及其传递依赖 |

`third_party_setup_target()` 为第三方编译 target 关闭 clang-tidy、编译告警和告警视为错误。第三方 include 路径使用 `SYSTEM`；提交 hook 也排除整个本目录。

TinyGLTF 使用 nlohmann/json 与 stb；当前配置没有启用 RapidJSON、Draco 或 Android 资源接口。tinyobjloader 使用默认三角化路径，没有启用 Mapbox Earcut。

## 许可

根目录 MIT License 不替代依赖自身的许可。随附的许可文本与源码声明共同保留：

| 依赖 | 许可文件 |
| --- | --- |
| GLFW | [LICENSE.md](glfw/LICENSE.md) |
| GLM | [copying.txt](glm/copying.txt) |
| Dear ImGui | [LICENSE.txt](imgui/LICENSE.txt) |
| meshoptimizer | [LICENSE.md](meshoptimizer/LICENSE.md) |
| stb | [LICENSE](stb/LICENSE) |
| tinyobjloader | [LICENSE](tiny_obj/LICENSE) |
| TinyGLTF | [LICENSE](tiny_gltf/LICENSE) |
| nlohmann/json | [LICENSE.MIT](nlohmann/LICENSE.MIT)，以及 [上游说明](nlohmann/README.md)中的附带组件声明 |

## 更新依赖

更新时记录来源、版本或提交，保留授权文件、源码声明和必要平台文件。构建适配放在对应 CMake 模块中；改变上游 API、backend 或可选功能时，同步检查使用处和新增依赖的许可。
