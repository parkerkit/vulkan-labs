# Vulkan 基础

[文档](README.md) · [专题实现](rendering-techniques.md) · [示例目录](samples.md)

本文介绍 Vulkan 对象、一次绘制的数据流及资源的使用与同步，结合仓库的 Dynamic Rendering 与 Render Pass 路径说明公共概念。具体技术组合见专题实现。

## 对象与生命周期

| 对象 | 职责与依赖 |
| --- | --- |
| `VkInstance` | 声明 API 版本、实例扩展与验证层，连接 Vulkan 实现 |
| `VkSurfaceKHR` | 连接窗口系统，供设备查询呈现能力 |
| `VkPhysicalDevice` | 提供队列族、扩展、feature、格式和限制信息 |
| `VkDevice` / `VkQueue` | 启用所需设备能力，创建资源并提交工作 |
| `VkSwapchainKHR` | 提供呈现图像，配置格式、尺寸、usage 与呈现模式 |
| Buffer / Image / 内存 | 保存几何、参数、纹理和附件数据 |
| 视图、描述符、管线 | 解释资源、连接 Shader 绑定并描述执行状态 |
| 命令与同步对象 | 组织工作、建立依赖并管理在途资源 |

实例与设备扩展在各自创建阶段声明，设备 feature 还需显式启用。支持某个 Vulkan 版本，并不等于程序请求的所有可选能力都已启用。

对象销毁需要同时满足依赖与执行条件：GPU 已结束相关访问，引用该对象的使用也已结束。例如，Framebuffer 引用 Image View，Image 绑定设备内存；释放顺序需要先处理引用方，再销毁资源并释放内存。

无窗口渲染和计算不需要 Surface 与交换链，其余设备、资源和同步责任仍然存在。

## 图形管线与附件

### 一次绘制的数据流

下图展示 Vulkan 传统图元管线（Primitive Pipeline）的逻辑顺序。标记为可选的阶段按管线配置执行。不同顶点、图元和片元可以并行处理，图中顺序表示逻辑上的数据依赖关系，不代表 GPU 必须串行执行所有操作。

```text
绘制命令（Draw Command）
  → 顶点输入与图元组装（Vertex Input / Input Assembly）
       索引绘制从 Index Buffer 读取索引，并处理图元重启
       索引经过 vertexOffset 调整后用于顶点属性访问
       顶点属性根据 Vertex Input Layout 读取
       图元拓扑决定如何组织点、线、三角形或 Patch
  → 顶点着色器（Vertex Shader）
  → 细分控制着色器 → 固定功能细分器 → 细分求值着色器（可选，成组启用）
  → 几何着色器（Geometry Shader，可选）
  → 顶点后处理（Vertex Post-Processing）
       Transform Feedback（可选）
       → Viewport Swizzle / Flat Shading（按配置执行）
       → 图元裁剪（Primitive Clipping）
       → 为裁剪产生的新顶点计算输出属性
       → 透视除法（Clip Coordinates → NDC）
       → 视口变换（NDC → Framebuffer Coordinates）
       → 正反面判断（Front Face Determination）
  → 光栅化（Rasterization）
       面剔除、多边形模式、Rasterizer Discard
       深度偏移、线宽、采样覆盖（Sample Coverage）
  → 片元处理（Fragment Processing）
       覆盖操作（Scissor、Sample Mask 等，具体时序见下节）
       提前深度／模板测试（适用时）
       → 片元着色器（Fragment Shader，Sample Shading 控制调用频率）
       → 延后深度／模板测试（适用时）
  → 颜色附件操作（Color Attachment Operations）
       混合或逻辑操作（二者按配置选择）
       → 颜色写入掩码 → 颜色附件写入
  → 附件解析（Attachment Resolve，按需执行）
```

- **绘制命令（Draw Command）** 触发一次或一批绘制。`vkCmdDraw` 和 `vkCmdDrawIndexed` 的绘制参数由 CPU 在记录命令时提供。`vkCmdDrawIndirect` 和 `vkCmdDrawIndexedIndirect` 从缓冲区读取绘制参数；带 `Count` 的间接绘制命令还从 Count Buffer 读取实际绘制数量，并受 `maxDrawCount` 限制。绘制命令决定顶点数量、索引数量、实例数量及起始位置等参数。

