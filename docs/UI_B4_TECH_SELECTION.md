# EBC 5.3 B4.1「UI 域」技术选型

> 调研方式：只读源码 + 静态检索，未改引擎源码、未编译、未运行构建。
> 性能数字均为**推断**（依据绘制路径与缓存机制，不做基准测试）。
> 证据格式统一为 `文件:行号`，完整索引见第 10 节。

---

## 0. 结论（先行）

**推荐方案：方案 3 的子形态 3B —— 在 `KX_KetsjiEngine::EndFrame()` 的"合成后"位置插入一个独立的屏幕空间 2D UI pass，绘制核心用 BLF（文本）+ GPU batch（矩形/纹理），不把 UI 对象化为场景物体，也不改 EEVEE/Vulkan 后端。**

**一句话理由：三条路里只有它同时具备中文文本、任意纹理、屏幕空间布局与命中判定，并且完全走 GPU 抽象层，在 Vulkan（本 build 默认后端）与 OpenGL 下都是同一套代码。**

三个必须先知道的硬事实：

1. **`bge.render` 在本分支只剩 `drawLine`**，没有 `drawBox` / `drawText` / `drawCircle` / 字体 / 纹理 / 对齐 API；而且它是 Python 模块，B4 节点是 C++ 原生运行时，不能直接复用这套接口。证据：`source/gameengine/Ketsji/KX_PythonInit.cpp:1195-1221`（方法表只有 `drawLine`）、`同文件:1853`（模块名 `bge.render`）。
2. **CJK 字体在 blenderplayer 里默认不可用**：字体栈（含内置 Noto Sans CJK）由 `BLF_load_font_stack()` 加载（`source/blender/blenfont/intern/blf_font_default.cc:95-100`），但 player 只调用 `BLF_load_default()` / `BLF_load_mono_default()`（`source/gameengine/GamePlayer/GPG_ghost.cpp:1045,1047`），**没有**调用字体栈加载。原生 UI 必须在初始化时显式调用一次 `BLF_load_font_stack()`。
3. **现有 `scene.post_draw` 是活的，但它跑在调试绘制 flush 里**：`RAS_OpenGLDebugDraw::Flush()` 会触发 `KX_Scene::POST_DRAW`（`source/gameengine/Rasterizer/RAS_OpenGLRasterizer/RAS_OpenGLDebugDraw.cpp:145-147`），而 `KX_KetsjiEngine::PostRenderScene()` 反而没有调用点（只有定义：`KX_KetsjiEngine.cpp:1062`，全仓检索无 caller）。因此不要按"PostRenderScene 是主渲染收尾"来设计 UI。

B4.1 推荐工作量：**13–16 人日**（单人，含 Mock/回归；推断，见 7.5）。

---

## 1. 评估范围与方法

- 目标：B4.1 增加 9 个 UI 节点 + `KX_UIAdapter`，先做选型，不写实现。
- 候选：
  1. `bge.render` 的 2D API；
  2. Grease Pencil / GPencil 数据块；
  3. 独立渲染层 / overlay pass（含 3A 现有 overlay collection、3B 新增 2D pass、3C 改后端）。
- 约束：沿用 B4 适配器 SOP（`docs/NATIVE_NODE_ADAPTER_SOP.md:1-208`）：接口有中性默认实现、Mock 记录、引擎实现走真实路径、节点端口表驱动。
- 判定维度：文本（中文/字体/字号/对齐）、图片/纹理、布局与锚点、鼠标命中、Vulkan/EEVEE 兼容、性能（重绘 vs 缓存）、代码复用、工作量、风险。

---

## 2. 方案对比总表

| 维度 | 方案 1：bge.render 2D | 方案 2：GPencil | 方案 3A：overlay collection | 方案 3B：EndFrame 2D pass（推荐） |
|---|---|---|---|---|
| 文本能力 | **无**（只有 3D 线段） | **无**（GP 数据是笔画/点，不含字形） | 有，但只有 `KX_FontObject`（3D 文本对象，改字重建几何） | **强**：BLF，支持字体/字号/颜色/旋转/阴影/按宽度断行，中文靠字体栈 |
| 图片/纹理 | **无** | 只能把纹理贴在笔画材质上，不是任意 UI 矩形 | 可以，需建图片平面 + 材质/对象 | **强**：`GPU_SHADER_2D_IMAGE_RECT_COLOR` + `BKE_image_acquire_gpu_texture` |
| 布局与锚点 | **无** | **无** | 3D 对象 transform，做 2D 布局很别扭 | **强**：CPU 矩形布局，相对位置/尺寸/9 宫锚点/裁剪 |
| 鼠标命中 | 只能自己算，但没有 UI 元素 | 只能自己算 | 屏幕矩形命中要自己做，对象边界是世界空间 | **可行**：`KX_InputAdapter::MousePosition` + 矩形命中，BGUI 已验证 |
| Vulkan/EEVEE 5.3 | 调试绘制走 GPU 抽象，能用；但 API 是 Python 层 | GP 引擎走 DRW，能用；但游戏侧没有 GP 数据 API | 走 EEVEE，能用，但每帧多一遍 EEVEE | **最好**：纯 GPU 抽象 + BLF，Vulkan 是默认后端 |
| 性能 | 每帧调试列表，无缓存 | GP batch 有缓存，但每次改字/改形会重建几何 | 每帧完整 EEVEE pass + depsgraph 同步 | **好**：一遍 2D pass，元素/批次可缓存，字形有缓存 |
| 复用度 | 低（只能复用 DebugDraw 的 2D 文本/盒子原型） | 低 | 中（overlay camera/collection、FontObject） | **高**（BLF/GPU/BGUI/legacy UI 节点语义/适配器框架） |
| 工作量（人日，推断） | 16–20 | 20–28 | 14–18 | **13–16** |
| 主要风险 | 等于重写一个 UI 渲染器，且文本/图片仍弱 | 数据管线复杂、无文本、更新成本高 | 每帧 EEVEE 成本、内容耦合、分屏/裁剪难 | 排版深度有限、Vulkan 状态管理、分屏/渲染到纹理需额外处理 |

> 表内"强/无"都是基于源码能力判定；性能列为推断。

---

## 3. 方案 1：`bge.render` 的 2D API

### 3.1 这个分支实际提供什么

- `bge.render` 的 Python 方法表只有窗口/鼠标/材质模式等管理函数，**唯一的绘制函数是 `drawLine`**：`source/gameengine/Ketsji/KX_PythonInit.cpp:1195-1221`。
- `drawLine` 收的是 `MT_Vector3`，落到 `KX_RasterizerDrawDebugLine`，也就是**世界坐标 3D 调试线**，不是屏幕空间 2D 基元：`KX_PythonInit.cpp:1052-1075`、`source/gameengine/Ketsji/KX_Globals.cpp:92-97`。
- 没有 `drawBox`、`drawText`、`drawCircle`、`makeTexture`、`makeFont`、文本对齐参数。全文检索 `rasterizer_methods` 与 `bge.render`，未发现第二套 2D 绘制入口。

### 3.2 内部其实有 2D 文本/盒子，但不对节点开放

- `RAS_DebugDraw` 有 `RenderBox2D` / `RenderText2D`：`source/gameengine/Rasterizer/RAS_DebugDraw.h:150,152`，实现在 `RAS_DebugDraw.cpp:142,149`。
- 这两个函数只被场景调试属性使用：`source/gameengine/Ketsji/KX_Scene.cpp:2357,2393,2401`；Python 侧无法调用。
- 文本使用固定的 `blf_mono_font`，字号来自 `fontSize`，只有位置和颜色：`source/gameengine/Rasterizer/RAS_OpenGLRasterizer/RAS_OpenGLDebugDraw.cpp:192-209`。**没有字体切换、没有字号参数、没有水平/垂直对齐、没有换行、没有图片、没有裁剪。**
- 调试列表每帧 flush 后清空，不能承载持久 UI 树：`RAS_DebugDraw.cpp:156` 起；`FlushDebugDraw` 在 `KX_KetsjiEngine.cpp:287,332`。

