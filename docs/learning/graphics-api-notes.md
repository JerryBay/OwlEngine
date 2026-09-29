# 底层图形 API 知识笔记

记录讨论中值得沉淀的图形 API 概念：它解决什么问题、关键机制是什么、哪些边界容易混淆。
以 Vulkan 为起点，后续在实际讨论到 D3D12 或移动端差异时补充对照。

## 记录范围

- 按知识主题合并更新，保留可迁移的原理与必要示例，不逐条收录提问或对话。
- 不收录 C++ 通用写法、IDE/CMake 配置、项目文件位置、模块命名、开发进度等工程细节。
- 不建立个人“理解画像”，不推断掌握程度。需要深化的具体机制归到对应知识点下。
- 项目实现可作为讲解例子，但不能把某个示例的选择写成 Vulkan 的普遍要求。
- 后续解释相关概念前参考本笔记；只有产生值得保留的新原理、澄清或 API 差异时才更新。

## 1. API、Loader 与驱动

Vulkan API 定义应用与实现之间的调用契约。Loader 负责发现实现并分发调用，驱动负责面向具体 GPU
实现这些操作。启用的 Layer 可以参与调用链，例如进行有效性检查。

SDK 提供开发和调试工具；安装完整 SDK 与运行 Vulkan 程序是两件事。运行仍需要可用的 Vulkan 实现，
应用依赖的 Loader、Layer 等组件也必须满足运行条件。API 名称或版本声明不等于 GPU 支持所有特性。

## 2. Vulkan 对象的职责与依赖

| 对象 | 解决的问题 |
| --- | --- |
| `VkInstance` | 建立应用的 Vulkan 上下文，枚举物理设备，承载实例级能力 |
| `VkPhysicalDevice` | 表示物理设备及其特性、限制、内存类型和队列族能力 |
| `VkDevice` | 建立逻辑设备，启用所需设备特性，创建和管理设备级资源 |
| `VkQueue` | 接收提交的工作；不同队列族提供不同能力，呈现支持还与 Surface 有关 |
| `VkSurfaceKHR` | 表示与平台呈现目标的连接，不负责创建 Device 或执行绘制 |
| `VkSwapchainKHR` | 提供一组可获取、渲染和呈现的图像，并参与呈现系统的协调 |

创建一个对象需要另一个对象，不代表前者拥有后者。生命周期要同时考虑 API 的对象依赖和在途 GPU 使用；
CPU 不再访问某个句柄，不意味着可以立即销毁 GPU 仍在使用的资源。

## 3. 资源对象与内存分配

`VkBuffer` 描述大小和用途，`VkDeviceMemory` 提供底层存储，绑定操作把两者连接起来。
创建 Buffer 本身不等于已准备好可用的存储；分配时还要满足资源的大小、对齐和兼容内存类型要求。
这种分离允许应用管理分配策略，在满足绑定约束时让多个资源使用同一大块分配的不同区域。

`HOST_VISIBLE` 表示主机可映射访问；`HOST_COHERENT` 影响主机与设备之间是否需要显式 Flush/Invalidate。
**Coherent 不等于同步完成**，也不允许 CPU 随意覆盖 GPU 正在读取的数据。
`DEVICE_LOCAL` 与 `HOST_VISIBLE` 并不互斥，不能简单把它们理解为“显存”和“系统内存”二选一。

Staging 上传把 CPU 写入位置与 GPU 长期使用位置分开：CPU 写可映射的临时 Buffer，GPU 通过 Copy
把数据移入目标 Buffer。需要分别解决三个问题：非 coherent 主机写入要 Flush；Copy 写入到后续
顶点／索引读取要建立内存依赖；CPU 释放临时 Buffer 和命令资源前要确认 GPU 已完成使用。
Fence 等待不能代替 Flush 或 GPU 内存依赖，Barrier 也不能代替 CPU 对完成状态的确认。

