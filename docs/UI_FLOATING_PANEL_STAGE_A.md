# 终末地风格沉浸式悬浮 UI — 阶段 A（静态浮动 Region）

本轮范围：只做阶段 A。未做事件、拖拽、模糊、配色系统、节点编辑器宿主。
结论先行：**一个屏幕级（Screen-level）浮动 Region 已创建并可持久绘制**，`winrct` 完全脱离 `ScrArea`
约束；跨区域边界不裁剪；切换工作区后仍在。默认不参与事件上下文，因此输入无条件透传给下方编辑器。

---

## 1. 源码勘察结论（里程碑 ①）

### 1.1 `HudRegionData` 能否承载扩展字段：**能**

- `HudRegionData` 定义于 `source/blender/editors/interface/regions/interface_region_hud.cc:95`，当前只有
  `short regionid` + `int region_index_hint` 两个字段。
- `ARegion::regiondata` 是 `void *`，由 `hud_region_add()`（同文件 `:324`）分配、
  `hud_region_free()`（`:219`）释放。给它追加字段（位置、尺寸、透明度、锁定、独立配色偏移等）
  只影响这个私有结构体，**不需要改 DNA、不需要改 RNA、不改变 .blend 读写**，风险最低。
- 但字段够放 ≠ 落点够用：HUD Region 目前是 **ScrArea 内的一等公民**，不是屏幕级公民。

### 1.2 为什么不直接把阶段 A 挂在现有 `RGN_ALIGN_FLOAT` 的 HUD Region 上

关键约束在 `source/blender/editors/screen/area.cc` 的 `region_rect_recursive()`：

- `:1785`–`:1798` 对 `RGN_ALIGN_FLOAT` 计算 `overlap_remainder_margin`，并在 `:1798`
  用 `BLI_rcti_isect(&region->winrct, &overlap_remainder_margin, &region->winrct)` 把
  `winrct` **硬钳制在所属 ScrArea 的矩形内**。因此面积级 HUD 永远无法跨 ScrArea。
- 面积级 Region 按面积顺序绘制与混合（`wm_draw_region_blend`，`source/blender/windowmanager/intern/wm_draw.cc:1221`），
  即使强行把 `winrct` 改到面积外，后绘制的面积也会覆盖它。
- 面积级 Region 还会随 Area/编辑器类型切换被重建或清理，并写入 .blend 的 Area regionbase。

结论：**面积级 HUD 不满足「跨 ScrArea 不裁剪 + 视口之上 + 切工作区仍在」**。

### 1.3 选定落点：屏幕级 Region（`bScreen::regionbase`）

`bScreen::regionbase` 就是菜单/Popup 使用的“屏幕级 Region”容器，DNA 注释明确为 runtime only：

- 绘制：`wm_draw_window_offscreen()` 末尾单独循环所有屏幕级 Region
  （`wm_draw.cc:1116`–`:1143`），随后 `wm_draw_window_onscreen()` 在全部 Area 之后混合
  （`:1241`），因此**天然在所有 Area 之上**，且只受自身 `winrct` 裁剪。
- 事件：事件路由只命中 Area 内 Region（`wm_event_system.cc` 的 `region_event_inside` 只遍历
  area regionbase），屏幕级 Region 没有 handler 时**不会成为事件上下文**，键盘/鼠标事件
  继续落到下方 Area Region，满足阶段 A「只绘制不响应」。
- 生命周期：runtime only，不写入 .blend；屏幕切换时随 `ED_screen_exit()` 走 `ED_region_exit()`
  （`source/blender/editors/screen/screen_edit.cc:962`），把 `runtime->visible = false` 并释放
  绘制缓冲（同文件 `:911`、`:909` 的 `WM_draw_region_free`）。**这是本轮实际踩到的坑，见 §4.1**。

### 1.4 是否需要新增 `region_type`

阶段 A **不需要**新增 `eRegion_Type`：直接复用 `RGN_TYPE_HUD`（RNA 显示名为 “Floating Region”），
不触碰 DNA 枚举、`RGN_TYPE_NUM` 及各 `[RGN_TYPE_NUM]` 数组，也不触碰 RNA 注册。

是否在后续阶段升级为独立 `RGN_TYPE_FLOATING_PANEL`，判据如下（当前不需要）：
- 需要独立主题/光标/快捷键表，或需要与 redo HUD 的语义彻底分开时；
- 需要 `rna_screen.cc` 暴露专属类型时（那会触碰 RNA 注册，应单独评审）。

### 1.5 阶段 A 最小改动清单（最终落地）