### 3.3 逐维度结论

| 维度 | 结论 | 证据 |
|---|---|---|
| 中文/字体/字号/对齐 | 不可用。无 UI 文本 API；内部文本是固定 mono 字体 + 固定字号，无对齐 | `KX_PythonInit.cpp:1195-1221`；`RAS_OpenGLDebugDraw.cpp:192-209` |
| 图片/纹理 | 不可用。`bge.render` 无纹理 API | 同上方法表 |
| 布局/锚点 | 不可用。没有 UI 元素概念 | 同上 |
| 鼠标命中 | 只能"自己画、自己算"，因为渲染层没有元素/矩形 | 推断（无 API） |
| Vulkan/EEVEE | 调试绘制通过 `GPU_*` / `imm*`，不直接调 GL，理论上跨后端；但节点仍需要 C++ 侧新入口 | `RAS_OpenGLDebugDraw.cpp` 全文无 `gl[A-Z]` 调用；`KX_PythonInit.cpp:1221` |
| 性能 | 每帧往调试列表塞线段，flush 即清空；无缓存、无批次复用 | `RAS_DebugDraw.cpp:156` |
| 复用度 | 只能复用 `RAS_DebugDraw`/`RAS_OpenGLDebugDraw` 的 2D 文本/盒子画法作为原型 | 见上 |
| 工作量 | 若真要补齐文本/图片/布局/命中，等于在引擎里新写一个 UI 渲染器，同时还要加 Python 与 C++ 两套入口：16–20 人日 | 推断 |
| 风险 | 高：做完仍缺字号/对齐/图片/裁剪 | — |

**判定：不推荐作为主方案。** 唯一可取的用法是：把 `RAS_DebugDraw::RenderBox2D/RenderText2D` 当作**第 0 周的临时原型**，验证"UI 画在屏幕空间"可行，但它不是产品级 UI 载体。

---

## 4. 方案 2：Grease Pencil / GPencil 数据块

### 4.1 渲染侧可行

- GPencil 引擎确实参与游戏渲染循环：`DRW_game_render_loop()` 在需要时启用 `view_data.grease_pencil`（`source/blender/draw/intern/draw_context.cc:3090-3098`）。
- GP v3（`OB_GREASE_PENCIL`）与 legacy（`OB_GPENCIL_LEGACY`）都能被引擎识别（`BL_DataConversion.cpp:869-870`）。

### 4.2 游戏侧不可行

- 在 gameengine 转换层，GP 对象**没有专门的游戏对象类型**：`OB_GREASE_PENCIL` 落到 `BL_gameobject_from_customobject()`，失败时退化为 `KX_EmptyObject`（`source/gameengine/Converter/BL_DataConversion.cpp:869-883`）。节点层拿不到笔画/点/图层的写接口。
- GP 的数据模型是笔画（stroke）+ 点（point）+ 材质，**没有字体/字形/排版概念**。`source/blender/draw/engines/gpencil/` 内没有文本/字体调用（源码检索为空，属"证据缺失"判断，标**推断**）。
- 要用 GP 显示中文，得先把文字转成曲线/笔画，再逐帧重建 stroke；这已经绕开了 BLF，且排版、换行、对齐全部要自己实现。
- 更新成本：GP 的 batch cache 有缓存，但每次改笔画/图层会触发 GP 数据 tag 与几何重建；如果 UI 每帧改文本，等于每帧重建 GP 几何（**推断**，依据 GP 引擎的 batch cache dirty 机制）。
- 命中判定仍要自己做屏幕矩形检测，GP 几何帮不上忙。

### 4.3 逐维度结论

| 维度 | 结论 | 证据 |
|---|---|---|
| 中文/字体/字号/对齐 | 不可用。GP 无字形数据 | `BL_DataConversion.cpp:869-883`；GP 引擎目录无字体调用（推断） |
| 图片/纹理 | 只能贴到笔画材质，不能做"带 UV 的 UI 图片矩形" | GP 材质路径（推断） |
| 布局/锚点 | 不可用。GP 是绘制数据，不是布局系统 | 同上 |
| 鼠标命中 | 只能另做屏幕矩形命中 | 推断 |
| Vulkan/EEVEE | GP 引擎走 DRW，跨后端可用；但游戏逻辑侧没有 GP API | `draw_context.cc:3090-3098` |
| 性能 | 静态 GP 有 batch cache；动态 UI 每次更新重建几何，成本高 | 推断 |
| 复用度 | 低：只能复用 GP 引擎渲染，不能复用节点/适配器/文本 | — |
| 工作量 | 20–28 人日：GP 数据创建 6–8、文字转笔画 8–10、命中/更新 4–6、联调 2–4 | 推断 |
| 风险 | 高：文本、动态更新、内容耦合（必须在场景里建 GP 对象） | — |

**判定：不推荐。** 它不是"UI 载体"，而是"矢量绘制载体"；文本和动态更新这两项恰好是 UI 的硬需求。

---

## 5. 方案 3：独立渲染层 / overlay pass

方案 3 有三个子形态，分别评估。

### 5.1 子形态 3A：复用现有 overlay collection + overlay camera

现状：

- overlay collection 是真实功能：`KX_Scene::AddOverlayCollection()` / `SetOverlayCamera()`（`KX_Scene.cpp:534-549,662`）。
- 主 pass 会把带 `OB_OVERLAY_COLLECTION` 标记的对象排除，overlay pass 只画这些对象：`draw_context.cc:3106-3122`。
- overlay pass 使用 alpha premult 混合：`KX_KetsjiEngine.cpp:1045-1048`；并且关闭部分效果：`KX_Scene.cpp:889`。
- 文本可以用 `KX_FontObject`：`SetText()` + `UpdateCurveText()`（`KX_FontObject.h:49`、`KX_FontObject.cpp:60-84`），中文字形走 Blender 的 vfont 曲线。

问题：

1. **每帧仍跑完整 EEVEE**：`DRW_game_render_loop()` 无条件 `view_data.eevee.set_used(true)`（`draw_context.cc:3094`），overlay pass 只是换相机/换对象集，不是轻量 2D pass。
2. **文本对象是 3D 几何**：每次改字要重建 curve 字符串并 tag `ID_RECALC_GEOMETRY`（`KX_FontObject.cpp:73-84`），对"每帧变化的 HUD 数字"成本不低。
3. **布局/锚点靠对象 transform**，9 宫锚点、相对尺寸、裁剪、滚动都要自己再包一层。
4. **命中判定**：对象边界是世界空间，屏幕矩形仍要自己算；而且 overlay 对象参与 depsgraph 同步。
5. 内容耦合：UI 必须在场景里以对象/集合存在，节点运行时没有"纯 UI 元素表"。

**判定：能实现，但不是最优主路径。** 适合"3D 面板/世界空间 UI"，不适合 B4.1 的屏幕 UI。

### 5.2 子形态 3B：EndFrame 处插入独立 2D pass（推荐）

为什么这个插入点成立：

- `KX_KetsjiEngine::Render()` 在合成完成后已经设置好**屏幕空间坐标系**：`GPU_viewport`、`GPU_scissor_test(true)`、`GPU_scissor(...)`、`GPU_matrix_ortho_set(0, width, 0, height, -100, 100)`（`KX_KetsjiEngine.cpp:812-817`），随后调用 `EndFrame()`，注释就是"Draw last remaining debug drawings + few stuff + swapBuffer"（同文件:819-820）。
- `EndFrame()` 内先画调试属性、再 `FlushDebugDraw`、再 `SwapBuffers`（`KX_KetsjiEngine.cpp:279-321`）。UI pass 可以插在 `FlushDebugDraw` 之后、`SwapBuffers` 之前。
- 同一位置的 `FlushDebugDraw` 已经在用 BLF 画 2D 文本（`RAS_OpenGLDebugDraw.cpp:192-209`），证明"BLF 在这个 GPU 状态下可用"。
- 视口模式走 `EndFrameViewportRender()`，同样有调试 flush（`KX_KetsjiEngine.cpp:324-332`），只是不 swap（363 行被注释），需要额外处理（见第 9 节）。

