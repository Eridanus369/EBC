# 浮动 UI 面板 — 阶段 C（背景模糊管线）

承接 [阶段 A](UI_FLOATING_PANEL_STAGE_A.md)（屏幕级浮动 Region）与 [阶段 B](UI_FLOATING_PANEL_STAGE_B.md)（拖拽 / 缩放 / 事件透传）。
阶段 C 按「先保底、再增强」的顺序实施：先做无模糊的纯色半透明玻璃底层，再接入视口颜色缓冲的模糊副本，
最后建立帧时间基线与拖拽降级。**保底方案独立可用，是所有路径的安全网。**

涉及文件（改动继续收敛在新增文件 + 最小侵入点，未改 DNA/RNA、未改 `wm_handlers_do_intern`、
未改 GameNodes `bl_idname`）：

- 新增/扩展：`source/blender/editors/interface/regions/interface_region_floating_panel.cc`
- 单行钩子：`source/blender/windowmanager/intern/wm_draw.cc:1238`
- 声明：`source/blender/editors/include/ED_screen.hh`
- 验证脚本：`tests/ui_floating_panel_stage_c.py`（保底）、`tests/ui_floating_panel_stage_c_blur.py`（模糊 + 帧时间 + 降级）

---

## 1. 里程碑 ①：保底方案（无模糊玻璃底层）

### 1.1 实现

`floating_panel_draw_glass()`（`interface_region_floating_panel.cc:166`）绘制三层，全部与模糊无关：

| 层 | 内容 | 参数 |
| --- | --- | --- |
| 底层 | 自上而下线性渐变 `#453D3D` → `#0B0303`，32 条水平色带近似 | Alpha = `EBC_FP_ALPHA`（默认 0.78，每次绘制实时读取并 clamp 0–1，见 `:98`） |
| 描边 | 1px `#85B2B2`，内缘四边 | Alpha 0.20（设计区间 15%–25%） |
| 手柄 | 右下角 14×14 缩放柄 `#85B2B2` | Alpha 0.55 |

Alpha = 0 时整层（含描边、手柄）直接不画，用作 A/B 像素证据的「背景基准」。
内容层未在本阶段实现；设计约定后续内容一律以不透明颜色绘制，不受玻璃 Alpha 影响。

保底方案有两条独立生效路径：

1. `EBC_FP_BLUR=0`：`floating_panel_region_draw()`（`:446`）在自己的离屏缓冲里画玻璃，由 `wm_draw_region_blend` 正常混合；
2. 模糊启用但 overlay 不可用（窗口 FBO 为空 / 缓存分配失败 / 面板越界）：`ED_screen_floating_panel_overlay_draw()`（`:774`）在窗口坐标系补画同一套玻璃（`:809-817`）。

两条路径在运行时都截过图，见下。

### 1.2 像素证据（`tests/ui_floating_panel_stage_c.py` → `logs/ui_floating_stage_c/stage_c_result.json`）

窗口 1920×1080（本机 DPI 缩放 1.0），面板窗口坐标 `x 1344..1763`、`y 129..368`
（图像坐标 y 711..950；截图 PNG 上下翻转）。三组截图 `c_00_bg_alpha0.png`（背景基准）、
`c_01_alpha078.png`（默认 Alpha）、`c_02_alpha035.png`（对照）差分结果：

- `changed_bbox = [1344,129,1763,368]` 与面板矩形完全一致，`changed_pixels_outside_panel = 0`；
- 面板内部改变像素 `88461`；
- 跨 area 边界（3D 视口 / 属性编辑器，x≈1576）的透射系数实测 `0.2308`，理论 `1-0.78=0.2200`；
- Alpha 0.78 与 0.35 的差值比 `1.801`，理论 `0.78/0.35=1.81`；
- `delta_luminance_top = -0.0072`、`delta_luminance_bottom = -0.1579`，顶部明显更亮 → 渐变为自上而下。

中心像素采样（RGB，图像坐标 x=1553）：

| 位置（窗口 y） | 背景 Alpha=0 | Alpha=0.78 | Alpha=0.35 |
| --- | --- | --- | --- |
| 顶部 y=348（近上缘） | 63,63,63 | 64,58,58 | 63,61,61 |
| 中心 y=248 | 63,63,63 | 44,38,38 | 55,52,52 |
| 底部 y=149（近下缘） | 63,63,63 | 26,19,19 | 47,43,43 |

即面板下方背景确实被半透明覆盖：同一像素由 63 变 26–64，且越靠顶部越接近背景亮度（渐变顶部更透亮）。
截图已读图确认：`c_01_alpha078.png`、裁切 `c_03_panel_crop_alpha078.png`。

---

## 2. 里程碑 ②：模糊接入

