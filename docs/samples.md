# 示例目录

[文档](README.md) · [效果展示](gallery.md) · [构建与运行](getting-started.md)

`samples/` 包含 9 个主题分组、73 个独立可执行程序。表中名称就是构建使用的 target，点击名称进入对应源码。

```sh
cmake --build --preset <preset> --target <target>
```

先按[构建与运行](getting-started.md#选择配置)配置当前平台的预设，再将 `<preset>` 和 `<target>` 替换为对应名称。

同一示例的不同实现使用独立入口和 target，可分别构建运行，命名规则见[多个入口与目标](#多个入口与目标)。

## Vulkan Tutorial

窗口渲染、资源管理与计算的基础路径。阅读时可以依次跟踪初始化、上传、描述符、管线和帧循环。

| Target / 源码 | 主题 |
| --- | --- |
| [`01_01_hello_triangle`](../samples/01_vulkan_tutorial/01_hello_triangle/main.cpp) | 实例、设备、交换链与三角形；[画面](gallery.md#第一个三角形) |
| [`01_02_vertex_buffers`](../samples/01_vulkan_tutorial/02_vertex_buffers/main.cpp) | 顶点、索引、staging 上传与索引绘制 |
| [`01_03_uniform_buffers`](../samples/01_vulkan_tutorial/03_uniform_buffers/main.cpp) | Descriptor 的基础使用 |
| [`01_03_uniform_buffers2`](../samples/01_vulkan_tutorial/03_uniform_buffers/main2.cpp) | 多个 DescriptorSet 的使用，对应着色器中的 layout(set = XX, binding = 0) |
| [`01_04_texture_mapping`](../samples/01_vulkan_tutorial/04_texture_mapping/main.cpp) | 纹理的基础使用 |
| [`01_04_texture_mapping2`](../samples/01_vulkan_tutorial/04_texture_mapping/main2.cpp) | ImGui 动态修改纹理的图片 |
| [`01_05_depth_buffering`](../samples/01_vulkan_tutorial/05_depth_buffering/main.cpp) | 深度附件、比较与写入 |
| [`01_06_loading_models`](../samples/01_vulkan_tutorial/06_loading_models/main.cpp) | OBJ 模型加载与纹理 |
| [`01_06_loading_models2`](../samples/01_vulkan_tutorial/06_loading_models/main2.cpp) | glTF 模型查看器、模型级颜色/PBR 控制、蒙皮与 Morph 动画；[画面对比与操作](gallery.md#gltf-模型查看器) |
| [`01_07_generating_mipmaps`](../samples/01_vulkan_tutorial/07_generating_mipmaps/main.cpp) | Mipmap、逐级 Blit 与布局转换 |
| [`01_08_multi_sampling`](../samples/01_vulkan_tutorial/08_multi_sampling/main.cpp) | 多重采样附件与 Resolve |
| [`01_09_compute_shader`](../samples/01_vulkan_tutorial/09_compute_shader/main.cpp) | 计算更新粒子，图形阶段读取并绘制；[画面与视频](gallery.md#gpu-粒子) |
| [`01_10_build_command_buffers`](../samples/01_vulkan_tutorial/10_build_command_buffers/main.cpp) | 绘制命令预录制与复用 |

对象关系见 [Vulkan 基础](vulkan-fundamentals.md)，数据绑定见[资源与数据输入](rendering-techniques.md#资源与数据输入)。

## Advanced

组合资源绑定、渲染状态、附件与命令组织。

| Target / 源码 | 主题 |
| --- | --- |
| [`02_01_triangles`](../samples/02_advance/01_triangles/main.cpp) | 多组几何资源与多次绘制 |
| [`02_02_dynamic_uniform_buffers`](../samples/02_advance/02_dynamic_uniform_buffers/main.cpp) | 动态 Uniform 与逐对象偏移 |
| [`02_03_pipelines`](../samples/02_advance/03_pipelines/main.cpp) | 多管线、拓扑、填充模式与视口 |
| [`02_04_pushconstants`](../samples/02_advance/04_pushconstants/main.cpp) | PushConstant 基础使用 |
| [`02_04_pushconstants2`](../samples/02_advance/04_pushconstants/main2.cpp) | 同一管线针对不同绘制调用多次提交 Push Constant |
| [`02_04_pushconstants3`](../samples/02_advance/04_pushconstants/main3.cpp) | 顶点着色器片段着色器共用一个 PushConstant |
| [`02_04_pushconstants4`](../samples/02_advance/04_pushconstants/main4.cpp) | 顶点着色器和片段着色器都使用一个 PushConstant |
| [`02_05_specialization_constants`](../samples/02_advance/05_specialization_constants/main.cpp) | 管线创建时设置 Shader 特化常量 |
| [`02_06_input_attachments`](../samples/02_advance/06_input_attachments/main.cpp) | Subpass 读取颜色和深度附件 |
| [`02_07_imgui`](../samples/02_advance/07_imgui/main.cpp) | Dear ImGui 的 GLFW / Vulkan backend |
| [`02_08_deferred_shading`](../samples/02_advance/08_deferred_shading/main.cpp) | G-buffer、输入附件与多光源；[画面对比](gallery.md#延迟渲染) |
| [`02_09_offscreen`](../samples/02_advance/09_offscreen/main.cpp) | 离屏渲染的图片大小和窗口大小始终保持一致 |
| [`02_09_offscreen2`](../samples/02_advance/09_offscreen/main2.cpp) | 离屏渲染到 VkImage，再作为纹理渲染到屏幕 |
| [`02_10_post_process`](../samples/02_advance/10_post_process/main.cpp) | 滤镜、卷积与 FXAA；[画面对比](gallery.md#后处理) |
| [`02_11_vertex_attributes`](../samples/02_advance/11_vertex_attributes/main.cpp) | 交错和分离的顶点属性布局 |
| [`02_12_blit`](../samples/02_advance/12_blit/main.cpp) | 交换链图像传输、回读与 JPG 导出 |
| [`02_13_stencil`](../samples/02_advance/13_stencil/main.cpp) | 模板标记、比较与矩形边框 |
| [`02_14_cpuparticle`](../samples/02_advance/14_cpuparticle/main.cpp) | CPU 更新粒子，点图元绘制 |
| [`02_15_blend`](../samples/02_advance/15_blend/main.cpp) | 颜色混合因子与逻辑操作 |
| [`02_16_transform`](../samples/02_advance/16_transform/main.cpp) | 变换 |
| [`02_16_transform2`](../samples/02_advance/16_transform/main2.cpp) | 变换 + ImGui |
| [`02_16_transform3`](../samples/02_advance/16_transform/main3.cpp) | 变换 + ImGui + Drawable |
| [`02_16_transform4`](../samples/02_advance/16_transform/main4.cpp) | 变换 + ImGui + Drawable + Texture |
| [`02_16_transform5`](../samples/02_advance/16_transform/main5.cpp) | 变换 + ImGui + Drawable + Offscreen |
| [`02_17_multi_thread`](../samples/02_advance/17_multi_thread/main.cpp) | 每个线程并行生成辅助命令缓冲区 |
| [`02_17_multi_thread2`](../samples/02_advance/17_multi_thread/main2.cpp) | 子线程中执行 RecordCommand |
| [`02_17_multi_thread3`](../samples/02_advance/17_multi_thread/main3.cpp) | 子线程中执行 DrawFrame，再由其子线程执行 RecordCommand |
| [`02_17_multi_thread4`](../samples/02_advance/17_multi_thread/main4.cpp) | 工作线程录制、提交，通过 join() 顺序等待 |
| [`02_18_instancing`](../samples/02_advance/18_instancing/main.cpp) | 简单使用 gl_InstanceIndex |
| [`02_18_instancing2`](../samples/02_advance/18_instancing/main2.cpp) | 给每个实例设置属性数据 |
| [`02_19_indirectdraw`](../samples/02_advance/19_indirectdraw/main.cpp) | 间接索引绘制参数与多次间接绘制 |
| [`02_20_query_pool`](../samples/02_advance/20_query_pool/main.cpp) | 遮挡查询 |
| [`02_20_query_pool2`](../samples/02_advance/20_query_pool/main2.cpp) | 管线统计：从不同管线阶段收集信息 |
| [`02_21_render_pass`](../samples/02_advance/21_render_pass/main.cpp) | 两个完全一样的 RenderPass，只会显示第二次的 RenderPass 绘制结果 |
| [`02_21_render_pass2`](../samples/02_advance/21_render_pass/main2.cpp) | 清除深度缓冲区，指定图元始终在最上层 |
| [`02_22_pipeline_cache`](../samples/02_advance/22_pipeline_cache/main.cpp) | 管线缓存创建、导出与持久化 |
| [`02_23_texture_cube_map`](../samples/02_advance/23_texture_cube_map/main.cpp) | 六面上传、Cube View 与方向采样；[画面](gallery.md#cubemap) |

技术流程见[专题实现](rendering-techniques.md)。

## Compute Shader

| Target / 源码 | 主题 |
| --- | --- |
| [`03_01_image_processing`](../samples/03_compute_shader/01_image_processing/main.cpp) | Storage Image 读写，将蓝色通道置零并显示处理前后结果 |
| [`03_02_indirect_draw`](../samples/03_compute_shader/02_indirect_draw/main.cpp) | 计算 Shader 写入间接绘制参数，界面控制实例数 |

两条计算数据路径见[计算与间接绘制](rendering-techniques.md#计算与间接绘制)。

## Headless

| Target / 源码 | 主题 |
| --- | --- |
| [`04_01_render`](../samples/04_headless/01_render/main.cpp) | 无窗口离屏绘制，回读并保存不同视角的 JPG |
| [`04_02_compute`](../samples/04_headless/02_compute/main.cpp) | 无窗口数值计算、回读与终端输出 |

对象管理方式见 [Headless 与 Vulkan-Hpp](rendering-techniques.md#headless-与-vulkan-hpp)。

## Geometry Shader

| Target / 源码 | 主题 |
| --- | --- |
| [`05_01_geometry_shader`](../samples/05_geometry_shader/01_geometry_shader/main.cpp) | Geometry Shader 将点扩展为三角形 |
| [`05_02_viewport_arrays`](../samples/05_geometry_shader/02_viewport_arrays/main.cpp) | 通过 `gl_ViewportIndex` 向两个 viewport 输出 |

设备要求和输出机制见[设备特性与扩展](rendering-techniques.md#设备特性与扩展)。

## Extensions

| Target / 源码 | 主题 |
| --- | --- |
| [`06_01_conditional_render`](../samples/06_extensions/01_conditional_render/main.cpp) | 条件 Buffer 与界面开关控制绘制 |
| [`06_02_multi_view`](../samples/06_extensions/02_multi_view/main.cpp) | 视图掩码、`gl_ViewIndex` 与数组图像各层 |
| [`06_03_dynamic_rendering`](../samples/06_extensions/03_dynamic_rendering/main.cpp) | 使用 KHR 扩展命令进行动态渲染 |
| [`06_03_dynamic_rendering2`](../samples/06_extensions/03_dynamic_rendering/main2.cpp) | 使用 Vulkan 1.3 核心命令 vkCmdBeginRendering |
| [`06_04_mesh_shader`](../samples/06_extensions/04_mesh_shader/main.cpp) | CPU 划分 meshlet，Mesh Shader 绘制苹果模型并按 meshlet 着色；[画面与操作](gallery.md#mesh-shader) |

接口与 feature 的关系见[设备特性与扩展](rendering-techniques.md#设备特性与扩展)。

## Vulkan-Hpp

以 C++ API 和 RAII 组织对象，前六项使用离屏输出，后两项引入窗口呈现。

| Target / 源码 | 主题 |
| --- | --- |
| [`07_01_hello_triangle`](../samples/07_vulkan_hpp/01_hello_triangle/main.cpp) | 单帧三角形与 JPG 输出 |
| [`07_02_multi_frame`](../samples/07_vulkan_hpp/02_multi_frame/main.cpp) | 多帧资源、提交与离屏输出 |
| [`07_03_buffer`](../samples/07_vulkan_hpp/03_buffer/main.cpp) | 顶点和索引缓冲及上传 |
| [`07_04_uniform`](../samples/07_vulkan_hpp/04_uniform/main.cpp) | Uniform 与 MVP 变换 |
| [`07_05_texture`](../samples/07_vulkan_hpp/05_texture/main.cpp) | 纹理上传与采样 |
| [`07_06_depth_buffer`](../samples/07_vulkan_hpp/06_depth_buffer/main.cpp) | 深度附件与遮挡 |
| [`07_07_swap_chain`](../samples/07_vulkan_hpp/07_swap_chain/main.cpp) | 窗口、Surface、交换链与呈现循环 |
| [`07_08_offscreen`](../samples/07_vulkan_hpp/08_offscreen/main.cpp) | 离屏渲染结果采样到窗口 |

资源生命周期见 [Headless 与 Vulkan-Hpp](rendering-techniques.md#headless-与-vulkan-hpp)。

## Rendering

| Target / 源码 | 主题 |
| --- | --- |
| [`08_01_shadow_mapping`](../samples/08_showcases/01_shadow_mapping/main.cpp) | 深度贴图、遮挡比较与 PCF；[画面对比](gallery.md#阴影映射) |
| [`08_02_hdr`](../samples/08_showcases/02_hdr/main.cpp) | 浮点离屏附件、曝光与 Reinhard；[画面对比](gallery.md#hdr-与色调映射) |

算法与数据流见[阴影与 HDR](rendering-techniques.md#阴影与-hdr)。

## Memory Allocation

通过 Vulkan Memory Allocator（VMA）管理 Buffer / Image 的内存选择、分配与绑定，绘制与队列同步由示例自身组织。

| Target / 源码 | 主题 |
| --- | --- |
| [`09_01_vma_vertex_buffers`](../samples/09_memory_allocation/01_vma_vertex_buffers/main.cpp) | VMA 顶点与索引缓冲，staging 上传后绘制彩色四边形 |
| [`09_02_vma_loading_models`](../samples/09_memory_allocation/02_vma_loading_models/main.cpp) | OBJ 与纹理上传、VMA 纹理及深度图像、持久映射 UBO，绘制旋转模型 |

资源分配、映射和缓存可见性见[VMA 内存分配](rendering-techniques.md#vma-内存分配)。

## 多个入口与目标

同一示例的不同实现分别保存在 `main.cpp`、`main2.cpp`、`main3.cpp` 等文件中。每个入口顶部的 `//` 注释说明该实现的作用；各版本通过独立 target 构建运行。

`main.cpp` 使用原 target 名，`mainN.cpp` 在原 target 名后直接追加数字 `N`。例如：

| 源码入口 | Target |
| --- | --- |
| `02_advance/04_pushconstants/main.cpp` | `02_04_pushconstants` |
| `02_advance/04_pushconstants/main2.cpp` | `02_04_pushconstants2` |
| `02_advance/04_pushconstants/main3.cpp` | `02_04_pushconstants3` |
| `02_advance/04_pushconstants/main4.cpp` | `02_04_pushconstants4` |

同目录各 target 共用 Shader 构建依赖。设备选择条件、资源文件与各版本的具体差异以对应源码为准。