绘制能力：

- **矩形/纯色**：`GPU_SHADER_3D_UNIFORM_COLOR`（`source/blender/gpu/GPU_shader_builtin.hh:151`）。
- **图片矩形 + 颜色 tint**：`GPU_SHADER_2D_IMAGE_RECT_COLOR`（同文件:36）；纹理通过 `BKE_image_acquire_gpu_texture(Image*, ImageUser*)` 获取，返回的是需要 `GPU_texture_free` 的 owned reference（`source/blender/blenkernel/BKE_image_gpu.hh:28-32`）。
- **文本**：BLF 提供 `BLF_load` / `BLF_load_mem`（`source/blender/blenfont/BLF_api.hh:67-68`）、`BLF_size`（144）、`BLF_position`（143）、`BLF_color4fv`（170）、`BLF_draw`（190）、`BLF_width_and_height`（326-327）、`BLF_width_to_strlen`（269）、**原生断行** `BLF_wordwrap` / `BLF_string_wrap`（346-351）、**旋转** `BLF_rotation`（343）、**裁剪** `BLF_clipping`（344）、阴影 `BLF_shadow` / `BLF_shadow_offset`（364/372）。
- **中文**：内置字体栈包含 `Noto Sans CJK Regular.woff2`（`release/datafiles/fonts/`），由 `BLF_load_font_stack()` 加载（`blf_font_default.cc:95-100`）；缺字回退只认 `BLF_DEFAULT` 字体的栈（`blf_glyph.cc:796-827`）。
- **对齐**：BLF 本身不做对齐，但可用 `BLF_width`/`BLF_height`（`BLF_api.hh:298,300`）算出文本尺寸后调整 `BLF_position`；legacy UI 节点已有 `halign`/`valign` 语义可作为对齐枚举（`editor/enum_types.py:588-601`）。
- **布局/命中**：屏幕矩形布局在 CPU 侧完成，鼠标位置来自 `KX_InputAdapter::MousePosition`（`KX_GameAdaptersEngine.cpp:198-213`），命中就是矩形包含判断；这不是新发明，BGUI 已经这么做了（`scripts/modules/bgui/widget.py:423-460`）。
- **现有 BGUI 参考实现**：`System` 用 `gpu.state.viewport_get()` 取视口（`bgui/system.py:33`）；`Label` 用 `blf.load/size/position/draw/dimensions`（`bgui/label.py:40,80-81,106-107,112-114`）；`Image` 用 `gpu.texture.from_image` + `batch_for_shader`（`bgui/image.py:33,42,56,67`）。这套 Python 实现可以直接作为原生渲染器的行为对照。

性能：

- UI 只占一遍 2D pass，不再跑 EEVEE；元素批次可以缓存，文本字形由 BLF glyph cache 缓存；只有脏元素需要重建（**推断**，依据 BLF glyph cache 与 GPU batch 机制）。
- 相比 3A，省掉一整遍 EEVEE + depsgraph 同步；相比方案 1，省掉"重写渲染器"的大部分工作。

**判定：推荐。**

### 5.3 子形态 3C：在 EEVEE/Vulkan 后端里挂 2D pass

- 需要在 `draw_context.cc` 的 EEVEE/overlay 流程里新增 engine/pass，并处理 Vulkan/GL 两套后端状态、framebuffer、MSAA、色彩管理。
- 现有代码里已经有 Vulkan + 自定义 BGE viewport 的 clear 兼容注释（`KX_Scene.cpp:917-921`），说明这条线已有过状态管理坑。
- 这超出 B4.1 的 UI 节点范围，且会把 UI 与渲染后端强耦合。

**判定：不推荐。** 3B 已经能复用现有 GPU 管线，没有必要改后端。

---

## 6. 推荐方案与理由

**推荐 3B：`EndFrame` 屏幕空间 2D UI pass + BLF/GPU batch + `KX_UIAdapter`。**

一句话：它把 UI 当作"每帧一遍 2D 绘制 + 一张持久元素表"，既拿到了 BLF 的中文文本和 GPU 的纹理能力，又避免了 overlay collection 的 EEVEE 全量 pass 和 GPencil 的几何重建。

推荐方案的边界（明确写死，避免后续误解）：

- **不改 EEVEE/Vulkan 后端**；只加 `KX_UIAdapter`、`KX_UIRenderer`、`KX_Scene::DrawUI()` 和一个 `EndFrame` 调用点。
- **不把 UI 做成场景物体**；UI 元素表由适配器持有，句柄是 `uint32_t`，不新增 `KX_ValueType`（复用 `KX_VALUE_INT`）。
- **不做复杂文本排版**；B4.1 只承诺单字体、字号、颜色、水平/垂直对齐、按宽度断行、阴影。富文本/彩色 emoji/竖排留到 B4.2。

---

## 7. 落地步骤

### 7.1 适配器接口草稿（按 SOP 中性默认实现 + Mock）

放在 `source/gameengine/GameLogic/GameNodes/KX_GameAdapters.h`，紧接 `KX_AudioAdapter` 之后；实现分三处：Mock/Null 在 `KX_GameAdapters.cpp`，引擎实现在 `KX_GameAdaptersEngine.cpp`。

