# 浮动 UI 面板 — 阶段 B（拖拽 / 缩放 / 事件透传）

阶段 B 的唯一真相源。承接 `docs/UI_FLOATING_PANEL_STAGE_A.md`。
工程：`/home/eridanusepsilon/Code/EBC/EBC5.3`，Blender 5.3 源码树，`build_linux`。

## 1. 起点修正

阶段 A 的勘察结论：屏幕级 Region（`bScreen::regionbase`）**不进入事件上下文**，
`wm_event_do_handlers_area_regions` 只遍历 `ScrArea::regionbase`，因此"透传"本来就是默认行为，
不需要防护。阶段 B 的问题反过来：如何在面板矩形内**精确、主动**接管输入，同时不惊动全局事件链。

## 2. 设计落点

| 内容 | 位置 |
| --- | --- |
| 单点 ensure 钩子 | `source/blender/windowmanager/intern/wm_draw.cc:1281`（`wm_draw_window()` 内，取到 active screen 之后） |
| 面板创建 / 定位 / 注册 handler | `source/blender/editors/interface/regions/interface_region_floating_panel.cc:405` `ED_screen_floating_panel_ensure()` |
| handler 注册调用 | 同文件 `:439` `WM_event_add_ui_handler(nullptr, &win->runtime->modalhandlers, ...)` |
| 事件接管回调 | 同文件 `:307` `floating_panel_event_handler()` |
| 交互状态机 | 同文件 `:93` `struct FloatingPanelData` |
| 拖拽 / 缩放矩形计算 | 同文件 `:234` `floating_panel_apply_interaction()` |
| 右下角手柄命中区 | 同文件 `:114` `floating_panel_handle_rect()` |
| 绘制 | 同文件 `:124` `floating_panel_region_draw()` |
| 公开声明 | `source/blender/editors/include/ED_screen.hh:846` |
| 构建登记 | `source/blender/editors/interface/CMakeLists.txt:51` |

### 为什么不会引入全局副作用

1. **不碰 `wm_handlers_do_intern` 主循环**，不注册 keymap，不设 active region，不改 DNA/RNA。
2. handler 注册在 `win->runtime->modalhandlers`。`wm_event_do_handlers` 处理顺序是
   `modalhandlers`（`wm_event_system.cc:4358`）→ 面积内 region handlers（`:4413`）→ 窗口 handlers
   （`:4443`）。面板 handler 只在这一段里"插队"，且：
   - 矩形内：`LEFTMOUSE` press/release、拖拽中的 `MOUSEMOVE` 返回 `WM_UI_HANDLER_BREAK`；
   - 矩形外：一律 `WM_UI_HANDLER_CONTINUE`，事件继续走原有链路，行为与未打补丁时一致。
3. `WM_event_add_ui_handler` 内部 `BLI_addhead`（`wm_event_system.cc:5223`），**后加入者优先**。
   菜单/Popup（`popup_block_create` → `region_handlers_add` / modal popup handler）和
   modal operator（`wm_handler_operator_insert`，`wm_event_system.cc:2621`）都在面板之后加入，
   因此它们永远先于面板拿到输入。面板不会抢走菜单与模态操作的按键。
4. handler 以 `nullptr` context 注册：不设置/清除 `bContext::wm::area` 或 region，
   因此不会污染操作器的上下文。
5. 屏幕级 Region 不出现在 `ScrArea::regionbase`，`ED_screen_set_active_region`（`:4385`）
   不会把它设为 active region。

## 3. 状态机

```cpp
enum class Action : uint8_t { None, Move, Resize };
int2 grab_mouse;      // 按下时的鼠标位置（窗口原生像素）
rcti grab_rect;       // 按下时的面板矩形，所有增量都相对它计算
const bScreen *grab_screen;  // 仅比较，不解引用
```

转移：

- `None` + `LEFTMOUSE` press 且点在 `region->winrct` 内 → 命中手柄则 `Resize`，否则 `Move`。
- `Move`/`Resize` + `ISMOUSE_MOTION` → 应用增量，`BREAK`。
- `Move`/`Resize` + `LEFTMOUSE` release → 回 `None`，`BREAK`（同时吞掉 release，避免下方编辑器收到不配对的释放）。
- `WINDEACTIVATE`、拖拽中切换 screen、出现 `WM_HANDLER_TYPE_OP` 模态操作器 → 取消回 `None`。
- **拖拽/缩放进行中不运行任何 hover 逻辑**：motion 在 `action != None` 时直接走交互分支并在
  `BREAK` 前返回；hover 字段预留在同一结构里，后续实现必须以 `action == None` 为前置条件。

拖拽区域：本轮没有控件，**整个面板矩形（除右下角手柄外）都是可拖拽的"非控件空白区"**，
等价于把整块面板当作标题栏。手柄命中区是右下角 `14×14`（窗口原生像素）。