Readback 是反向路径：GPU Copy 到可映射的目标，建立 transfer-write 到 host-read 的依赖，
等待完成后，对非 coherent 内存 Invalidate，再由 CPU 读取。Flush/Invalidate 的范围必须满足
`nonCoherentAtomSize` 规则；对单独分配且无并发访问的内存，映射整个分配并使用 offset 0 /
`VK_WHOLE_SIZE` 是一种起步策略。多个子分配共享底层内存时，不能直接照搬整块内存的操作范围。
资源的逻辑字节数与原生分配大小也要区分：内存需求有对齐约束，不代表每次 Buffer Copy 都要额外补齐。

VMA 将多个资源的 allocation 放入较大的 `VkDeviceMemory` block，按资源要求处理内存类型、
对齐和分配；某些资源也可能单独分配。**allocation 的大小与 block 的保留大小不同**：释放资源后，
分配器仍可保留空闲 block 供复用，不能仅凭保留内存未归零就判定资源泄漏。

使用 VMA 映射时，返回的指针已指向 allocation 起点；Flush/Invalidate 的偏移也相对此 allocation。
不应再加一次它在 block 内的 offset。VMA 会处理底层范围与 atom 对齐，但不会替应用补上 GPU Barrier、
等待 Fence 或判断何时销毁资源。`AUTO` 分配需要明确 CPU 访问意图：顺序写适合上传，随机访问适合回读；
实际 memory type 碰巧带有 `HOST_VISIBLE` 不等于分配时已声明可映射。
参见 [VMA：映射与缓存管理](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html)。

Staging 不是所有硬件上的唯一选择。统一内存架构或 host-visible/device-local 内存可能允许直接写入；
是否值得增加一次 Copy，需要结合 CPU 访问方式、GPU 重用频率和带宽衡量。无论选哪条路径，都不能覆盖
GPU 仍在读取的范围；启动时阻塞上传与运行时多帧上传也需要不同的回收策略。