```cpp
// ---- KX_GameAdapters.h ----
using KX_UIHandle = uint32_t;
inline constexpr KX_UIHandle KX_UI_NONE = 0;

enum class KX_UIAnchorX : uint8_t { Left, Center, Right };
enum class KX_UIAnchorY : uint8_t { Bottom, Center, Top };

struct KX_UIRect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

struct KX_UIStyle {
  float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};        /* 背景色 */
  float border_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float border_width = 0.0f;
  float angle = 0.0f;
  bool visible = true;
  bool enabled = true;
  bool clipping = false;                              /* 子元素裁剪 */
};

struct KX_UITextStyle {
  float font_size = 16.0f;
  float line_height = 1.0f;                           /* 相对字号 */
  float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  KX_UIAnchorX halign = KX_UIAnchorX::Left;
  KX_UIAnchorY valign = KX_UIAnchorY::Top;
  bool wrap = false;
  bool shadow = false;
  float shadow_offset[2] = {1.0f, 1.0f};
  float shadow_color[4] = {0.0f, 0.0f, 0.0f, 0.5f};
};

struct KX_UIHit {
  KX_UIHandle widget = KX_UI_NONE;
  bool hover = false;
  bool pressed = false;
  bool released = false;
  float local_x = 0.0f;
  float local_y = 0.0f;
};

class KX_UIAdapter {
 public:
  virtual ~KX_UIAdapter() = default;

  /* 每帧在节点 tick 之前调用；指针为输入设备像素坐标，
   * 适配器内部负责换算到 UI 画布坐标（注意 Y 轴方向）。 */
  virtual void BeginFrame(float delta_time, float mouse_x, float mouse_y, bool mouse_down) {}
  virtual void EndFrame() {}

  virtual KX_UIHandle CreateCanvas(const char *name, float design_w, float design_h, int scale_mode)
  {
    (void)name; (void)design_w; (void)design_h; (void)scale_mode;
    return KX_UI_NONE;
  }

  virtual KX_UIHandle CreateLayout(KX_UIHandle parent, const KX_UIRect &rect, bool rel_pos,
                                  bool rel_size, KX_UIAnchorX ax, KX_UIAnchorY ay,
                                  const KX_UIStyle &style)
  {
    (void)parent; (void)rect; (void)rel_pos; (void)rel_size; (void)ax; (void)ay; (void)style;
    return KX_UI_NONE;
  }

  virtual KX_UIHandle CreateLabel(KX_UIHandle parent, const KX_UIRect &rect, bool rel_pos,
                                  const KX_UIStyle &style, const KX_UITextStyle &text_style,
                                  const char *text, const char *font_name)
  {
    (void)parent; (void)rect; (void)rel_pos; (void)style; (void)text_style; (void)text; (void)font_name;
    return KX_UI_NONE;
  }

  virtual KX_UIHandle CreateImage(KX_UIHandle parent, const KX_UIRect &rect, bool rel_pos,
                                  bool rel_size, const KX_UIStyle &style, const char *image_name,
                                  const float tint[4], bool preserve_aspect)
  {
    (void)parent; (void)rect; (void)rel_pos; (void)rel_size; (void)style;
    (void)image_name; (void)tint; (void)preserve_aspect;
    return KX_UI_NONE;
  }

  virtual KX_UIHandle CreateButton(KX_UIHandle parent, const KX_UIRect &rect, bool rel_pos,
                                   bool rel_size, const KX_UIStyle &style, const KX_UIStyle &hover_style,
                                   const KX_UITextStyle &text_style, const char *text, const char *font_name)
  {
    (void)parent; (void)rect; (void)rel_pos; (void)rel_size; (void)style; (void)hover_style;
    (void)text_style; (void)text; (void)font_name;
    return KX_UI_NONE;
  }

  virtual KX_UIHandle CreateSlider(KX_UIHandle parent, const KX_UIRect &rect, bool rel_pos,
                                   bool rel_size, const KX_UIStyle &style, float bar_width,
                                   const float bar_color[4], const float bar_hover_color[4],
                                   float knob_size, const float knob_color[4],
                                   bool allow_bar_click, float value)
  {
    (void)parent; (void)rect; (void)rel_pos; (void)rel_size; (void)style; (void)bar_width;
    (void)bar_color; (void)bar_hover_color; (void)knob_size; (void)knob_color;
    (void)allow_bar_click; (void)value;
    return KX_UI_NONE;
  }

  virtual bool MoveWidget(KX_UIHandle widget, int mode) { (void)widget; (void)mode; return false; }

  virtual bool SetAttribute(KX_UIHandle widget, const char *attr, const void *value, int value_type)
  {
    (void)widget; (void)attr; (void)value; (void)value_type;
    return false;
  }

  virtual bool GetAttribute(KX_UIHandle widget, const char *attr, void *value, int value_type) const
  {
    (void)widget; (void)attr; (void)value; (void)value_type;
    return false;
  }

  virtual bool IsValid(KX_UIHandle widget) const { (void)widget; return false; }

  /* 引擎实现里真正绘制；Mock/Null 为空实现。viewport 尺寸用于布局与裁剪。 */
  virtual void Draw(float viewport_w, float viewport_h) { (void)viewport_w; (void)viewport_h; }

  virtual void Clear() {}
};

class KX_MockUIAdapter : public KX_UIAdapter {
 public:
  std::vector<std::string> calls;
  uint32_t create_calls = 0;
  uint32_t set_calls = 0;
  uint32_t hit_calls = 0;
  KX_UIHandle next_handle = 1;
  bool result = true;

  void Reset();
};
```

装配顺序（按 `docs/NATIVE_NODE_ADAPTER_SOP.md:54-57`）：

1. `KX_AdapterSet` 增 `std::unique_ptr<KX_UIAdapter> ui`（`KX_GameAdapters.h:957-965`）。
2. `KX_ExecutionContext::SetAdapters()` 末尾加第 9 个参数（默认 `nullptr`），加 `KX_UIAdapter &UI()`（`KX_ExecutionContext.h:153-168`）。
3. `KX_CreateEngineAdapters` / `KX_CreateNullAdapters` 都装配 UI（`KX_GameAdaptersEngine.cpp` 末尾）。
4. `KX_GraphSceneManager` 把 `adapters_->ui.get()` 传入 `SetAdapters`（`KX_GraphSceneManager.cpp:136-144`）。
5. `KX_AdapterSet::BeginFrame()` 里，先取鼠标，再 `ui->BeginFrame(delta, mx, my, down)`（`KX_GameAdapters.h:967`）。

### 7.2 9 个 UI 节点建议清单

节点 ID 直接沿用 legacy UI 节点名，保证迁移语义一致；端口顺序按 legacy 定义（证据见每行的文件:行号）。

| # | runtime_id | kind | 端口（输入 / 输出） | 配置 | legacy 依据 |
|---|---|---|---|---|---|
| 1 | `CreateUICanvas` | ACT | OnEnter / Done, Canvas | `scale_mode`（stretch/fit/fill）、设计分辨率 | `editor/nodes/ui/createuicanvas.py:55-57` |
| 2 | `CreateUILayout` | ACT | OnEnter, Parent, RelPos, Pos, RelSize, Size, Angle, Color, BorderWidth, BorderColor, Inverted, Spacing, Rows, Columns, StartingAngle, Radius / Done, Layout | `layout_type`（Sequential/Box/Polar）、`halign`、`valign` | `ui/createuilayout.py:91-114`；枚举 `enum_types.py:588-601` |
| 3 | `CreateUILabel` | ACT | OnEnter, Parent, RelPos, Pos, Angle, Text, Font, FontSize, LineHeight, FontColor, UseShadow, ShadowOffset, ShadowColor / Done, Label | `halign`、`valign`；**B4.1 新增** `text_halign`、`text_valign`、`wrap` | `ui/createuilabel.py:54-69`；属性表 `enum_types.py:520-558` |
| 4 | `CreateUIImage` | ACT | OnEnter, Parent, RelPos, Pos, RelSize, Size, Angle, Texture / Done, Image | **B4.1 新增** `tint`、`preserve_aspect`（可选） | `ui/createuiimage.py:52-62` |
| 5 | `CreateUIButton` | ACT | OnEnter, Parent, RelPos, Pos, RelSize, Size, Angle, Color, HoverColor, BorderWidth, BorderColor, Text, TextPos, Font, FontSize, LineHeight, FontColor / Done, Button, OnClick, OnHover, OnRelease | `halign`、`valign`、`text_halign`、`text_valign` | `ui/createuibutton.py:79-101` |
| 6 | `CreateUISliderWidget` | ACT | OnEnter, Parent, RelPos, Pos, RelSize, Size, Angle, BarWidth, BarColor, BarHoverColor, BarTexture, BorderWidth, BorderColor, KnobSize, KnobColor, KnobHoverColor, KnobTexture, InitialValue, Steps, AllowBarClick / Done, Slider, SliderValue, KnobPosition | `orientation`（可选） | `ui/createuislider.py:84-108` |
| 7 | `MoveWidget` | ACT | OnEnter, Widget / Done | `mode`（front/up/down/back） | `ui/movewidget.py:22-27` |
| 8 | `SetUIWidgetAttr` | ACT | OnEnter, Widget, Value（按属性类型复用 Bool/Color/Float 等端口） / Done | `widget_attr` 枚举 | `actions/setuiwidgetattr.py:44-53`；属性表 `enum_types.py:520-558` |
| 9 | `GetUIWidgetAttr` | PAR | Widget / Visibility, Color, Alpha, Vec2, String, Int, Float, Font, Image | `widget_attr` 枚举 | `parameters/getuiwidgetattr.py:51-61` |

说明：

- 第 8/9 个节点沿用 legacy 的"一个 config 选属性 + 一个 Value 端口"模型，避免给每个属性单独开节点。legacy 的可写属性集已覆盖 pivot/size/angle/width/height/clipping/halign/valign/border/text/font/font_color/font_size/line_height/text_halign/text_valign/wrap/shadow/texture（`editor/enum_types.py:520-558`），B4.1 只需实现其中 B4.1 承诺的子集。
- `AddUIWidget`（legacy 的第 11 个节点）不纳入 B4.1：Canvas/Layout 的 Parent 端口已经覆盖添加语义。
- 节点用的 `Font` / `Image` socket 已存在：`NodeSocketLogicFont`（`editor/sockets/fontsocket.py:11-29`）、`NodeSocketLogicImage`（`imagesocket.py:11-29`）、`NodeSocketLogicUI`（`uisocket.py:9-13`）。