- **顶点输入与图元组装（Vertex Input / Input Assembly）** Vertex Input 根据绑定的 Vertex Buffer、Index Buffer 和 Vertex Input Layout 获取顶点属性。对于索引绘制，首先从 Index Buffer 读取索引值。启用 Primitive Restart 时，特殊索引值用于切断当前图元序列并重新开始图元组装。重启索引的判断发生在添加 `vertexOffset` 之前；普通索引经过 `vertexOffset` 调整后，形成用于顶点属性访问的顶点索引。非索引绘制则使用连续生成的顶点索引。Primitive Topology 决定如何将顶点组织成点、线、三角形或 Patch。需要注意，`indexCount` 表示索引数量，而不是唯一顶点数量。

- **光栅化前的着色器（Pre-Rasterization Shader Stages）** 处理顶点和图元。顶点着色器输出裁剪坐标和顶点属性。细分控制着色器为 Patch 设置细分级别和控制点相关数据，固定功能细分器在参数空间生成细分顶点及图元，细分求值着色器根据参数坐标计算细分后的顶点属性和位置。几何着色器接收一个输入图元，可以输出零个或多个图元。细分控制着色器、固定功能细分器和细分求值着色器必须成组启用，且需要 Patch List 拓扑。

- **顶点后处理（Vertex Post-Processing）** 对最后一个启用的光栅化前着色器输出的顶点和图元执行固定功能操作。启用 Transform Feedback 时，先将最后一个光栅化前着色器的指定输出捕获到缓冲区，再进行其他固定功能顶点后处理。后续操作包括 Viewport Swizzle、Flat Shading、图元裁剪、透视除法和视口变换等。图元裁剪产生新顶点时，管线为这些顶点计算输出属性，再进行透视除法。透视除法将 Clip Coordinates 转换为 NDC，视口变换将 NDC 转换为 Framebuffer Coordinates。对于多边形图元，正反面判断（Front Face Determination）根据视口变换后的坐标绕序进行。

- **光栅化（Rasterization）** 将点、线和多边形图元转换为片元（Fragment），并确定每个片元覆盖的采样点。`cullMode` 和 `frontFace` 控制多边形面剔除及正面判定，`polygonMode` 控制多边形以填充、线框或点的形式光栅化。Rasterizer Discard 可以在光栅化之前丢弃图元，使其不产生后续片元。Depth Bias 根据配置调整深度值，主要用于 Shadow Mapping 等场景。

- **多重采样（Multisampling / MSAA）** 不是一个独立的管线阶段，而是贯穿光栅化、片元处理和附件操作的机制。`rasterizationSamples` 决定每个像素的采样数量。光栅化阶段根据多个 Sample Position 计算覆盖掩码（Coverage Mask）；Sample Mask 和 Alpha To Coverage 等机制可以进一步影响采样覆盖。Sample Shading 控制片元着色器的调用频率。启用 `sampleShadingEnable` 时，`minSampleShading` 用于确定每个片元的最少着色调用次数。深度、模板和颜色等数据可以按 Sample 分别处理。最终可以通过 Resolve 将多重采样附件解析为单采样附件。