## 4. 边界策略（明确写清）

- **允许跨 `ScrArea`**：面板是屏幕级 Region，不受 `ScrArea` 裁剪。拖过 3D 视图 / 属性区、
  节点编辑器 / 图片编辑器之间的分界都会被接受，且整体绘制在上层。
- **钳进窗口**：`Move` 与 `Resize` 最终都把矩形钳制在 `[0, W-1]×[0, H-1]` 内，
  不允许面板任何部分越出窗口（不做"允许越界"策略）。窗口尺寸 > 面板尺寸时：
  `xmin ∈ [0, W-w]`、`ymin ∈ [0, H-h]`（`w/h` 为含端点的像素宽度）。
- **最小尺寸**：`Resize` 下限 `160×120`（含端点 `xmax >= xmin+159`、`ymin <= ymax-119`）。
- 窗口缩放时只做"钳制"，不重新按比例摆放，避免把用户拖到的位置打回默认锚点
  （`floating_panel_region_clamp()`，`:210`）。

## 5. 编译

```bash
flock -w 900 /tmp/build_linux.lock ninja -C build_linux -j6 source/blender/editors/interface/all
flock -w 900 /tmp/build_linux.lock ninja -C build_linux -j6 blender
```

- 两条命令 exit 码均为 0；`error:` 计数 0。
- 产物 `build_linux/bin/blender` 233,931,456 字节（2026-11-08 16:33 更新）。
- 仅有仓库既有的 `DNA_DEPRECATED` 告警噪声（`DNA_space_types.h:934`）。

## 6. 验收证据

复现：

```bash
STAGE_B_OUT=$PWD/logs/ui_floating_stage_b build_linux/bin/blender \
  --factory-startup --enable-event-simulate \
  --python tests/ui_floating_panel_stage_b.py
```

方法：所有结论都来自截图前后帧的逐像素差分（`numpy`，Blender 内置），不依赖 API 返回值。
面板是拖拽期间唯一的画面变化源，所以"变化像素的包围盒 == 期望旧/新矩形并集"且"并集外变化像素 == 0"
即可证明位置、尺寸、无残影。另外对每个位置做面板内部颜色统计（均值/标准差）与手柄颜色检测，
确认那确实是面板本体。

环境：窗口 1920×1080；`VIEW_3D x=2..1575 y=98..1028`；`PROPERTIES x=1579..1917`；
`NODE_EDITOR x=294..1575 y=23..523`。截图与 `stage_b_result.json` 在
`logs/ui_floating_stage_b/`。

### 6.1 物体模式：拖面板不影响视口

- 拖拽点 `(1464, 350)`（标题栏带），40 个 `MOUSEMOVE`、步长 50 ms，总位移 `(-200,-80)`，共约 2 s。
- 视口矩阵（`area.spaces.active.region_3d.view_matrix`）逐元素完全相同：

  ```
  before = after =
  [0.40403, 0.914746, -0.0, 0.050462,
   -0.407774, 0.180108, 0.895143, -0.000748,
   0.818828, -0.361665, 0.445779, -18.388048,
   0.0, 0.0, 0.0, 1.0]
  VIEW_MATRIX_IDENTICAL=True
  ```

- 面板从 `x=1344..1763 y=129..368`（420×240）到 `x=1144..1563 y=49..288`。
  差分包围盒 `x=1144..1763 y=49..368`（620×320）与期望并集**完全一致**；
  并集外变化像素 **0**。旧位置被背景完整恢复（无残影）。
- 截图：`b_00_layout_idle.png`（拖前）、`b_01_drag_2s.png`（拖后，即 2 秒连续拖拽结束帧）。
- 面板本体：`body_mean=[0.2707,0.2389,0.2389]`、`std=0.0156`；
  右下角手柄 `handle_mean=[0.4039,0.4863,0.4863]`（青绿，`g-r` 明显为正）。

### 6.2 节点编辑器：矩形外框选正常，矩形内被面板消费

Shading 工作区（`NODE_EDITOR` 内 2 个节点：Principled BSDF、Material Output）。

- **矩形外**：按下 `(314, 43)` → 拖到 `(1300, 503)`（完全在 `x<1344` 的面板左侧），
  中点截图可见虚线框选矩形；释放后 **2 个节点全部被选中**（`selected=2`），说明区域正常收到输入。
- **矩形内**：先反选并把该帧作为基线（避免选中高亮变化混入差分），再在 `(1444, 350)`
  按下并拖 `(+120,-40)`。结果：
  - 面板到 `x=1464..1883 y=89..328`；差分包围盒 `x=1344..1883 y=89..368`（540×280）
    与期望并集完全一致，并集外变化像素 **0**；
  - **0 个节点被选中**（`selected=0`），节点编辑器没有出现框选框；
  - Shading 工作区 3D 视口矩阵逐元素完全相同（`SHADING_VIEW_MATRIX_IDENTICAL=True`）。