### 7.3 数据流：节点配置 → IR → 运行时 → 渲染

```text
[编辑器]
native_b4.py 表驱动条目
  (runtime_id, label, kind, inputs, outputs, config)
        │
        ▼
[IR 导出 ir_export.py]
  nodes[] + connections[] + config{}
  需要补 3 个映射：
    SOCKET_TYPE_UI(15)   -> 'Int'      （UI 句柄，复用 KX_VALUE_INT）
    SOCKET_TYPE_IMAGE(20)-> 'DataBlock'（按 datablock name 序列化）
    SOCKET_TYPE_FONT(21) -> 'DataBlock'
  现状：_VALUE_TAGS 只有 DATABLOCK，没有 UI/IMAGE/FONT
  （ir_export.py:40-52；socket 常量见 socket.py:55,60,61）
        │
        ▼
[运行时编译]
KX_GraphCompiler 把 value_tag 映射到 KX_ValueType
  'Int' -> KX_VALUE_INT（UI 句柄）
  'DataBlock' -> KX_VALUE_DATABLOCK（KX_SceneManifest.cpp:63-65）
  节点表 KX_FunctionNodesB4.cpp：kB4Specs[] + B4_ACTION/B4_PAR
  注册进 KX_RuntimeNodes.cpp（B2/B3 的注册点：KX_RuntimeNodes.cpp:235-236）
        │
        ▼
[每帧执行]
KX_AdapterSet::BeginFrame(dt)
  input->BeginFrame(dt) -> ui->BeginFrame(dt, mouse_x, mouse_y, down)
graph tick：CreateUICanvas / CreateUILayout / ... / CreateUIButton
  -> ctx.UI().CreateXxx(...) -> 返回 uint32 句柄
  -> SetUIWidgetAttr：ctx.UI().SetAttribute(handle, attr, value)
  -> GetUIWidgetAttr：ctx.UI().GetAttribute(handle, attr, out)
  -> CreateUIButton / CreateUIWidget 的 OnClick/OnHover/OnRelease
     来自 ui->BeginFrame() 里的矩形命中结果
        │
        ▼
[渲染]
KX_KetsjiEngine::EndFrame()
  RenderDebugProperties()
  FlushDebugDraw()                 ← 现有 BLF 文本也在这里画
  for (KX_Scene *scene : m_scenes) scene->DrawUI();
  SwapBuffers()
        │
        ▼
KX_Scene::DrawUI()
  -> m_graphmgr->Adapters()->ui->Draw(canvas_w, canvas_h)
  -> EngineUIAdapter::Draw()
       ├─ 计算绝对矩形（canvas 缩放 / 相对位置 / 9 宫锚点 / 裁剪）
       ├─ 矩形：GPU_SHADER_3D_UNIFORM_COLOR
       ├─ 图片：GPU_SHADER_2D_IMAGE_RECT_COLOR + BKE_image_acquire_gpu_texture
       └─ 文本：BLF_size/position/color/draw（中文依赖 BLF_load_font_stack）
```

IR 侧的具体缺口（必须写进实现任务）：

- `SOCKET_TYPE_UI` (15)、`SOCKET_TYPE_IMAGE` (20)、`SOCKET_TYPE_FONT` (21) 不在 `_VALUE_TAGS`（`generator/ir_export.py:40-52`，常量在 `editor/sockets/socket.py:55,60,61`）。
- `materialize_constants()` 只处理 `Float`/`Bool`（`ir_export.py:281-304`），datablock 默认值不会自动变成常量；因此 B4.1 建议 **Font/Image 走 config 字符串（datablock 名）+ 运行时按名解析**，避免扩展 IR 值类型。
- 若坚持用 datablock socket，需要把名字写进 IR config，并在运行时通过 `bmain` 按名查找；这属于额外 2–3 人日（推断）。

### 7.4 渲染插入点与生命周期

- 插入点：`KX_KetsjiEngine::EndFrame()` 中 `FlushDebugDraw()` 之后、`m_rasterizer->EndFrame()` / `SwapBuffers()` 之前（`KX_KetsjiEngine.cpp:279-321`）。
- 视口模式：`EndFrameViewportRender()` 也一样插入（`KX_KetsjiEngine.cpp:324-332`），但该路径不 swap（363 行注释掉），并且最终显示由编辑器控制；**B4.1 可先只保证独立 player 窗口，编辑器 Viewport Render 标为已知缺口**。
- 状态保存/恢复：UI pass 前设置 `GPU_depth_test(GPU_DEPTH_NONE)`、`GPU_blend(GPU_BLEND_ALPHA_PREMULT)`、必要时 `GPU_scissor`；绘制后恢复默认状态（参考 `RAS_OpenGLDebugDraw.cpp:149-152` 的 `StateSet::set()` + `GPU_depth_test(GPU_DEPTH_ALWAYS)`）。
- 生命周期：`KX_Scene` 卸载时 `m_graphmgr.reset()`（`KX_Scene.cpp:344-348`），UI 元素表随适配器销毁；节点持有的句柄在同一场景内有效，跨场景必须视为失效。
- 字体初始化：`KX_UIAdapter` 首次创建 Label 时调用一次 `BLF_load_font_stack()`（`blf_font_default.cc:95-100`），否则 player 里中文会缺字（`GPG_ghost.cpp:1045,1047` 未加载字体栈）。
- 字体资源：优先用字体 datablock（`VFont.filepath` / `VFont.packedfile`，`DNA_vfont_types.h:29,32`）：外部字体 `BLF_load(filepath)`，打包字体 `BLF_load_mem(name, packed->data, packed->size)`（`BLF_api.hh:67-68`）。如果用户选的是没有 CJK 字形的字体，回退仍依赖 `BLF_DEFAULT` 字体栈（`blf_glyph.cc:796-827`）。
- 图片资源：按名解析 `Image*`，`BKE_image_acquire_gpu_texture()` 取纹理并缓存，元素销毁时 `GPU_texture_free()`（`BKE_image_gpu.hh:28-32`）。

### 7.5 工作量拆分（推荐方案，推断）

| 工作项 | 人日 | 说明 |
|---|---|---|
| 规格冻结 + 证据复核 + 端口表评审 | 0.5–1 | 本文件 + 9 节点端口/配置定稿 |
| `KX_UIAdapter` + Mock/Null + 装配/访问器 | 1.5–2 | 按 SOP 三件套；`KX_AdapterSet` / `ExecutionContext` / `GraphSceneManager` |
| `KX_UIRenderer`：布局、矩形、图片、BLF 文本、裁剪 | 4–5 | 含 canvas 缩放、锚点、相对尺寸、脏元素批次缓存 |
| `EndFrame` / `EndFrameViewportRender` 插入 + `KX_Scene::DrawUI()` | 0.5–1 | 状态保存/恢复、场景遍历 |
| 9 节点 C++ 规格表 + 执行函数 + 注册 | 3–4 | `KX_FunctionNodesB4.cpp`、`KX_RuntimeNodes.cpp` |
| 编辑器 `native_b4.py` + 导入 + IR/端口映射 | 1.5–2 | 含 UI/Image/Font 的 IR tag 与 config 序列化 |
| Mock 单测 + 编辑器注册测试 + E2E + 回归 | 2–3 | 沿用 SOP 第 5 步 |
| **合计** | **13–16** | 不含复杂排版/富文本 |

---

## 8. 放弃另外两条方案的具体原因

### 8.1 放弃方案 1（`bge.render`）