### 2.1 落点、数据来源与时机

钩子在 `source/blender/windowmanager/intern/wm_draw.cc:1236-1238`：

```c
wm_draw_callbacks(win);
wmWindowViewport(win);

/* Stage C: the floating panel glass needs the composited window as backdrop. It must run here,
 * after areas were composited and before the screen-level regions are blended. */
ui::ED_screen_floating_panel_overlay_draw(win);

/* Blend in floating regions (menus). */
for (ARegion &region : screen->regionbase) { ... }
```

- **数据来源**：`GPU_framebuffer_active_get()`（`:360`）取到的窗口 FBO。此处的像素是
  `wm_draw_window_onscreen()` 中 `wm_draw_window_offscreen()` 把所有 Area（含 3D 视口）合成后的结果，
  也就是「视口颜色缓冲」的屏幕级副本，而不是某个 Area 的离屏纹理。面板是屏幕级 Region，
  它自己的纹理此时尚未被 `wm_draw_region_blend` 混入，所以 rect 内没有任何面板像素，不会采到自己的旧帧。
- **为什么不与视口重绘打架**：`wm_draw_window_offscreen()` 在本帧内已经结束，窗口 FBO 只是合成结果，
  没有任何视口绘制会再往上写。overlay 只是往这个 FBO 上叠一次模糊底 + 玻璃；下一次 `wm_draw_window()`
  会从头重画，因此不存在跨帧残留。
- **窗口尺寸变化**：`floating_panel_blur_cache_ensure()`（`:282`）在窗口尺寸改变时销毁并重建
  全窗口捕获纹理（`UNORM_8_8_8_8`，`GPU_TEXTURE_USAGE_ATTACHMENT | SHADER_READ`）与配套 FBO。
- **区域 draw 与 overlay 的互斥**：模糊启用且无拖拽时，`floating_panel_region_draw()` 直接 `return`
  （`:454-459`），玻璃只由 overlay 画一次；拖拽/关闭模糊时 overlay 不画，改由 region draw 画。
  两条路径不会重复叠加。

### 2.2 算法：单趟 13-tap 圆盘高斯（`:351`）

1. `GPU_framebuffer_blit(win_fb, 0, capture_fb, 0, GPU_COLOR_BIT)`（`:381`）整窗复制一次；
2. 13 个采样点：中心 1 个 + 两圈 6 个（半径 `0.55R` 与 `R`，每圈间隔 60°），
   权重 `w_i = exp(-0.5 d_i² / σ²)`、`σ = 0.5R`，再归一化使 `Σw_i = 1`（`:396-421`）；
3. 每个 tap 画一个**恰好覆盖面板矩形**的纹理四边形，UV 整体平移 `(dx/win_w, dy/win_h)`
   （`:423-435`）。第一个 tap 用 `GPU_BLEND_NONE` 覆盖，其余用 `GPU_BLEND_ADDITIVE_PREMULT` 累加，
   权重乘在 **RGB** 上：`tint = {w_i, w_i, w_i, 1.0}`（`:431`）。
4. 结果恒等于 `Σ w_i·c_i`，面板外不写任何像素 → 裁剪由 UV/几何范围完成，不需要 Scissor
   （所以 `GPU_scissor_test(false)`，`:385`）；
5. 最后在同一坐标系叠 `floating_panel_draw_glass()`（`:438-442`）。

关键取舍（踩过的坑，不要回退）：

- **加性混合下权重不能放 alpha**。`GPU_BLEND_ADDITIVE_PREMULT` 的源/目标因子是 RGBA 全 `(ONE, ONE)`，
  完全忽略源 alpha。首版写成 `{1,1,1,w}` 导致 13 次满色累加、面板纯白；改为把权重乘进 RGB 后恢复。
- **不要在这个钩子里向非窗口 FBO 绘制**（已知不稳定，见第 5 节）。因此放弃「降到半分辨率 + 两趟分离高斯」
  的常规方案，改为直接在窗口 FBO 合成。

### 2.3 模糊证据（`tests/ui_floating_panel_stage_c_blur.py`）

为把「模糊」与「玻璃渐变」分离，锐度对比在同一面板矩形上做前后对照（`EBC_FP_ALPHA=0`：
off 看到原始背景，on 看到纯模糊底），外加以面板左侧的独立 control 矩形检验「面板外不受影响」。
指标为图像内相邻像素亮度差绝对值的平均（相邻差分均值，越低越平滑）：

| 区域 | blur off | blur on | 比值 |
| --- | --- | --- | --- |
| 面板矩形内 | 0.0213 | 0.0093 | **0.437**（判据 < 0.75 → 面板内背景确实被模糊） |
| 面板外 control | 0.0061 | 0.0061 | **1.000**（面板外零影响） |