继续深入：部分非 coherent 范围的对齐、批量上传与完成后回收，以及独立显卡和统一内存的性能取舍。
参见 [Khronos：内存分配](https://docs.vulkan.org/guide/latest/memory_allocation.html)。

### Image、ImageView 与 Sampler

Image 描述图像的格式、尺寸、mip 层数和用途，绑定的内存提供存储。ImageView 指定对哪些图像
子资源进行何种解释，例如某一段 mip 层；它引用 Image，不复制像素。Sampler 保存过滤、寻址和
LOD 规则，可以用于多个兼容图像，不拥有 Image 或 ImageView。资源组合交给 Shader 的绑定是另一步。

创建多层 mip 的 Image 只准备了存储；像素上传、mip 生成和采样前的布局转换仍需完成。
`initialLayout = UNDEFINED` 不能理解为像素已经清零，Sampler 创建成功也不能证明目标格式支持线性过滤。
普通 optimal-tiling Image 的实际存储大小和布局由实现决定，不能按 `width × height × 像素字节数`
直接计算其内存绑定要求或把它当作连续 CPU 像素数组。

让 View 在 Image 之前销毁、Image 在分配器和 Device 之前销毁，可以建立清楚的所有权边界；
这些 CPU 对象的销毁顺序仍需要以 GPU 已完成相关使用为前提。移动 C++ owner 可以保留原生句柄，
但覆盖一个已有资源的 owner 会释放旧资源，必须满足旧资源的生命周期要求。
参见 [Image 创建规则](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageCreateInfo.html)、
[ImageView](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html) 和
[Sampler](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html)。

## 4. Shader、Pipeline 与 Pipeline Layout

Shader 描述可编程阶段的计算；Graphics Pipeline 把 Shader 与顶点输入、图元装配、光栅化、颜色输出等状态组合起来。
普通图形管线创建完成后，可销毁作为创建输入的 Shader Module；GPU 后续绘制使用的是创建好的 Pipeline。

Pipeline Layout 描述 Descriptor Set 和 Push Constant 的接口布局，不是顶点布局，也不是实际资源的容器。
动态状态允许部分值在命令录制时指定；例如动态 Viewport/Scissor 可以减少窗口尺寸变化导致的管线重建。
Dynamic Rendering 省去传统 Render Pass/Framebuffer 对象的创建需求，但没有消除附件格式兼容性和同步要求。

继续深入：哪些状态适合固定在 Pipeline 中，哪些适合动态设置；Shader 资源接口如何与实际绑定的资源衔接。

## 5. 命令录制、提交与异步执行

调用 `vkCmdDrawIndexed` 是把绘制命令录入 CommandBuffer。通过 Queue Submit 提交后，GPU 才能执行这些工作；
提交函数返回不代表 GPU 已执行完毕。CommandBuffer 也不会自动复制它引用的全部资源数据。

资源准备、命令录制、GPU 执行可以处于不同进度。资源复用和销毁需要相应的完成条件，
不能用“CPU 已经走到下一帧”作为依据；多帧并行正是需要明确管理这些重叠生命周期的原因之一。

## 6. 同步、图像布局与呈现

同步要区分**执行依赖**和**内存依赖**：规定操作先后，与让先前写入对后续访问可见，并不是同一件事。
Barrier 的阶段、访问范围和图像布局需要围绕真实的资源使用来设置。

- Fence 可用于让 CPU 等待或查询与其关联的 GPU 提交完成。
- Semaphore 用于建立提交之间、或获取／渲染／呈现之间的依赖；Timeline Semaphore 还支持主机操作。
- Image Layout 描述图像在特定用途下的布局要求；布局转换需要与相关访问正确同步。
- Acquire、Render、Present 是不同环节。渲染提交完成，不等于呈现系统已释放相关资源，更不等于屏幕已显示这一帧。

`oldLayout = UNDEFINED` 允许丢弃旧像素内容，但不会取消与先前访问之间的同步要求；布局转换本身也需要排序。
例如，Acquire Semaphore 等待在 `COLOR_ATTACHMENT_OUTPUT` 时，后续布局转换 Barrier 的源阶段也可设为
`COLOR_ATTACHMENT_OUTPUT`，把“获取完成 → 布局转换 → 颜色附件写入”连成执行依赖链。
只设置 Barrier 的目标阶段并不能保证转换发生在 Semaphore 等待之后；源阶段 `NONE` 也不等于“自动等完前面的工作”。
这里等待的是先前访问结束，源访问掩码可以保持 `NONE`。参见
[Khronos：交换链获取与呈现同步示例](https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples#swapchain-image-acquire-and-present)。

窗口变化时，应根据哪些对象依赖尺寸、格式和呈现能力决定重建范围，而不是销毁整套渲染资源。
仅尺寸变化、颜色格式变化、暂时没有可绘制区域，需要分别处理。

继续深入：Fence、Semaphore、Barrier 各自覆盖的范围，以及渲染完成和呈现资源可复用之间的区别。
参见 [Khronos：同步](https://docs.vulkan.org/guide/latest/synchronization.html)。

## 7. GPU 捕获是怎样帮助验证的

运行日志只能说明应用走到了某个调用点；GPU 捕获把一帧中的 API 事件、资源、Pipeline 状态和输出图像保存下来，
可以检查实际录入的 Draw、绑定的 Shader、Render Target、Viewport 和资源读写关系。

一次 RenderDoc 调试应分成两个阶段：先由 RenderDoc 注入目标程序并生成 `.rdc`，再打开捕获分析事件。
因此“捕获失败”和“捕获后发现绘制错误”是两类不同问题：前者尚未获得 GPU 帧证据，不能据此判断 Vulkan 绘制逻辑错误。

典型的单 Draw 检查顺序是：

```text
capture → open_capture → list_events / list_draws
        → goto_event → get_pipeline_state / get_bindings
        → get_shader / export_render_target
```

`vkCmdDrawIndexed` 出现在事件列表中，只能证明捕获到了绘制命令；还要确认它有颜色附件、正确的 Shader 和有效的索引范围。
导出的 Render Target 或像素历史，才是对该 Draw 输出的独立检查。Validation Layer 日志则用于检查 API 使用是否合法，
它与画面是否正确是互补证据。