1. **它没有 UI 所需的 API 面**：唯一绘制函数是 3D 线段 `drawLine`（`KX_PythonInit.cpp:1195-1221`），没有文本、矩形、图片、字体、对齐。
2. **内部 2D 文本不可复用为产品能力**：`RenderText2D` 只有固定 mono 字体 + 固定字号 + 位置/颜色（`RAS_OpenGLDebugDraw.cpp:192-209`），没有字号/字体/对齐/换行/裁剪参数；`RenderBox2D` 只有纯色矩形（`RAS_DebugDraw.h:150`）。
3. **调试绘制是每帧清空的临时列表**（`RAS_DebugDraw.cpp:156`），不能持有持久 UI 树、不能做元素级脏更新。
4. **层级不匹配**：`bge.render` 是 Python API；B4 节点是 C++ 原生运行时，最终仍要写 `KX_UIAdapter` + 引擎渲染器。选它等于"多写一层 Python API + 从零补文本/图片/布局"，工作量不低于方案 3B，能力却更弱。

### 8.2 放弃方案 2（GPencil）

1. **游戏侧没有 GP 数据写接口**：GP 对象在转换层退化为通用 `KX_GameObject` / `KX_EmptyObject`（`BL_DataConversion.cpp:869-883`），节点层无法通过游戏对象 API 改笔画/点/图层。
2. **没有文本能力**：GP 数据模型是笔画/点，不是字形；中文要先转曲线/笔画，等于自己重做排版（`source/blender/draw/engines/gpencil/` 无字体调用，**推断**）。
3. **更新成本高**：UI 文本/数值每帧变化会导致 GP 数据/几何重建（**推断**，依据 GP batch cache dirty 机制），而且 GP 引擎在游戏循环里也要跑一遍 pass（`draw_context.cc:3090-3098`）。
4. **布局/命中仍要另做**：GP 不提供 UI 布局或屏幕矩形命中，最终还是要写方案 3B 的布局/命中代码，GP 只多了"画矢量形状"这一项收益。

---

## 9. 什么情况下推荐会翻车

1. **需要富文本/复杂排版**：多字体混排、彩色 emoji、竖排、双向文字、复杂断行规则。BLF 只提供单字体绘制 + 宽度量算 + 简单断行（`BLF_wordwrap` / `BLF_string_wrap`，`BLF_api.hh:346-351`），没有排版引擎；B4.1 只承诺"按宽度断行"，超出就会翻车。
2. **每帧大量文本/元素更新**：例如 200+ 个每帧变化的 Label、逐字动画、滚动日志。BLF 每条 `BLF_draw` 一次，CPU 布局 + 逐元素批次会顶不住；需要图集化/文本缓存，当前设计没有。
3. **分屏/多相机 + 每视口 UI**：3B 在 `EndFrame()` 里画一次全屏；如果每个 camera viewport 需要独立 HUD，需要改成 per-camera pass（`KX_KetsjiEngine.cpp:812-817` 的 viewport/scissor 是按最终合成区域设置的）。
4. **编辑器 Viewport Render 模式**：`EndFrameViewportRender()` 不 swap（`KX_KetsjiEngine.cpp:363` 注释），显示由编辑器接管；如果只在 `EndFrame` 插入，编辑器内可能看不到 UI（**推断**，需要实机验证）。
5. **渲染到纹理/截图/ImageRender 路径**：UI 画在 backbuffer 上，如果录制/离屏渲染不经过 `EndFrame`，UI 不会出现在输出里（**推断**）。
6. **Vulkan 状态污染**：3B 直接改 GPU 状态。若不严格保存/恢复，可能影响后续 2D filter 或下一帧；仓库里已有 Vulkan + 自定义 BGE viewport 的 clear 兼容注释（`KX_Scene.cpp:917-921`），说明这类问题真实存在。
7. **场景切换/热重载**：UI 句柄属于当前场景的适配器；`m_graphmgr.reset()` 后旧句柄失效（`KX_Scene.cpp:344-348`）。如果节点把句柄存进跨场景状态，会拿到野句柄。
8. **需要 UI 参与后处理/景深/2D filter**：3B 画在 filters 之后（filters 在 `KX_Scene.cpp:924-926`，UI 在 `EndFrame`），不会被 2D filter 影响；如果需要"UI 也被滤镜处理"，得把插入点前移，和"UI 不受后处理影响"的目标冲突。
9. **需要 3D 世界空间 UI 面板**：例如贴在物体上的血条、VR 面板。3B 是屏幕空间；这类需求反而应该用 3A 的 overlay collection + `KX_FontObject`（`KX_Scene.cpp:534-549`、`KX_FontObject.cpp:60-84`）。
10. **字体/图片资源未打包**：player 按 `VFont.filepath` / 图片路径解析；如果 blend 没打包字体/图片，运行时找不到资源（**推断**，资源解析路径需实机验证）。
11. **字体目录缺失**：`BLF_load_font_stack()` 依赖 `BLENDER_DATAFILES/fonts` 目录；找不到目录时只打印错误并继续（`blf_font_default.cc:58-66`），表现为中文全部缺字。便携版/裁剪版发行包必须确认 datafiles 带上 `Noto Sans CJK Regular.woff2`。

---

## 10. 证据索引

### 10.1 `bge.render` / 调试绘制

| 结论 | 证据 |
|---|---|
| `bge.render` 方法表只有 `drawLine`，无 2D 文本/矩形/图片 | `source/gameengine/Ketsji/KX_PythonInit.cpp:1195-1221` |
| `drawLine` 是 3D 世界坐标线段 | `KX_PythonInit.cpp:1052-1075`；`source/gameengine/Ketsji/KX_Globals.cpp:92-97` |
| 模块注册名 `bge.render` | `KX_PythonInit.cpp:1853` |
| `RAS_DebugDraw` 有 `RenderBox2D` / `RenderText2D` | `source/gameengine/Rasterizer/RAS_DebugDraw.h:150,152`；`RAS_DebugDraw.cpp:142,149` |
| 2D 文本只被调试属性使用 | `source/gameengine/Ketsji/KX_Scene.cpp:2357,2393,2401` |
| 文本固定 mono 字体、固定字号、仅位置/颜色 | `source/gameengine/Rasterizer/RAS_OpenGLRasterizer/RAS_OpenGLDebugDraw.cpp:192-209` |
| 调试列表 flush 后清空 | `RAS_DebugDraw.cpp:156` |

### 10.2 GPencil

| 结论 | 证据 |
|---|---|
| GP 引擎参与游戏渲染循环 | `source/blender/draw/intern/draw_context.cc:3090-3098` |
| GP 对象在游戏侧退化为通用对象/Empty | `source/gameengine/Converter/BL_DataConversion.cpp:869-883` |
| GP 引擎目录无文本/字体调用（推断） | `source/blender/draw/engines/gpencil/` 源码检索为空 |
| GP batch cache dirty / 重建（推断） | GP 引擎 batch cache 机制（未逐行引用） |

### 10.3 overlay / 渲染 pass

| 结论 | 证据 |
|---|---|
| overlay collection + overlay camera 存在 | `source/gameengine/Ketsji/KX_Scene.cpp:534-549,662` |
| 主 pass 排除 overlay 对象，overlay pass 只画 overlay 对象 | `source/blender/draw/intern/draw_context.cc:3106-3122` |
| overlay pass alpha premult | `source/gameengine/Ketsji/KX_KetsjiEngine.cpp:1045-1048` |
| overlay pass 关闭部分效果 | `KX_Scene.cpp:611,889` |
| overlay pass 仍启用 EEVEE | `draw_context.cc:3094` |
| 2D filters 在相机 setup 内执行 | `KX_Scene.cpp:924-926` |
| Vulkan + 自定义 BGE viewport 的 clear 兼容注释 | `KX_Scene.cpp:917-921` |
| `EndFrame` 前已设置屏幕正交/视口/scissor | `KX_KetsjiEngine.cpp:812-820` |
| `EndFrame` 顺序：调试属性 → FlushDebugDraw → swap | `KX_KetsjiEngine.cpp:279-321` |
| 视口模式 `EndFrameViewportRender` 不 swap | `KX_KetsjiEngine.cpp:324-367` |
| `PostRenderScene` 无调用点 | 定义 `KX_KetsjiEngine.cpp:1062`；全仓检索无 caller |
| `POST_DRAW` 实际由调试 flush 触发 | `RAS_OpenGLDebugDraw.cpp:145-147` |

