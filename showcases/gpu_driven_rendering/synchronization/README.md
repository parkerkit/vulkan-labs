# Synchronization

基于 `compute_draw_indirect`，通过两个 Compute dispatch 和一次图形阶段展示同一图形队列内的四种同步依赖。
CMake target：`gpu_synchronization`。

## 每帧数据流程

1. CPU 提供动画时间、位移幅度和绘制比例，通过 Push Constants 交给 Compute A。
2. `synchronization_state.comp` 为每个物体生成 `ObjectState`：位移、绘制开关和颜色。
3. Compute → Compute 屏障发布状态，`synchronization_commands.comp` 读取绘制开关并生成完整间接命令。
4. 图形阶段前，分别为间接参数和状态缓冲建立内存依赖。
5. CPU 逐物体绑定原有几何，用 Push Constant 提供对象 ID，再执行 `vkCmdDrawIndexedIndirect`。
6. 顶点着色器从状态 SSBO 读取位移；片元着色器从同一 SSBO 读取颜色并调制纹理。

`instanceCount` 为 0 的命令不生成实例；命令数组及 CPU 绘制调用仍保持 1000 条。
绘制比例只是确定性开关，不进行视锥测试或可见列表压缩。

## 缓冲与布局

| 资源 | 内容 | 写入方 | 读取方 |
| --- | --- | --- | --- |
| 索引数量 | 每个网格的 `uint index_count` | CPU 初始化 | Compute B |
| ObjectState | 两个 `vec4`，步长 32 字节 | Compute A | Compute B、Vertex、Fragment |
| 间接命令 | `VkDrawIndexedIndirectCommand`，步长 20 字节 | Compute B | Draw Indirect |
| CameraUniform | MVP 矩阵，64 字节 | CPU 更新当前帧 | Vertex |

`ObjectState.offset_and_visible.xyz` 保存位移，`.w` 保存绘制开关；`color` 保存 RGBA。
状态缓冲使用 `STORAGE_BUFFER`；间接命令缓冲同时使用 `STORAGE_BUFFER | INDIRECT_BUFFER`。
图形管线的 set 0 使用相机和纹理，set 1 复用 Compute 描述符集合读取状态缓冲。

## 显式屏障

以下阶段名省略 `VK_PIPELINE_STAGE_2_` 前缀，访问名省略 `VK_ACCESS_2_` 前缀；均使用对应的 `_BIT`。

| 依赖 | 缓冲 | 源阶段 / 访问 | 目标阶段 / 访问 |
| --- | --- | --- | --- |
| Compute A → Compute B | ObjectState | COMPUTE_SHADER / SHADER_STORAGE_WRITE | COMPUTE_SHADER / SHADER_STORAGE_READ |
| Compute B → Indirect | 间接命令 | COMPUTE_SHADER / SHADER_STORAGE_WRITE | DRAW_INDIRECT / INDIRECT_COMMAND_READ |
| Compute A → Vertex | ObjectState | COMPUTE_SHADER / SHADER_STORAGE_WRITE | VERTEX_SHADER / SHADER_STORAGE_READ |
| Compute A → Fragment | ObjectState | COMPUTE_SHADER / SHADER_STORAGE_WRITE | FRAGMENT_SHADER / SHADER_STORAGE_READ |

第一道屏障位于两个 dispatch 之间。第二组包含两条缓冲屏障：
一条覆盖间接命令，另一条合并 Vertex 和 Fragment 对 ObjectState 的读取。
第二组屏障仍能覆盖此前 Compute A 的写入；Compute B 只读取状态，不会重写它。
所有屏障都在动态渲染作用域之外录制。

顶点着色器通过 SSBO 读取状态，目标阶段是 `VERTEX_SHADER`；
这里没有把状态作为顶点属性绑定，因此不使用 `VERTEX_ATTRIBUTE_INPUT`。
同一队列中的提交顺序及 timestamp 计时标记不能替代这些资源的内存可见性依赖。

## 帧槽位与 CPU/GPU 访问

ObjectState、间接命令、CameraUniform 和描述符集合按飞行帧分配。
框架在 `OnUpdate` 前等待当前槽位的 fence，因此覆盖这个槽位时，其上一次读取已完成。
同一帧内 Compute 与绘制之间的依赖由上述 GPU 屏障建立。
初始化的索引数量缓冲写入后不再修改；`Buffer::Write()` 刷新主机缓存，后续队列提交发布此前的主机写入。

这个示例使用单个支持图形和计算的队列，屏障的队列族索引均为 `VK_QUEUE_FAMILY_IGNORED`。
跨队列 semaphore、队列族所有权转移和异步计算可在后续示例中单独学习。

## 观察方式

- `Animate GPU state`：冻结或推进动画时间，两次 dispatch 仍每帧执行。
- `Displacement`：调整顶点着色器读取的 GPU 位移幅度，最大为 0.75。
- `Active fraction`：调整 Compute B 生成的有效命令比例；0 隐藏全部物体，1 绘制全部物体。
- 颜色随 GPU 动画变化，片元着色器直接读取本帧生成的颜色。
- `Auto rotate` 或空格：控制场景整体旋转，与 GPU 状态动画独立。
- Home / `Reset camera`：重新取景；R / `Reset all`：恢复全部控制参数。

相机包围范围预留最大位移，避免自动近远裁剪误切动画。
复用 `fw::GpuTimer` 显示 State、Commands、Draw 三段近似 GPU 耗时，包含对应边界处的同步成本，
结果来自已完成的帧槽位，不包含 CPU 录制和 ImGui 绘制；计时器不额外等待 GPU。

屏障始终启用。正确性可结合 Vulkan Synchronization Validation 检查；画面稳定本身不能证明没有同步问题。

参考：[Vulkan Guide — Synchronization Examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)。