- 截图：`b_10_nodes_idle.png`（基线）、`b_11_nodes_outside_mid.png`（外部框选中）、
  `b_12_nodes_outside_after.png`（外部释放后 2 节点选中）、`b_13_nodes_inside_before.png`（内部基线）、
  `b_14_nodes_inside_after.png`（内部拖后，面板已右移、无框选）。
- 背景噪声对照：同工作区两张空闲帧相隔 0.6 s，节点区域仅 6013 像素变化（节点预览缩略图渐进刷新）；
  内部拖拽差分在排除 3D 视口 / 图像编辑器 / File Browser / 状态栏后并集外为 0。
- 坐标说明：面板初始 `x=1344..1763`，节点区域右边界 `1575`，因此面板同时覆盖
  `NODE_EDITOR` 右侧 231 px 和 `PROPERTIES` 左侧 185 px；内部按下点 `1444` 落在节点区域内的面板矩形里，
  事件被面板消费，节点编辑器收不到 press。

### 6.3 跨 ScrArea 与窗口边缘

- 缩到最小：`160×120`（`resize_min: min size enforced = True`），差分包围盒与并集一致、并集外 0。
- 跨区 + 右边缘：面板移到 `x=1760..1919 y=169..288`，即 `xmax=1919=W-1`，
  整块落在 `PROPERTIES`（`x>=1579`）之上，未被裁剪。差分包围盒 `x=1144..1919`（776×120）与并集一致，
  并集外变化像素 **0**。截图 `b_04_edge_right.png`。
- 上边缘：继续上拖到 `y=960..1079`，`ymax=1079=H-1`。差分包围盒 `y=169..1079`（160×911）
  与并集一致，并集外 0。截图 `b_05_edge_top.png`。
- 结论：**跨区允许，越出窗口不允许（钳进窗口）**。

### 6.4 连续拖拽 2 秒无丢帧 / 无残影

- 6.1 的 2 s / 40 步是连续拖拽；结束帧 `b_01_drag_2s.png` 中面板尺寸仍精确为 420×240，
  位置精确等于最后一步鼠标增量对应的矩形（`1144..1563 y=49..288`）。
- 差分显示的旧矩形区域被背景完整恢复，并集外 0 像素 → 无拖影、无残留帧。
- 帧间一致性由每步 50 ms 的 timer 驱动，模拟事件按序处理；最终帧与期望值逐像素吻合。

### 6.5 截图复核

已读取并人工确认：`b_01_drag_2s.png`（3D 视口内单一浮动面板、右下青绿手柄、视口未旋转、
无 splash、无残影）、`b_04_edge_right.png`（面板钳在窗口最右、完整覆盖在属性区之上、未被裁剪）、
`b_10_nodes_idle.png` / `b_11_nodes_outside_mid.png` / `b_14_nodes_inside_after.png`
（节点编辑器中框选只在矩形外出现；矩形内拖拽后无框选、节点无高亮）。

## 7. 限制与后续

- 只接管左键（press/motion/release）。中键、右键、滚轮、键盘本轮全部 `CONTINUE`。
- 出现任意 `wmEventHandler_Op` 模态操作器时，面板降级为"只绘制"并取消进行中的交互
  （`wm_window_has_modal_operator()`，`:278`）。这是有意的：模态优先。
- 拖拽中切换 screen（工作区切换）会静默取消；`WINDEACTIVATE` 同样取消。
- 窗口尺寸变化只钳制、不按比例重排。
- 真实鼠标拖出窗口再松开时，release 可能不在本窗口内；`WINDEACTIVATE` 只能覆盖窗口失活路径，
  跨窗口拖拽尚未处理（本轮不扩）。
- hover 尚未实现；状态机已预留，后续实现必须以 `action == None` 为前提。
- 多面板、锁定、最小化、独立配色/透明度、模糊底、排序置顶均未涉及。

## 8. 上游合并风险

- 改动文件：新增 `interface_region_floating_panel.cc`（1 个 CMake 行）+ `ED_screen.hh` 声明 1 行
  + `wm_draw.cc` 调用 3 行（含注释）+ `tests/ui_floating_panel_stage_b.py`。
- 不涉及 DNA/RNA、不改 `wm_handlers_do_intern`、不改 GameNodes 的 `bl_idname`。
- 主要冲突面：`wm_draw_window()` 内的 1 处钩子；若上游把屏幕级 Region 的绘制路径重构
  （`wm_draw_window_offscreen` / `wm_draw_window_onscreen`），需重新核对 composite 顺序。
- `wm_event_system.hh` 为 windowmanager 内部头文件，本文件只读 `modalhandlers` 列表，
  若该结构在 5.4/6.0 重命名需同步修改 `wm_window_has_modal_operator()` / `floating_panel_handler_exists()`。