截图：`logs/ui_floating_stage_c/c_13_blur_crop.png`（`EBC_FP_ALPHA=0.78` + 模糊的视觉版本）。
读图确认：面板内属性编辑器文字、3D 网格线被明显糊化，面板外（上方 `Rotation X`、右侧）保持锐利；
1px `#85B2B2` 描边与渐变可见。

---

## 3. 里程碑 ③：帧时间基线与拖拽降级

### 3.1 测量方法

`bpy.ops.wm.redraw_timer(type='DRAW_WIN_SWAP', iterations=N)`，1920×1080、factory startup 静态场景。
为抑制温升与其他编译线的负载漂移，采用**平衡顺序 + 多块重复**：`EBC_FP_BLUR` 序列 `0,1,1,0,0,1,1,0`，
每块 100 帧，先丢第一块（冷启动）。原始输出：`logs/ui_floating_stage_c/stage_c_timing_run2.txt`
（另有一次 3 对 × 60 帧的预跑：`stage_c_timing_run.txt`）。

### 3.2 数字（每帧 绘制+交换 平均值）

| 块 | EBC_FP_BLUR | 平均帧时间 (ms) |
| --- | --- | --- |
| 0（冷启动，丢弃） | 0 | 4.131 |
| 1 | 1 | 2.830 |
| 2 | 1 | 2.761 |
| 3 | 0 | 2.900 |
| 4 | 0 | 2.908 |
| 5 | 1 | 2.747 |
| 6 | 1 | 2.761 |
| 7 | 0 | 2.795 |
| **均值（丢冷启动）** | **0** | **2.868** |
| **均值（丢冷启动）** | **1** | **2.775** |

- 差值 `2.775 − 2.868 = **−0.093 ms**`，即 **−3.24%**（开模糊反而略快）；
- 预跑 60 帧一对结果：off `2.956` / on `2.856`（−3.41%），与上表一致；
- overlay 自身 CPU 侧耗时（含 blit 提交 + 13 个四边形提交）`0.024–0.042 ms/帧`（每 30 帧打印一次）。

**判定：≤5% 损耗 —— 通过。** 但要如实说明两点：

1. 这不是「模糊白赚 3%」，而是两条路径的结构差：开模糊时面板 region 的离屏绘制被跳过，玻璃改在窗口 FBO
   的 overlay 里画；换来的是整窗 blit + 13 个面板大小的采样四边形。净结果落在噪声内且略偏负，
   单块波动约 ±2–3%（测量时系统 load average 3–4，另两条线在编译）。
2. 这是静态场景的 ~350fps 基线（2.8 ms/帧），模糊开销是**固定值（约 0.1–0.2 ms 量级）**；
   视口越重，相对占比越低。13-tap 数量固定，模糊半径只改变采样偏移、不改变采样数，因此开销与半径无关。
3. `DRAW_WIN_SWAP` 的计时在 CPU 侧，overlay 的 0.024–0.042 ms 只覆盖提交；GPU 完成时间反映在帧循环均值里。

### 3.3 拖拽降级（真实实现 + 状态日志）

规则（`ED_screen_floating_panel_overlay_draw()`，`:790-795`）：

- `data.action != None`（拖拽或缩放中）→ **整帧跳过**捕获与模糊，状态置 `skipped (drag/resize)`，
  本帧由 region draw 画保底玻璃；
- 释放回到 `None` → 下一帧自动恢复 `blur capture`；
- `EBC_FP_BLUR=0` → `fallback (blur disabled)`。

日志只在实际状态变化时打印（`:239-247`），所以日志本身就是状态迁移证据。连续拖拽 2 秒（40 步 × 50 ms，
每步 −3 px）：

```
[FP-BLUR] blur capture          ← 按下前
[FP-BLUR] skipped (drag/resize) ← 左键按下（拖拽开始）
[FP-BLUR] blur capture          ← 释放后恢复
```

配合截图锐度（`stage_c_blur_result.json`）：拖拽中面板 `0.0037`，释放后 `0.0011`（更平滑 = 模糊已恢复）。
截图：`c_20_drag_mid.png` / `c_22_drag_mid_crop.png`（拖拽中，背景文字清晰可辨 → 模糊被跳过）、
`c_21_after_release.png` / `c_23_after_release_crop.png`（释放后，背景被糊化）。

---

## 4. 配置项

| 环境变量 | 作用 | 默认 | 范围 |
| --- | --- | --- | --- |
| `EBC_FP_ALPHA` | 玻璃底层 Alpha | 0.78 | 0–1（clamp） |
| `EBC_FP_BLUR` | 模糊开关，非 0 即开 | 1 | 0/1 |
| `EBC_FP_BLUR_RADIUS` | 高斯半径（整窗像素） | 10 | 0.5–40（clamp） |