| # | 文件 | 改动 |
|---|---|---|
| 1 | `source/blender/editors/interface/regions/interface_region_floating_panel.cc`（新增） | 静态 ARegionType + 纯色矩形绘制 + 硬编码 placement + ensure 逻辑 |
| 2 | `source/blender/editors/interface/CMakeLists.txt` | 增加一行源文件 |
| 3 | `source/blender/editors/include/ED_screen.hh` | 在 `blender::ui` 内新增一个函数声明 |
| 4 | `source/blender/windowmanager/intern/wm_draw.cc:1271` 起 `wm_draw_window()` | 取到 `screen` 后调用一次 `ui::ED_screen_floating_panel_ensure(win, screen)`（`:1281`） |
| 5 | `tests/ui_floating_panel_stage_a.py`（新增，验证脚本） | 布局/工作区切换/截图，不修改源码 |

无 DNA 结构变更、无 RNA 定义/属性/路径变更、无 `bScreen`/`ScrArea` 结构变更、无
`wm_handlers_do_intern` 主循环变更。

### 1.6 阶段 A 实现要点

- 新建 Region：`BKE_area_region_new()` + `BLI_addtail(&screen->regionbase, region)`，
  `regiontype = RGN_TYPE_HUD`，`alignment = RGN_ALIGN_FLOAT`，
  `runtime->type` 指向本文件内的静态 `ARegionType`（`draw` 回调唯一）。
- 不注册任何 handler / keymap / gizmo；不进入事件上下文。
- 绘制：`wmOrtho2_region_pixelspace()` 后以 immediate quad 填充整块 Region；
  颜色先用占位墨色 `#453D3D`（alpha 0.92），只是阶段 A 占位，不代表配色系统。
- Placement 以**窗口原生像素**为单位（与 `wmViewport()`/`winrct` 坐标一致）：
  420×240，锚点 `x = 0.70 * window_width`、`y = 0.12 * window_height`，
  默认布局下恰好横跨 `VIEW_3D | PROPERTIES`，用于证明跨 Area 不裁剪。
- `wm_draw_window()` 每次重绘 ensure 一次：缺则创建，有则重新 placement 并
  `ED_region_floating_init()`（恢复 `visible` / rect，见 §4.1）。

---

## 2. 编译结果（里程碑 ②）

- 命令（带排队锁，未 kill 任何进程）：
  `flock /tmp/build_linux.lock ninja -C build_linux -j6 blender`
- 目标：`blender`（`build_linux/bin/blender`）
- 最终两次构建 exit code：`0`
  - 日志：`logs/UI-FLOATING-A-build-20261108-161015.log`（exit 0）
  - 日志：`logs/UI-FLOATING-A-build-20261108-161737.log`（exit 0，修复后）
- 日志中 `error:` 计数：0；新增文件仅出现仓库既有的
  `DNA_DEPRECATED` 告警噪声（`DNA_space_types.h:934`），与本改动无关。

---

## 3. 运行时证据（里程碑 ③）

运行方式（GUI，非 background）：

```
STAGE_A_OUT=<abs out> build_linux/bin/blender --factory-startup \
    --python tests/ui_floating_panel_stage_a.py
```

本次运行：`RUN_EXIT=0`，窗口 1920×1080，日志
`logs/UI-FLOATING-A-runtime-20261108-161737.log`，截图目录
`logs/UI-FLOATING-A-run-20261108-161737/`。

脚本实测布局（窗口坐标，y 自下而上）：

```
expected panel rect x=1344..1763 y=129..368
VIEW_3D     x=2    y=98   w=1574 h=931  intersect=(232x240) OVERLAP
PROPERTIES  x=1579 y=23   w=339  h=819  intersect=(185x240) OVERLAP
```

即矩形左 232px 在 3D 视口内、右 185px 在属性编辑器内，跨越 `x=1575/1579` 的面积边界，
且在视口之上（屏幕级 Region 在全部 Area 之后混合）。

截图（已逐张读图确认）：

| 文件 | 现象 |
|---|---|
| `stage_a_layout.png` | Layout 工作区：矩形位于视口右下、横跨视口↔属性边界，边界分割线被矩形覆盖，无裁剪；启动 splash 菜单因其后创建而压在矩形之上（菜单应在上层，符合预期） |
| `stage_a_workspace_modeling.png` | 切到 Modeling 工作区：同一位置、同一尺寸的矩形仍在，仍跨边界，仍不裁剪 |
| `stage_a_back_layout.png` | 切回 Layout：矩形重新出现，位置尺寸一致 |

补充：`bpy.ops.screen.screenshot()` 会先 `WM_redraw_windows()`（`screendump.cc:68`）再读像素，
因此截图走的是与正常 GUI 完全相同的 `wm_draw_window()` 绘制路径，不是特判出的假象。

---

## 4. 实现记录与已知边界

### 4.1 切回工作区后矩形消失（已修复）

- 现象：Layout → Modeling 有矩形；Modeling → Layout 无矩形（首次两轮验证均复现，
  `stage_a_back_layout.png` 与上一轮字节数完全相同）。