### 10.4 文本 / 字体 / 图片

| 结论 | 证据 |
|---|---|
| 字体栈加载（含 Noto CJK） | `source/blender/blenfont/intern/blf_font_default.cc:95-100` |
| 内置 `Noto Sans CJK Regular.woff2` | `release/datafiles/fonts/` |
| player 只加载默认/mono，不加载字体栈 | `source/gameengine/GamePlayer/GPG_ghost.cpp:1045,1047` |
| 缺字回退只认 `BLF_DEFAULT` 字体 | `source/blender/blenfont/intern/blf_glyph.cc:796-827` |
| BLF 字体/字号/位置/颜色/绘制/量算 API | `source/blender/blenfont/BLF_api.hh:67-68,143,144,170,190,269,298,300` |
| BLF 断行/旋转/裁剪 API | `BLF_api.hh:343,344,346-351` |
| BLF 阴影 API | `BLF_api.hh:364,372` |
| 图片 -> GPU 纹理，需释放 | `source/blender/blenkernel/BKE_image_gpu.hh:28-32` |
| 2D 图片矩形 shader / 纯色 shader | `source/blender/gpu/GPU_shader_builtin.hh:36,151` |
| `KX_FontObject` 文本更新会 tag 几何重建 | `source/gameengine/Ketsji/KX_FontObject.h:49`；`KX_FontObject.cpp:60-84` |
| VFont 有 filepath / packedfile | `source/blender/makesdna/DNA_vfont_types.h:29,32` |

### 10.5 BGUI 参考实现

| 结论 | 证据 |
|---|---|
| System 用 `gpu.state.viewport_get()` 取视口 | `scripts/modules/bgui/system.py:33` |
| Label 用 `blf.load/size/position/draw/dimensions` | `scripts/modules/bgui/label.py:40,80-81,106-107,112-114` |
| Image 用 `gpu.texture.from_image` + `batch_for_shader` | `scripts/modules/bgui/image.py:33,42,56,66` |
| 命中判定是矩形包含 | `scripts/modules/bgui/widget.py:423-460` |

### 10.6 适配器 / 节点 / IR

| 结论 | 证据 |
|---|---|
| `KX_AdapterSet` 现有 8 个适配器 | `source/gameengine/GameLogic/GameNodes/KX_GameAdapters.h:957-969` |
| 输入适配器提供像素鼠标位置 | `source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp:198-213` |
| 场景适配器提供屏幕尺寸 | `KX_GameAdaptersEngine.cpp:473-483` |
| `ExecutionContext::SetAdapters` / 访问器 | `KX_ExecutionContext.h:153-168` |
| GraphSceneManager 传入全部适配器 | `KX_GraphSceneManager.cpp:136-144` |
| B3 规格表与注册宏 | `KX_FunctionNodesB3.cpp:656-671,674` |
| B2/B3 注册入口 | `KX_RuntimeNodes.cpp:235-236` |
| 编辑器表驱动 | `editor/nodes/native_b3.py:12-18`；`native_batch.py:32-48,143` |
| IR 值标签缺 UI/Image/Font | `generator/ir_export.py:40-52`；`editor/sockets/socket.py:55,60,61` |
| IR 常量只物化 Float/Bool | `generator/ir_export.py:281-304` |
| legacy UI 节点依赖外部 `uplogic` | `editor/nodes/ui/createuibutton.py:28`；`bge_gamenodes/__init__.py:364-376` |
| native runtime 开启时不生成 uplogic 代码（避免跑两遍） | `bge_gamenodes/__init__.py:91-98` |
| legacy UI 节点语义/端口 | `ui/createuicanvas.py:55-57`；`ui/createuilayout.py:91-114`；`ui/createuilabel.py:54-69`；`ui/createuiimage.py:52-62`；`ui/createuibutton.py:79-101`；`ui/createuislider.py:84-108`；`ui/movewidget.py:22-27`；`actions/setuiwidgetattr.py:44-53`；`parameters/getuiwidgetattr.py:51-61` |
| 锚点/对齐/可写属性枚举 | `editor/enum_types.py:520-558,588-601` |
| UI/Font/Image socket 已存在 | `editor/sockets/uisocket.py:9-13`；`fontsocket.py:11-29`；`imagesocket.py:11-29` |

### 10.7 运行时与后端

| 结论 | 证据 |
|---|---|
| 值类型枚举（含 Int/Datablock） | `source/gameengine/GameLogic/GameNodes/KX_RuntimeValue.h:39-58` |
| 值槽布局：`type` + 16B union，实测 `sizeof=24B`、`alignof=8`（g++ 实测，见 12.2） | `KX_RuntimeValue.h:95-104` |
| 编译器支持 DataBlock 标签 | `KX_SceneManifest.cpp:63-65`；`KX_GraphCompiler.cpp:88`（类型转换白名单） |
| Linux 默认 GPU 后端是 Vulkan | `source/blender/makesdna/DNA_userdef_types.h:211-217` |
| 本 build 打开 Vulkan | `build_linux/CMakeCache.txt:1492` |
| canvas 尺寸/鼠标坐标转换 API | `source/gameengine/Rasterizer/RAS_ICanvas.h:81-82,92` |

---

## 11. 推断清单与待验证

以下结论**不是**直接读到的源码事实，落地前需要实机/基准验证：

1. **性能排序**（3B 优于 3A/1/2）：依据"绘制路径 + 缓存机制"推断，没有基准。需要实测：100/500/1000 个元素、每帧全量更新 vs 静置。
2. **GP 每帧更新几何重建**：GP batch cache dirty 路径未逐行引用；需要用 GP v3 + 修改笔画实测。
3. **GP 无文本能力**：属于"源码检索为空"的证据缺失判断；不排除未来 GP v3 有文本 modifier/节点。
4. **编辑器 Viewport Render 下 UI 是否可见**：`EndFrameViewportRender` 不 swap，显示归编辑器；需要实机在编辑器 Game Engine 视口验证。
5. **渲染到纹理/ImageRender/截图是否包含 UI**：取决于是否经过 `EndFrame`；需要实机验证。
6. **BLF 在 Vulkan 下的全部特性**：BLF 走 GPU batch（`blf_font.cc` 的 batch/glyph cache），逻辑上跨后端；但彩色 emoji、COLR 字体、阴影在 Vulkan 下的表现需要实机验证。
7. **`KX_RuntimeValue` 的实际 sizeof**：`type(1B) + pad + union(16B)`，**实测** `sizeof=24B`、`alignof=8`（已用 `g++ -std=c++17` 编译验证，不再是推断）。
8. **UI 句柄用 `KX_VALUE_INT` 是否够用**：32 位句柄上限 42 亿，足够；但如果未来要跨场景持久句柄，需要带场景 id 的 64 位或字符串键。
9. **字体/图片资源的打包解析**：`VFont.filepath` / `packedfile` 与 `Image` 的解析路径需要实机确认；B4.1 建议先支持"资源已打包 + 按名查找"的 happy path。
10. **`BLF_load_font_stack()` 的调用时机**：建议在 UI 适配器首次创建文本元素时调用；需验证重复调用是否幂等（`blf_font_default.cc:95-100` 看起来会跳过已加载，但未实测）。

---

## 12. 评审决策（已拍板，B4.1 执行口径）

> 本节原为待决问题，现全部定稿；B4.1 实现与验收以下述口径为准。

