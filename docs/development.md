# 开发指南

[文档](README.md) · [工程组织](project-layout.md) · [构建与运行](getting-started.md)

## 阅读与修改入口

一个示例的 `main.cpp` 组织初始化、资源与帧流程；存在多种实现时，其他版本使用 `main2.cpp`、`main3.cpp` 等独立入口，文件顶部的 `//` 注释说明各版本的用途。`shaders/` 保存相应 GLSL。修改时同时跟踪 CPU 数据、Shader 声明、描述符布局、附件用途和同步关系。

- 调整画面与算法：修改对应示例及 Shader。
- 调整公共编译策略：修改根 `cmake/` 模块。
- 调整第三方接入：修改 `third_party/cmake/` 及其依赖声明。
- 调整运行资源：在 `assets/` 子模块中管理，主仓库记录子模块提交。

共享构建配置通过 target 传播；示例的资源和渲染流程由自身管理。[工程组织](project-layout.md)说明具体接口。

## clang-tidy

使用 `msvc-debug-tidy` 或 `linux-debug-tidy` 构建时，CMake 对参与编译的自有 target 运行 `clang-tidy`。直接配置 CMake 时，`ENABLE_CLANG_TIDY` 默认开启。第三方头文件通过 `SYSTEM` include 接入，第三方 target 关闭这项检查。

检查集合、命名约定和头文件过滤见根目录 [.clang-tidy](../.clang-tidy)。普通构建按该配置报告诊断；提交 hook 另外通过 `--warnings-as-errors` 将选定诊断提升为错误。

构建时检查以参与编译的翻译单元为单位。修改头文件后，重新构建包含该头文件的 target，可以检查它在真实编译上下文中的用法。

## pre-commit

安装 Python 3.10+、`clang-format` 和 `clang-tidy`，并将工具加入 `PATH`：

```powershell
python -m pip install pre-commit PyYAML
pre-commit install
```

配置见 [.pre-commit-config.yaml](../.pre-commit-config.yaml)，本地 hook 使用当前 Python 环境。

| 检查 | 行为 |
| --- | --- |
| C/C++ 格式化 | 使用根目录 `.clang-format`，可能直接修改文件 |
| 文本风格 | 去除行尾空白、统一换行并补齐文件末尾换行 |
| 编码 | 要求 UTF-8 无 BOM 或 ASCII |
| JSON / YAML | 解析并检查语法 |
| clang-tidy | 检查编译数据库中的变更源文件 |
| 大文件 | 单文件不超过 1 MiB |

`third_party/`、`assets/`、`build/`、`out/` 被排除。格式化或文本 hook 修改文件后，需要审阅并重新暂存。

### 编译数据库与头文件

[clang_tidy_check.py](../hooks/clang_tidy_check.py) 从仓库根目录依次查找：

1. `build/msvc-debug-tidy/compile_commands.json`
2. `build/msvc-debug/compile_commands.json`
3. `build/msvc-release/compile_commands.json`
4. `build/` 下按路径排序的第一个编译数据库。

hook 使用首个找到的数据库；调整 target 或配置后，应更新对应构建目录，避免使用过期条目。

| 输入或条件 | 结果 |
| --- | --- |
| 数据库中的 C/C++ 源文件 | 按数据库中的编译上下文运行 tidy |
| 头文件 | 不作为独立翻译单元检查，也不触发全库扫描 |
| 没有数据库，或源文件未被收录 | 警告并跳过相应检查 |
| 找不到 clang-tidy，或实际检查失败 | 返回失败 |

## 编码与构建约定

格式以 [.clang-format](../.clang-format) 为准；其他文本遵循 [.editorconfig](../.editorconfig) 与 [.gitattributes](../.gitattributes)。文本使用 UTF-8 无 BOM，通常使用 LF，`.bat` / `.cmd` 使用 CRLF。

## 文档与媒体

文档中的功能、命令和参数应能对应当前源码。新增内容根据用途放入效果展示、使用指南、示例索引或专题说明，共用概念通过链接引用。

截图和视频保存在 `docs/screenshots/`，文件名采用 `<target>[_<模式>].<扩展名>`。对比画面保持相机、场景和尺寸一致，用说明文字标明附件、滤镜或曝光参数。

图片使用相对路径和有意义的替代文本；MP4 通过文字链接或静态预览图链接。媒体与其他非排除文件一样受 1 MiB 单文件限制。素材的公开使用还应遵循相关模型、纹理和其他资源的许可。