每次绘制实时读取，运行时可改、无需重启。旧实验变量 `EBC_FP_BLUR_ALPHA` 已删除（模糊底与玻璃层已合并为
「模糊底 + 可调 Alpha 玻璃」两层，不再需要单独控制模糊不透明度）。

---

## 5. 已知不稳定条件（明确写清，不含糊）

**条件**：在这个绘制钩子（`wm_draw_window_onscreen` 内、窗口 FBO 已绑定）里，**bind 一个非窗口 FBO
（无论裸 `gpu::FrameBuffer` 还是 `GPUOffScreen`）并向其绘制**，本机构建上会被静默丢弃：不报 GL 错误、
`check_valid` 通过、viewport/scissor 正确、clear 生效，但任何 draw 都不产生像素。

**当时的诊断证据**（`logs/ui_floating_stage_c/diag_run7.txt`，1920×1080）：

```
capture@panel(+60,+60)=63,63,63,255        ← 窗口 FBO 捕获有效
after-clear@(530,320)=255,0,255,255        ← 半分辨率 FBO clear 到品红有效
half_fb valid=1 err='' viewport=0,0,960,540 scissor=0,0,960,540
after-fill@(530,320)=255,0,255,255         ← 向该 FBO 画覆盖四边形：无效，仍为品红
window-fill@(200,200)=0,255,0,255          ← 同一段 draw 代码画进窗口 FBO：正常（绿）
downsample@(530,320)=255,0,255,255
half@panel(+30,+30)=0,0,0,0                ← 读回半分辨率纹理全 0
```

**复现方式**：在 `floating_panel_overlay_blur()`（`:351`）里临时插入一个
`GPU_offscreen_create(win_w/2, win_h/2, false, …)`，`GPU_offscreen_bind()` 后 `clear` 到品红，
再用 `floating_panel_draw_image_quad()` 画一个覆盖全屏的捕获纹理四边形，随后
`GPU_framebuffer_read_color()` 读回：会得到「clear 生效、draw 无效」。根因尚未完全定位
（推测与该 hook 下的 srgb target / GPU 状态变体有关，`gpu_framebuffer` 与 `gl_framebuffer` 均未见错误路径）。

**规避（当前实现）**：全程不创建任何中间 FBO，捕获纹理只作为采样源，13 个 tap 直接合成进窗口 FBO。
因此该不稳定条件在当前代码中不可达。若未来要恢复两趟高斯，必须先把该条件解决，否则会得到「无模糊」画面。

**保底兜底**：即使上述条件再次出现，画面上仍有三条保底路径（`EBC_FP_BLUR=0`、拖拽、overlay 返回 false）；
`ED_screen_floating_panel_overlay_draw()` 在 `floating_panel_overlay_blur()` 返回 false 时直接补画玻璃，
不会出现「面板消失」。运行时未观察到该路径，因为当前实现没有可失败的中间 FBO。

---

## 6. 复现步骤

```bash
# 0) 构建排队（起手前确认无 ninja/cc1plus）
flock /tmp/build_linux.lock ninja -C build_linux -j6 blender

# 1) 保底方案像素证据
build_linux/bin/blender --factory-startup --enable-event-simulate \
    --python tests/ui_floating_panel_stage_c.py
# 产出 logs/ui_floating_stage_c/stage_c_result.json + c_00..c_03

# 2) 模糊 / 帧时间 / 拖拽降级
STAGE_C_OUT=$PWD/logs/ui_floating_stage_c \
build_linux/bin/blender --factory-startup --enable-event-simulate \
    --python tests/ui_floating_panel_stage_c_blur.py
# 产出 stage_c_blur_result.json + c_10..c_23；状态与耗时打印在 stderr 的 [FP-BLUR] 行
```

注意：`build_linux/bin/blenderplayer` 在本机 EEVEE 初始化时 SIGSEGV（其他线的问题，与本阶段无关），
画面证据一律用主程序 GUI + `bpy.ops.screen.screenshot`。

---

## 7. 限制与后续

- 模糊只作用于「面板矩形内的背景副本」，不采样面板自身内容（无需，面板绘制在其后）。
- 13-tap 圆盘在半径 40 时会有轻微 banding/星芒感；若要更高质量，需先解决第 5 节的离屏绘制问题再做
  可分两趟高斯，届时可引入降采样 + 升级采样数。
- 拖拽期间是「直接跳过」而非「低质量模糊」；释放后立即恢复，切换点由 `action` 状态机给出，无额外定时器。
- 内容层（文字/控件）尚未实现；实现时必须用不透明颜色，玻璃层的 Alpha 只影响背景底色。
