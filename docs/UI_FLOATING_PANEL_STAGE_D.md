# 阶段 D：锚点体系 / 展开收起动画 / 配色系统

本文记录阶段 D 的落点、状态机、曲线与配色，以及 GUI 实测数据。所有数字来自
`tests/ui_floating_panel_stage_d.py` 的一次完整运行，产物在
`logs/ui_floating_stage_d/`（`stage_d_result.json`、`stage_d_log.txt`、`d_*.png`、
`crops/crop_*.png`）。

## 1. 改动落点与最小侵入

| 文件 | 改动 |
| --- | --- |
| `source/blender/editors/interface/regions/interface_region_floating_panel.cc` | 阶段 D 全部实现（锚点、动画、hover、锁/最小化、独立 alpha、配色） |
| `source/blender/windowmanager/intern/wm_draw.cc` | 阶段 C 已接入的 overlay 钩子，未再改动 |
| `tests/ui_floating_panel_stage_d.py` | 新增验收脚本（GUI + 事件模拟 + 像素/时序测量） |
| `docs/UI_FLOATING_PANEL_STAGE_D.md` | 本文 |

未触碰 `wm_handlers_do_intern`、DNA/RNA、GameNodes `bl_idname`，未动 7 个未跟踪的
`_lib.glsl`，未 commit/push。

## 2. 状态机

- `View::Anchor`（收起）/ `View::Expanded`（展开）。
- `Action::{None, Move, Resize, Slider}`；hover 仅在 `Action::None` 时更新（沿用阶段 B 预留）。
- 锚点点击 → 300 ms rect 插值到 `expanded_rect`；展开态最小化 → 反向插值回锚点。
- 动画期间 `animating == true`：只画玻璃，不画内容，模糊 overlay 也跳过；press 事件被忽略。
- 锁定：press 在面板内被消费，但 `Move/Resize/Slider` 均不启动；锁定瞬间把当前有效 alpha
  冻结进 `alpha_override`，因此全局 `EBC_FP_ALPHA` 也不再影响它。锁定状态下锁定按钮与最小化
  仍然可用（可解锁、可收起）。

关键常量（`interface_region_floating_panel.cc`）：

```
ANCHOR_SIZE             = 44 px
ANCHOR_ALPHA_IDLE       = 0.17
ANCHOR_ALPHA_HOVER      = 0.60
ANCHOR_HOVER_SCALE      = 1.06
ANCHOR_HOVER_SECONDS    = 0.175 s
PANEL_ANIM_SECONDS      = 0.300 s
TITLE_BAR_HEIGHT        = 34 px
BUTTON_SIZE             = 20 px
SLIDER_TRACK_Y          = 27 px
```

动画由 `WM_event_timer_add(..., TIMER, 1/60 s)` 驱动：`floating_panel_anim_tick` 同时推进
rect 插值（`cubic-bezier(0.4, 0, 0.2, 1)`，二分反解 x→t）与 hover ramp（ease-out cubic，175 ms）。
每 tick 向 `EBC_FP_TRACE_FILE` 追加一行，完成时把实测毫秒数写入 `anim_last_ms` /
`hover_last_ms` 并导出到 `EBC_FP_STATE_FILE`。

## 3. 配色系统

| 用途 | 颜色 | 说明 |
| --- | --- | --- |
| 基底（上→下） | `#453D3D → #0B0303` | 12 段色带，自上而下 |
| 信号（左→右） | `#178D8D → #85B2B2` | 仅小面积：hover、选中、滑块已填充段、按钮激活、焦点环、锁定指示 |
| 正文 | `#E8E8E2` | 标题与读数 |
| 次要文字 | `#85B2B2` @ 0.60 | 说明行、`LOCK // ...` |
| 图标（中性） | `#B5AFA5` @ 0.85 | 未激活的锁 / 最小化 / 滑块 knob 外框 |
| 图标（激活） | `#85B2B2` | 悬停或锁定 |
| 描边 | `#85B2B2` @ 0.20–0.55 | 1 px 直角描边；hover 时提高到 0.90 并加 2 px 内圈光晕 |

形态：全直角、1 px 细线分割、读数用等宽字体（`BLF_draw_mono`，字符宽 6.2×scale）。
面板宽高、读数均实时显示，便于逐帧核对。

## 4. 验收数据（一次运行）

运行命令（GUI、事件模拟、无重编译）：

```
STAGE_D_OUT=logs/ui_floating_stage_d \
EBC_FP_STATE_FILE=/tmp/fp_state.json \
EBC_FP_TRACE_FILE=/tmp/fp_trace.txt \
EBC_FP_BLUR=1 \
build_linux/bin/blender --factory-startup --enable-event-simulate \
  --python tests/ui_floating_panel_stage_d.py
```

`verdicts` 全部为 `true`（10/10）。

### 4.1 锚点三态（milestone ①）

像素反解模型 `out = a·glass + (1-a)·backdrop`：`glass` 按行取 12 段色带理论值，`backdrop`
取锚点右侧 4 列视口底色（严格排除 x=1575 的 1 px 视口/编辑器亮边框），只取
`|C-B| > 25`（三通道）的强信号行，idle/hover 各 12 行样本。

| 状态 | 实测 alpha | 判据 | 结果 |
| --- | --- | --- | --- |
| 默认 | 0.1734 | 0.15–0.20 | 通过 |
| 悬停 | 0.6056 | ≈0.60（0.55–0.65） | 通过 |
| 退出悬停 | 0.1734 | 与默认一致 | 通过 |