- **片元处理（Fragment Processing）** 接收光栅化产生的片元，执行片元相关操作及可选的 Fragment Shader。Fragment Shader 根据插值后的输入计算颜色及可选的深度、模板参考值等输出。`smooth` 输入使用透视校正插值，`noperspective` 使用非透视校正插值，`flat` 输入使用图元指定的 Provoking Vertex 值。Scissor Test、Sample Mask Test、Depth Bounds Test、Stencil Test 和 Depth Test 等 Fragment Operations 根据规范规定的顺序及执行条件进行。深度和模板测试可以在 Fragment Shader 之前或之后执行，具体取决于 Early Fragment Tests、Shader 输出及相关状态配置。片元着色与测试的相对顺序见[深度、模板与混合的顺序](#深度模板与混合的顺序)。

- **颜色附件操作（Color Attachment Operations）** 对 Fragment Shader 输出的颜色执行后续处理。根据 Pipeline 配置，可以执行 Blending 或 Logic Operation；两者是不同的颜色处理模式。Color Write Mask 控制 R、G、B、A 各颜色通道是否允许写入，最终结果写入对应的 Color Attachment。

- **附件解析（Attachment Resolve）** 按 Render Pass Subpass 或 Dynamic Rendering 的附件配置执行。对于 MSAA Color Attachment，可以将每个像素的多个 Sample 解析为单采样颜色结果。Depth 和 Stencil Attachment 也可以在支持并配置相应 Resolve Mode 时执行解析。传统 Render Pass 可以在 Subpass 中配置 Resolve Attachment；Dynamic Rendering 则通过 `VkRenderingAttachmentInfo` 的 `resolveMode`、`resolveImageView` 和 `resolveImageLayout` 配置解析目标，通常在 Rendering 实例结束时执行。没有配置 Resolve Attachment 时，不会自动发生 MSAA Resolve。

参考 [Vulkan 绘制命令](https://docs.vulkan.org/spec/latest/chapters/drawing.html) · [细分](https://docs.vulkan.org/spec/latest/chapters/tessellation.html) · [几何着色](https://docs.vulkan.org/spec/latest/chapters/geometry.html) · [顶点后处理](https://docs.vulkan.org/spec/latest/chapters/vertexpostproc.html) · [光栅化与多重采样](https://docs.vulkan.org/spec/latest/chapters/primsrast.html) · [片元操作](https://docs.vulkan.org/spec/latest/chapters/fragops.html) · [Render Pass 与附件解析](https://docs.vulkan.org/spec/latest/chapters/renderpass.html#renderpass-resolve-operations)。

`VK_EXT_mesh_shader` 提供另一条光栅化前路径。Mesh Shader Pipeline 不使用传统 Vertex Input、Input Assembly、Vertex Shader、Tessellation 和 Geometry Shader 阶段。绘制命令通过 `vkCmdDrawMeshTasksEXT` 或间接绘制命令启动 Mesh Shader 工作组。可选的 Task Shader 可以分派一个或多个 Mesh Shader 工作组；Mesh Shader 直接生成顶点和图元，再进入后续的图元裁剪、透视除法、视口变换、正反面判断和光栅化等处理。Task Shader 不是必需的，Mesh Shader 可以直接启动。

参考 [Vulkan Task 和 Mesh Shader](https://docs.vulkan.org/spec/latest/chapters/shaders.html#shaders-mesh) · [仓库 Mesh Shader 示例](../samples/06_extensions/04_mesh_shader/main.cpp)。

### 深度、模板与混合的顺序

Scissor Test 根据帧缓冲坐标限制片元覆盖区域，Sample Mask Test 可以进一步屏蔽采样点。它们属于 Fragment Operations，而不是独立的 Shader Stage。对于普通光栅化流程，这些操作通常在深度和模板测试之前执行。

深度范围测试（Depth Bounds Test）比较深度附件中对应采样点的已有深度与 `[minDepthBounds, maxDepthBounds]`。已有深度超出范围时，该采样点失去覆盖。使用此测试需要设备支持并启用 `depthBounds` 特性。

启用模板和深度测试时，Stencil Test 先于 Depth Test。模板测试使用模板参考值（Reference）、比较掩码（Compare Mask）和深度模板附件中的模板值进行比较。根据模板测试是否失败、深度测试是否失败或两项测试均通过，分别选择 `failOp`、`depthFailOp` 或 `passOp` 更新模板值。模板写入还受 Write Mask 控制。

深度测试比较当前片元的深度值与深度附件中对应 Sample 的已有深度值。比较操作由 `depthCompareOp` 决定。测试通过后，只有在 `depthTestEnable` 和 `depthWriteEnable` 均启用的情况下，才会更新深度附件。深度测试关闭时，深度写入也不会执行。

片元着色与深度、模板操作的相对顺序取决于 Shader 执行模式、Pipeline 状态和相关规范条件。下图展示两种典型时序，省略 Scissor、Sample Mask 等覆盖操作：

```text
提前测试（Early Fragment Tests）
  光栅化 → 提前的深度／模板测试 → 片元着色 → 颜色附件操作

延后测试（Late Fragment Tests）
  光栅化 → 片元着色 → 延后的深度／模板测试 → 颜色附件操作
```

- **提前测试（Early-Z）** 可以在着色前拒绝未通过深度或模板测试的采样点，减少无效的 Fragment Shader 执行。GPU 也可以在不改变 Vulkan 可观察结果的前提下，采用提前深度测试或保守的提前拒绝优化。需要区分硬件内部的 Early-Z 优化与 Shader 显式声明的 Early Fragment Tests。

- **延后测试（Late Fragment Tests）** 在 Fragment Shader 执行之后进行相关测试，因此可以使用 Shader 输出的 `gl_FragDepth` 作为深度测试输入，也可以在 Shader 执行 `discard` 后，根据剩余覆盖情况进行后续深度和模板操作。

- **显式提前测试（Explicit Early Fragment Tests）** 由 GLSL 的 `layout(early_fragment_tests) in;` 请求。此时深度和模板测试在 Fragment Shader 执行之前进行，使用光栅化产生的深度值，而不是之后可能写出的 `gl_FragDepth`。如果 Fragment Shader 随后执行 `discard`，可以阻止相应颜色输出，但已经发生的深度或模板附件更新不会因此撤销。

- **模板测试（Stencil Test）** 先于 Depth Test。模板测试失败时执行 `failOp`；模板测试通过但深度测试失败时执行 `depthFailOp`；两项均通过时执行 `passOp`。模板更新还受 `writeMask` 限制。模板测试和深度测试都可以独立启用或关闭。

- **混合与颜色写入（Blending / Color Write）** 在相关 Fragment Operations 完成后执行。启用 Blending 时，颜色附件中已有的颜色与 Fragment Shader 输出按照混合因子和混合操作组合。启用 Logic Operation 时则执行颜色逻辑运算，Blending 和 Logic Operation 是两种不同的处理模式。Color Write Mask 决定最终允许更新哪些颜色通道。

需要注意，Early Fragment Tests 不意味着所有 Fragment Operations 都可以任意提前。Vulkan 对 Scissor、Sample Mask、Depth Bounds、Stencil、Depth、Sample Counting 等操作规定了相应的顺序和条件。GPU 可以采用不同的内部执行方式，但必须满足规范要求的可观察行为。

参考 [Vulkan 片元操作](https://docs.vulkan.org/spec/latest/chapters/fragops.html) · [Vulkan 颜色混合](https://docs.vulkan.org/spec/latest/chapters/framebuffer.html#framebuffer-blending) · [Vulkan 深度测试指南](https://docs.vulkan.org/guide/latest/depth.html)。

### 管线状态与附件

`VkPipeline` 组合着色器及顶点输入、输入组装、光栅化、深度模板、多重采样、颜色混合等 Pipeline State。渲染命令指定实际使用的附件和执行的绘制操作。需要区分 Pipeline 创建时声明的兼容信息与 Rendering 时实际绑定的附件。

| 渲染方式 | 管线兼容信息与实际附件 |
| --- | --- |
| Dynamic Rendering | 创建 Pipeline 时通过 `VkPipelineRenderingCreateInfo` 声明 Color、Depth 和 Stencil Attachment 的格式及相关信息。`vkCmdBeginRendering` 通过 `VkRenderingInfo` 和 `VkRenderingAttachmentInfo` 提供实际使用的 Image View、布局、Load / Store 操作以及可选的 Resolve 配置 |
| Render Pass | 创建 Pipeline 时指定兼容的 Render Pass 和 Subpass。Render Pass 描述附件格式、用途、Subpass 使用关系及附件之间的依赖；Framebuffer 提供实际使用的 Image View 和尺寸 |

Fragment Shader 的颜色输出位置（`layout(location = N)`）与 Color Attachment 的索引对应，输出位置 N 对应 `pColorAttachments[N]`。对应位置还需要有兼容的颜色格式和相应的 Pipeline 状态配置。

深度测试需要实际绑定 Depth Attachment，并且 Pipeline 声明的 Depth Attachment Format 应与实际使用的附件格式兼容。Stencil 操作同样需要对应的 Stencil Attachment。对于不需要深度或模板操作的 Rendering，可以不绑定相应附件，但不能在缺少对应附件时执行依赖该附件的测试或读写操作。

Dynamic Rendering 中，`VkPipelineRenderingCreateInfo` 声明 Pipeline 预期使用的附件格式，而 `VkRenderingInfo` 提供本次 Rendering 实际使用的附件。两者需要满足 Dynamic Rendering 的格式兼容规则。绑定 `pDepthAttachment = nullptr` 表示本次 Rendering 不提供 Depth Attachment，并不等于 Pipeline 自动关闭深度测试；是否启用深度测试仍由 Pipeline State 控制。

框架的 Basic 示例通过 Dynamic Rendering 绑定颜色和深度附件。顶点着色器写出裁剪坐标和 UV，片元着色器用插值后的 UV 采样纹理，管线按深度状态处理覆盖并写入颜色。附件布局转换和访问同步由应用录制。

源码 [Basic 绘制](../showcases/cpu_driven_rendering/basic/sample.cpp) · [顶点着色器](../showcases/cpu_driven_rendering/basic/shaders/base.vert) · [片元着色器](../showcases/cpu_driven_rendering/basic/shaders/base.frag) · [框架管线状态](../framework/src/pipeline.cpp)

## 坐标空间与深度

### 裁剪、透视除法与视口变换

模型矩阵把模型坐标转到世界空间，观察矩阵把世界坐标转到观察空间，投影矩阵再生成齐次裁剪坐标 `(xc, yc, zc, wc)`。使用列向量时，顶点着色器按 `P × V × M × position` 计算。固定功能阶段随后按以下顺序处理图元。

1. **齐次裁剪** 使用裁剪坐标和 `wc` 确定图元落在裁剪体中的部分。
2. **透视除法** 用 `wc` 除前三个分量，得到 NDC 坐标 `(xc/wc, yc/wc, zc/wc)`。
3. **视口变换** 把 NDC 映射到帧缓冲坐标 `(xf, yf, zf)`，随后交给光栅化。

| 属性 | Vulkan 默认深度裁剪、正宽高视口 | OpenGL 默认约定 |
| --- | --- | --- |
| 裁剪空间 X、Y | `[-wc, wc]` | `[-wc, wc]` |
| 裁剪空间 Z | `[0, wc]` | `[-wc, wc]` |
| NDC X、Y | `[-1, 1]` | `[-1, 1]` |
| NDC Z | `[0, 1]` | `[-1, 1]` |
| 帧缓冲坐标原点 | 左上 | 左下 |
| 帧缓冲 +X / +Y | 向右 / 向下 | 向右 / 向上 |

模型、世界与观察空间的手性由应用选择。OpenGL 可用 `glClipControl` 调整原点和裁剪深度约定。Vulkan 可用负高度视口反转 NDC Y 到帧缓冲 Y 的映射，也可启用 `VK_EXT_depth_clip_control` 选择 `[-1, 1]` 的 NDC 深度范围。

参考 [Vulkan 顶点后处理](https://docs.vulkan.org/spec/latest/chapters/vertexpostproc.html) · [OpenGL glClipControl](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glClipControl.xhtml)

### Vulkan 视口与正面绕序

在默认 `[0, 1]` 深度约定下，`VkViewport` 使用下列公式计算帧缓冲坐标。

```text
xf = viewport.x + (xNdc + 1) × viewport.width / 2
yf = viewport.y + (yNdc + 1) × viewport.height / 2
zf = viewport.minDepth + zNdc × (viewport.maxDepth - viewport.minDepth)
```

覆盖从 `(0, 0)` 开始的整张附件时，正高度视口使用 `y = 0`、`height = H`。负高度视口使用 `y = H`、`height = -H`，覆盖同一区域并反转 Y 映射。

框架的 `OrbitCamera` 使用 `glm::perspectiveRH_ZO` 生成右手观察空间对应的 `[0, 1]` 投影深度，再通过 `projection[1][1] *= -1` 翻转一次 Y。Basic 示例配合正高度视口使用这一投影。GLM 的通用投影函数也可以通过 `GLM_FORCE_DEPTH_ZERO_TO_ONE` 选择 `[0, 1]` 深度约定。

Vulkan 根据三角形在帧缓冲坐标中的有向面积判断正反面。对投影或视口做一次 Y 翻转会改变面积符号，`frontFace` 需要与网格绕序和坐标变换匹配。框架默认使用 `VK_FRONT_FACE_COUNTER_CLOCKWISE` 和背面剔除。

参考 [VkViewport](https://docs.vulkan.org/refpages/latest/refpages/source/VkViewport.html) · [Vulkan 光栅化](https://docs.vulkan.org/spec/latest/chapters/primsrast.html) · [框架相机投影](../framework/src/orbit_camera.cpp) · [框架管线默认值](../framework/include/framework/pipeline.h)

### 深度映射与附件

NDC Z 规定透视除法后的深度坐标范围。`minDepth` 和 `maxDepth` 决定如何把它映射为帧缓冲深度。深度附件格式决定深度值如何存储及其精度，例如 `VK_FORMAT_D32_SFLOAT`。常规透视投影将沿视轴的观察空间深度非线性地映射为帧缓冲深度。在框架的右手观察空间中，视轴朝向负 Z，轴向深度为 `-z_view`。同一观察空间 Z 平面上的点可以距相机远近不同，但映射到相同的深度值。

| 深度方案 | 近处 / 远处 | 常见清除值 | 常见比较操作 |
| --- | --- | --- | --- |
| 常规前向深度 | 较小 / 较大 | `1.0` | `VK_COMPARE_OP_LESS` |
| Reverse-Z | 较大 / 较小 | `0.0` | `VK_COMPARE_OP_GREATER` |

Basic 示例使用 `minDepth = 0`、`maxDepth = 1` 和常规前向深度。Reverse-Z 可以保留 Vulkan 默认的 NDC Z 范围，通过调整映射和比较方向实现。

参考 [Vulkan Guide：Depth](https://docs.vulkan.org/guide/latest/depth.html) · [Basic 深度附件和视口](../showcases/cpu_driven_rendering/basic/sample.cpp)

## 描述符与管线布局

描述符连接 Shader 声明与实际资源，Pipeline Layout 声明描述符集布局及 Push Constant 范围。

| 对象或结构 | 作用 |
| --- | --- |
| `VkDescriptorSetLayout` | 定义 binding、类型、数量和访问阶段 |
| `VkDescriptorPool` | 提供描述符集及各类型描述符的容量 |
| `VkDescriptorSet` | 按 layout 分配，保存资源绑定 |
| `VkDescriptorBufferInfo` | 指向 Buffer、偏移与范围 |
| `VkDescriptorImageInfo` | 指向视图、Sampler 及使用时的图像布局 |
| `VkWriteDescriptorSet` | 指定被更新的 set、binding 与资源信息 |
| `VkPipelineLayout` | 组合各 set layout 和 Push Constant 范围 |

同一个绑定需要在 GLSL、layout 声明和更新操作中对应：

```text
layout(set = 0, binding = N) ...
VkDescriptorSetLayoutBinding.binding = N
VkWriteDescriptorSet.dstBinding = N
```

流程为创建 layout 和 pool、分配 set、更新资源，再在命令中绑定兼容的 set。`maxSets` 限制集数量，`VkDescriptorPoolSize` 限制各类型描述符总数，需要分别计数。

Descriptor Set 中的图像布局声明不会自动转换 Image；转换与同步由命令执行。每帧变化的数据还需有独立存储或安全复用机制，避免 CPU 覆写 GPU 正在读取的范围。

## Buffer 与内存

```text
创建 Buffer、声明 usage
  → 查询内存需求
  → 选择内存类型、分配并绑定
  → 主机写入或传输上传
  → 建立消费者所需的依赖
```

顶点和索引常通过主机可见的 staging Buffer 上传到设备本地内存；逐帧 Uniform 则可写入合适的主机可见内存。具体选择取决于设备架构与访问模式。

非 coherent 内存需要按要求 flush 或 invalidate，并满足范围对齐。`vkUnmapMemory` 不代替 flush；GPU 写入后的主机读取还要先等待执行完成。暂存资源也只能在传输不再使用它之后释放。

## Image、视图与采样

纹理上传通常包含像素读取、staging 写入、Image 分配、传输布局转换、Buffer 到 Image 的拷贝，以及面向采样的布局与访问依赖。

- **usage** 声明资源允许的用途。
- **layout** 表达图像子资源的访问布局。
- **Image View** 选择格式、aspect、mip 和数组层，供附件或 Shader 访问。
- **Sampler** 设置过滤、寻址与 LOD 等采样行为。

正确的 layout 不能代替执行与内存依赖。拷贝命令可以直接访问 Image；采样和附件使用则通过 Image View 解释资源。各向异性等采样能力还受设备 feature 与限制约束。

## 命令、队列与同步

命令池属于一个队列族，命令缓冲要提交到兼容队列族的队列。使用 Dynamic Rendering 时，典型的命令录制顺序如下。

```text
BeginCommandBuffer
  BeginRendering
    BindPipeline
    SetViewport / SetScissor
    BindVertexBuffers / BindIndexBuffer
    BindDescriptorSets
    DrawIndexed
  EndRendering
EndCommandBuffer
```

传统 Render Pass 路径用 `vkCmdBeginRenderPass` 和 `vkCmdEndRenderPass` 限定绘制范围。管线内的顶点处理、光栅化和片元处理由绘制命令触发。

录制、提交和 GPU 执行是不同的阶段。CPU 返回或线程结束不能证明 GPU 完成；命令引用的资源需保持有效，直到相关执行结束。

| 机制 | 用途 |
| --- | --- |
| Fence | 供主机等待提交完成、判断资源可复用 |
| Semaphore | 在设备操作间传递等待关系，例如 acquire、绘制和 present |
| Pipeline Barrier | 建立执行和内存依赖，可伴随布局或队列族所有权转换 |
| Subpass Dependency | 描述 Render Pass 内外及 Subpass 间的依赖 |

同步需明确生产者阶段、写入类型、消费者阶段和读取类型。跨队列族且使用独占共享模式的资源，还可能需要所有权转移。参见 [Vulkan Guide：Synchronization](https://docs.vulkan.org/guide/latest/synchronization.html)。

## 帧资源与交换链

典型帧流程为等待当前 frame slot、acquire 图像、更新可复用资源、录制并提交绘制、present，再推进 frame slot。应先处理 acquire 的失败或过期结果，再进行依赖后续提交才能完成的同步对象重置。

`currentFrame` 是帧资源索引，`imageIndex` 是 acquire 返回的交换链图像索引。并行帧数与交换链图像数独立；Uniform、命令缓冲、Fence 和呈现相关 Semaphore 应按各自的复用条件组织。

重建交换链时，更新受图像、格式和尺寸影响的视图、深度附件、Framebuffer、离屏资源、描述符、管线状态和命令。更新范围由实际依赖决定。

## 计算管线与工作组

计算管线使用描述符、Pipeline Layout 和计算 Shader，由 `vkCmdDispatch` 指定工作组数。例如：

```glsl
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
```

256 表示每组 invocation 数。处理 N 个元素时，工作组数量通常向上取整，并在 Shader 中处理越界；当 N 可整除本地组大小时，可以直接按整组分派。

计算输出交给顶点输入、间接命令、图像采样或主机回读时，要针对具体消费者建立依赖。对应数据路径见[计算与间接绘制](rendering-techniques.md#计算与间接绘制)。
