# Vulkan 基础

[文档](README.md) · [专题实现](rendering-techniques.md) · [示例目录](samples.md)

阅读 Vulkan 程序时，可以围绕三个问题展开：对象依赖谁、数据如何流动、什么时候可以复用或销毁资源。本文以窗口渲染和传统 Render Pass 路径说明公共概念；具体组合方式见专题实现。

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

## 坐标空间与深度

```text
模型 → 世界 → 观察 → 裁剪坐标 (xc, yc, zc, wc)
     → 透视除法 → NDC → 视口变换 → 帧缓冲
```

MVP 矩阵产生裁剪坐标，除以 `w` 得到 NDC。下表采用默认深度裁剪、正宽高 viewport，以及 OpenGL 默认的窗口原点：

| 轴或属性 | Vulkan | OpenGL 默认设置 |
| --- | --- | --- |
| NDC x、y 范围 | `[-1, 1]` | `[-1, 1]` |
| NDC z 范围 | `[0, 1]` | `[-1, 1]` |
| +x 在窗口中的方向 | 向右 | 向右 |
| +y 在窗口中的方向 | 向下 | 向上 |
| 窗口坐标原点 | 左上 | 左下 |
| 模型、世界与观察空间的手性 | 由应用的坐标与矩阵约定决定 | 由应用的坐标与矩阵约定决定 |

NDC 范围、窗口方向和世界空间的左右手约定需要分别理解。通常的前向深度投影将近处映射到较小的深度值；反向深度等设置会改变这一关系。NDC 的 z 范围也不等同于深度附件的存储格式或视口深度范围。参见 [Vulkan Guide：Depth](https://docs.vulkan.org/guide/latest/depth.html)。

本仓库多处使用 `GLM_FORCE_DEPTH_ZERO_TO_ONE` 生成对应深度范围，并通过 `proj[1][1] *= -1` 调整 y 方向。另一种方式是在支持负 viewport 高度的环境中调整 `height` 与 `y`；方向变化时也要核对正面绕序和剔除状态。参见 [VkViewport](https://docs.vulkan.org/refpages/latest/refpages/source/VkViewport.html)。

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

## 图形管线与附件

图形管线组合 Shader、顶点输入、图元拓扑、视口、光栅化、采样、深度模板、混合和动态状态。Shader 输入布局、CPU 数据与管线声明需一致。

传统渲染路径中：

| 对象 | 描述内容 |
| --- | --- |
| Render Pass | 附件格式、采样数、load / store、Subpass、布局与依赖 |
| Framebuffer | 实际使用的 Image View 及尺寸 |
| Graphics Pipeline | Shader 与状态，以及兼容的 Render Pass / Subpass |
| Command Buffer | 实际绑定、状态设置和绘制顺序 |

深度测试除了管线开关，还需要深度图像、内存、视图、附件和清除配置。多附件输出也需要片段输出位置、Subpass 引用和 Framebuffer 视图顺序对应。

Dynamic Rendering 在开始渲染时直接提供附件；它改变对象组织方式，仍需处理资源布局与同步。

## 命令、队列与同步

命令池属于一个队列族，命令缓冲要提交到兼容队列族的队列。常见绘制命令为：

```text
BeginCommandBuffer
  BeginRenderPass
    BindPipeline
    SetViewport / SetScissor
    BindVertexBuffers / BindIndexBuffer
    BindDescriptorSets
    DrawIndexed
  EndRenderPass
EndCommandBuffer
```

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