尺寸：idle↔hover 差分包围盒 `[1525,1571,540,586]` = **47×47 px**；状态矩形从 44×44
变为 47×47；`47/44 = 1.0682`，设计值 1.06（差值来自整数取整 ≤1 px），判据 1.03–1.09 通过。

悬停过渡时长：C++ 实测 `hover_ms = 183.34 ms`（进入与退出同值），18 个上升帧；
判据 150–200 ms 通过。截图 `d_00`（默认）、`d_01`（悬停）、`d_02`（退出）逐张读图确认：
默认态几乎透出网格底纹、仅细青绿描边；悬停态明显变暗、变大并出现码角与光晕。

### 4.2 展开 / 收起动画（milestone ②）

“干净计时”（无截图阻塞主循环）测得：

| 方向 | `anim_ms` | 判据 | 结果 |
| --- | --- | --- | --- |
| 展开 | 302.79 ms | 285–330 ms | 通过 |
| 收起 | 300.02 ms | 285–330 ms | 通过 |

（wall time 322 ms 含 60 Hz tick 对齐与轮询开销；判定采用 C++ 内部时钟。）

中间帧（截图时的真实 rect，`d_10..d_13`）：

| 帧 | rect | 宽×高 | 观感 |
| --- | --- | --- | --- |
| d_10 | `[1520,1575,527,577]` | 56×51 | 仍在锚点附近的小块 |
| d_11 | `[1397,1707,248,431]` | 311×184 | 中段，玻璃渐变已成形、无内容 |
| d_12 | `[1352,1755,146,377]` | 404×232 | 接近终态 |
| d_13 | `[1344,1763,129,368]` | 420×240 | 终态，内容出现 |

收起中间帧：`d_34` `[1502,1889,153,375]`（388×223）、`d_35` `[1519,1667,423,520]`（149×98）。
逐张读图确认矩形连续缩放、动画期间只显示玻璃、结束后内容才出现。

`EBC_FP_TRACE_FILE` 的逐帧宽度序列也显示 bezier 缓动（如 19.5 ms→48 px、52.8→81、
86.1→168、119.5→273、152.8→340、202.8→393、286.1→420）：两端慢、中段快。

### 4.3 独立透明度滑块与锁定（milestone ③）

两点经验标定（alpha=0 帧 = 背景，alpha=1 帧 = 玻璃本色），在面板内部
`y∈[rect.ymin+62, +120]`、`x∈[rect.xmin+20, +300]` 的条带上反解透射系数：

| 场景 | C++ alpha | 像素反解 alpha | 透射率 1-a |
| --- | --- | --- | --- |
| 滑块拉到 82% | 0.8282 | 0.8261 | 0.1739 |
| 全局 env 降到 0.05（滑块仍 82%） | 0.8282 | 0.8261 | 0.1739 |
| 滑块拉到 18% | 0.2194 | 0.2222 | 0.7778 |

全局 0.05 与滑块 82% 两帧逐像素 `max_abs = 0.0`、`changed_px = 0`，证明滑块优先级高于
`EBC_FP_ALPHA`。逐张读图：`d_22`（82%，下层属性文字不可见）与 `d_24`（21%，下层文字明显
透出）对照成立；`d_23` 与 `d_22` 观感一致；`d_25` 为 100% 不透明。

锁定（先把滑块拉到 100%，保证逐像素可比）：

| 操作 | 状态 rect | 状态 alpha | 像素比较 |
| --- | --- | --- | --- |
| 锁定后静止（对照） | `[1500,1919,117,356]` | 1.0 | `max_abs 0.0`，`changed_px 0 / 98176` |
| 锁定后拖拽 48×18 px | 不变 | 不变 | `max_abs 0.0`，`changed_px 0 / 98176` |
| 锁定后点滑块末端 | 不变 | 不变 | `max_abs 0.0`，`changed_px 0 / 98176` |

逐张读图 `d_30..d_33`：四张在视觉上完全一致，`LOCK // ENGAGED` 常驻。

悬停门控：`action == None` 才更新 hover。`d_26`（鼠标在面板外）→ `d_27`（悬停锁按钮）图标
区 108/400 像素变化、`max_abs 61`（中性暖灰→青绿）；拖拽经过锁按钮时状态导出为
`action=1, hover=0`，`d_28` 中锁/最小化图标保持中性。收起回锚点：`rect` 与初始完全相同，
锚点区逐像素 `max_abs 0.0`（`d_40` 与 `d_00` 一致）。

## 5. 已知限制

- 内容层（文字、按钮、滑块）不随有效 alpha 淡出：滑块本身可调范围是 0.05–1.0，因此 UI 上
  不会出现 alpha=0；而 `EBC_FP_ALPHA=0.0`（仅用于标定）会保留文字/控件。这样在极低
  不透明度下仍可操作面板，属于有意选择，已在代码注释与本文记录。
- 动画时长按 C++ 内部时钟判定；截图/重绘会推迟 60 Hz tick，因此视觉中间帧的
  “计划偏移”与真实 `anim_ms` 不完全对应（表 4.2 已给出截图时的真实 rect）。
- 悬停放大为整数像素取整：44→47 px，实测比值 1.0682，与设计 1.06 差 ≤1 px。