**D1：单字体 + 对齐/断行/阴影**
Label 只承诺七个文本属性：`font_size`、`color`、`halign`、`valign`、`wrap`、`shadow`、`shadow_offset`。
单字体（不开放字体族切换）。富文本、多字体、描边/渐变等一律留 B5，不在 B4.1 做。

**D2：EndFrame 画一次全屏 HUD**
HUD 在 `EndFrame` 统一绘制一次；分屏 / 每视口 HUD 标记 B5。
接口按 **canvas 句柄** 分发设计：`KX_UIRenderer` 的调用点接收 canvas 句柄，未来改成 per-viewport pass 时只改调用点，不改函数签名。

**D3：Font/Image 用 config 字符串存 datablock 名**
`font` / `image` 属性以 config 字符串保存 datablock 名称，运行时从 `bmain` 按名查找。
不改 `ir_export.py` 的 `_VALUE_TAGS`；找不到时向 stderr 输出明确诊断并且不崩溃。
外部路径 / 相对路径解析留 B5。

**D4：POST_DRAW / BGUI 仅作调试原型通道**
保留 `POST_DRAW`/BGUI 作为调试原型手段；产品路径必须走原生适配器。
`bgui/label.py`、`bgui/image.py` 只作为行为对照，运行时零依赖。

### 12.1 由决策直接导出的工程约束

- 首批 5 节点只覆盖 canvas/label/image/button/layout；文本属性严格限定为 D1 的七个。
- `KX_UIAdapter` 三件套（Engine / Mock / Null）为唯一产品入口；BGUI 不得进入运行时依赖图。
- 字体与图片采用"已打包 + 按名查找"happy path；查找失败 = 诊断 + 跳过该元素，不 fatal。
- 端到端验收中，headless 只验证 Mock 调用契约与命中逻辑；真实 BLF 可见性单独在 player 中确认。

### 12.2 实测证据

- `KX_RuntimeValue` 尺寸（2026-11-04，2026-11-05 复测）：
  探针源码从未入库且已丢失；等价探针就是下面这段（可直接编译，不再依赖临时源码路径）：

  ```cpp
  #include <cstdio>
  #include "KX_RuntimeValue.h"
  static_assert(sizeof(KX_RuntimeValue) == 24, "size");
  static_assert(alignof(KX_RuntimeValue) == 8, "align");
  int main()
  {
    std::printf("sizeof=%zu alignof=%zu\n", sizeof(KX_RuntimeValue), alignof(KX_RuntimeValue));
    return 0;
  }
  ```

  编译命令：`c++ -std=c++17 -I source/gameengine/GameLogic/GameNodes -I source/gameengine/GameLogic -I source/gameengine/Expressions -I source/blender/blenlib <片段>.cpp`
  → `sizeof=24 alignof=8`，2026-11-05 实跑输出 `/tmp/sizo_probe.log`。探针中保留两条 `static_assert`。

### 12.3 第 0 天实测结果（2026-11-04）

**适配器三件套已装配**

- `KX_AdapterSet` 第 9 个成员 `ui`；`KX_ExecutionContext::SetAdapters` 第 9 个参数（默认 `nullptr`），`ctx.UI()` 空实现回退 `KX_NullUIAdapter()`。
- `KX_CreateEngineAdapters` → `KX_CreateEngineUIAdapter(scene)`；`KX_CreateNullAdapters` → `NullUIAdapter`。
- `KX_AdapterSet::BeginFrame`：先 `input->BeginFrame(delta)`，再取 `MousePosition` 与 `MouseButtonActive(0, Hold)`，最后 `ui->BeginFrame(delta, mx, my, down)`。
- 逻辑侧 `KX_AdapterSet::EndFrame` 只结束输入帧；HUD 绘制走独立 `DrawUI()`。

**EndFrame 插入点已固定**

- `KX_KetsjiEngine::EndFrame` 与 `EndFrameViewportRender` 在 `FlushDebugDraw` 之后、`SwapBuffers` 之前调用 `KX_Scene::GraphDrawUI()` → `KX_GraphSceneManager::DrawUI()` → `ui->EndFrame()`。
- 此时 `Render()` 已设置 `GPU_matrix_ortho_set(0, width, 0, height, -100, 100)` 与 scissor，和现有调试文本的 BLF 绘制点完全一致。

**BLF 字体栈**

- `KX_UIAdapterEngine` 在首个 Label（及 Button）创建时调用一次 `BLF_load_font_stack()`，全局 guard 保证进程内只加载一次。
- 探针以 stub 计数观测：第一个 Label 后计数 1；第二个 Label、Button、第二个适配器实例均保持 1。**当日日志 `/tmp/b41_ui_mock.log` 与探针源文件均已丢失，未留存**（2026-11-05 复核确认），因此该结论现在只有代码依据：`g_ui_font_stack_loaded` 守卫在 `KX_UIAdapterEngine.cpp:56-62`，测试钩子见 `KX_UIAdapterEngine.h:90`。
- 底层 `BLF_load` 对同一路径返回已缓存字体（`BLF_api.hh:67-68`），与 guard 双击双保险。

**Font / Image 打包 happy path**

- 字体：`DejaVu Sans Book` 打包后 `packed_file` 约为 0.7 MB 量级（原记录的精确值已失去当日日志支撑，2026-11-05 复核时日志缺失，故降级为量级描述）；保存重载后按名查找命中，`packed_file` 大小不变。
- 图片：PNG 打包后约为 10 KB 量级（同样，原精确值无日志支撑，降级为量级描述）；保存重载后仍在 `bpy.data.images` 且 `packed=True`。
- 按名查找缺失（`NoSuchFont`）返回空：运行时的处理是 stderr 诊断 + 跳过该元素，不崩溃。
- 约束：仓库内置 CJK 字体为 `.woff2`，VFont 不能直接加载；D1 的默认字体路径依赖 BLF 字体栈，用户自带字体仅承诺 ttf/otf。

**编辑器视口可见性（环境限制，未完成目视）**

历史观测（2026-11-04）称本机虚拟 X 显示下嵌入视口与 blenderplayer 只推进 1 帧；**该探针文件 `/tmp/ui_probe_ticks.txt` 已于 2026-11-05 复核确认丢失，这个精确帧数不再有日志支撑**。2026-11-05 的 C.8 复验（`/tmp/c8_gui_fix.log`）用 `.debug_trace.json` 帧计数测得 blenderplayer 在 12 秒内帧号推进到 510–534（日志记录 510 / 531 / 534），原「player 只推进 1 帧」的结论不成立；编辑器内嵌 Game Engine 视口未重测。因此 B4.1 当前口径为：

> **B4.1 只保证独立 player 窗口中的 HUD 可见性；编辑器 Game Engine 视口的可见性留待真机补验，不作为 B4.1 验收门槛。**

代码插入点与 D2 不变；待渲染器落地后，用同一 `GraphDrawUI()` 路径在真机复验一次即可。

**B4.2 sizeof 复核（只读审计）**

四条 grep（`sizeof(KX_RuntimeValue)`、`16.*RuntimeValue`/`RuntimeValue.*16`、`20.*RuntimeValue`/`RuntimeValue.*20`、`memcpy.*runtime_data`/`runtime_data.*memcpy`）结果落在 `/tmp/sizeof_audit.log`（2026-11-05 重新生成；原日志曾丢失）。结论：

- 所有值槽布局都通过 `sizeof(KX_RuntimeValue)` / `alignof(KX_RuntimeValue)` 取尺寸，未发现硬编码 16 或 20；唯一疑似命中是 `KX_FunctionNodesB4.cpp:349` 的 `1920` 字面量（`ConfigInt("design_w", 1920)`），属正则误报。
- 本机重新实测 `sizeof(KX_RuntimeValue)=24`、`alignof=8`，与 12.2 节一致。
- 无需改代码，无 TODO 遗留。