- 根因：`ED_screen_exit()` 对**所有屏幕级 Region**（含本面板）执行 `ED_region_exit()`
  （`screen_edit.cc:962`），其中 `region->runtime->visible = false`
  （`screen_edit.cc:911`）。切回时 ensure 只找到旧 Region 但未重新初始化，
  `wm_draw_window_offscreen()` 在 `:1118` 直接 `continue` 跳过不可见 Region。
- 修复：ensure 的“已存在”分支改为重新 placement + `ED_region_floating_init()`
  （该函数会重新执行 `region_evaluate_visibility()` 与 `region_update_rect()`，
  `area.cc:2533`）。修复后 `stage_a_back_layout.png` 正常显示。
- 结论：屏幕级 Region 是 runtime-only 的，**跨屏幕切换必须每次重绘做 ensure + 重新 init**，
  这是后续阶段的固定约束，不是一次性 hack。

### 4.2 阶段 A 明确不做

- 不处理任何事件（无 hover/拖拽/缩放/锁定/吸附/模态降级）；鼠标与键盘行为与改动前完全一致。
- 不做背景模糊与玻璃层，不做配色系统、不做锚点/动画。
- 不做持久化：面板位置尺寸硬编码；不写 .blend、不写 UserDef、不写 JSON。
- 不做节点编辑器宿主与参数折叠。
- 不新增 region_type / RNA 项。

### 4.3 层叠顺序

屏幕级 Region 按 `bScreen::regionbase` 顺序绘制。本面板在首次重绘时创建，菜单/Popup 之后创建，
因此菜单/Popup 在本面板之上（启动 splash 截图即为此现象）。阶段 B 若需要「面板压过菜单」，
需要显式的 z-order 规则，不能在阶段 A 偷改绘制顺序。

---

## 5. 风险与维护成本

### 5.1 上游 5.4/6.0 合并冲突面

| 改动点 | 冲突面 | 说明 |
|---|---|---|
| 新文件 `interface_region_floating_panel.cc` | 无 | 上游几乎不可能同时对同一新文件改动，合并冲突概率最低 |
| `editors/interface/CMakeLists.txt` 一行 | 低 | 与上游新增文件列表同段，人工合并一次即可 |
| `ED_screen.hh` 新增声明块 | 低 | 追加在 `ui` namespace 末尾，不动既有声明 |
| `wm_draw.cc` 单点 4 行（`:1278`–`:1281`） | 中 | 该函数是绘制主路径，上游经常改动；冲突时保留「取 screen 后即 ensure」的位置语义即可 |
| DNA / RNA | **零** | 不改结构、不注册类型、不改数据路径；旧 .blend 读写不受影响 |

### 5.2 维护成本与债务

1. **复用 `RGN_TYPE_HUD` 的语义债**：屏幕级 HUD 与面积级 redo HUD 共用同一个 `regionid`。
   当前无代码路径依赖“HUD 必属 Area”（`ED_area_type_hud_clear` 只遍历 area regionbase），
   可接受；若后续阶段需要独立快捷键/主题/光标，应追加 `RGN_TYPE_FLOATING_PANEL`（枚举尾部追加，
   并评审 RNA 枚举项的变更），不要继续复用。
2. **每次重绘都要 placement + init**：成本可忽略（一次 rect 计算），但含义是“屏幕切换会清空
   runtime 状态”。后续持久化状态（位置、尺寸、透明度、锁定）必须存在 `bScreen` 之外的稳定容器，
   否则每次切工作区都会丢；且恢复逻辑要能区分“同一屏幕 resize”和“新屏幕重建”。
3. **非交互性依赖事件路由现状**：阶段 A 之所以“透传”是因为屏幕级 Region 不在 Area 命中测试里。
   阶段 B 若给该 Region 挂 handler，必须走独立注册路径并显式做「坐标在面板矩形内才消费」，
   不得改 `wm_handlers_do_intern` 主循环。
4. **alpha/颜色空间**：绘制颜色直接写在 GPU 线性空间。阶段 C 配色系统落地时需要统一处理
   sRGB→linear 与全局透明度，不能沿用这里的字面值。
5. **每轮构建都要排队**：本机有并行开发线，构建必须 `flock /tmp/build_linux.lock`；
   本轮两次构建均为 `-j6` 且未 kill 任何进程。

---

## 6. 复现步骤

```
# 1) 构建（排队）
flock /tmp/build_linux.lock ninja -C build_linux -j6 blender

# 2) 运行时验证（GUI 可用时）
mkdir -p logs/UI-FLOATING-A-run-$(date +%Y%m%d-%H%M%S)
STAGE_A_OUT=$PWD/logs/UI-FLOATING-A-run-... \
  build_linux/bin/blender --factory-startup --python tests/ui_floating_panel_stage_a.py
```

脚本会打印窗口/Area 布局与矩形命中关系，并输出三张截图；任一步失败都会在退出日志中可见。
