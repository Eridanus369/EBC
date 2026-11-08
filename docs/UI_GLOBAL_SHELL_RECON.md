# G 阶段：全局外壳重构 —— 只读勘察与方案设计

> 勘察对象：`/home/eridanusepsilon/Code/EBC/EBC5.3`
> 本轮性质：**只读**。未改任何源码、未编译、未 commit/push、未触碰 7 个未跟踪的 `_lib.glsl`、未触碰浮动面板线的 `interface_region_floating_panel.cc` / `wm_draw.cc` / `ED_screen.hh`。
> 结论全部基于本树实际代码，行号为勘察时实测。

---

## 0. 一句话结论（先看这里）

| 议题 | 结论 |
|---|---|
| 1. 控件配色能否走主题 | **能，约 85% 能**。本版本的控件色已全部走 `bTheme` + `blender::ui::theme::get_color_*`，不存在 `UI_GetThemeColor*`。竖向双色渐变（含 `#453D3D → #0B0303`）可用 `shaded/shadetop/shadedown` 精确表达；**横向/多段青绿渐变、hover 精确色、`BUT_NODE_ACTIVE`、节点背景 alpha、线宽/模糊**必须落绘制层。 |
| 2. Topbar 呼吸式自动隐藏 | **可行，但「真改 ARegion 高度」是高风险路**。顶栏是 global area，高度由 `cur_fixed_height` 驱动且**每次 `ED_screen_global_areas_refresh` 都会被重置**。低风险落点是：隐藏原生 global topbar，改用 `ED_area_offscreen_create` 造一个离屏 `SPACE_TOPBAR` area，只对这一块逐帧改 vert 高度 —— 3D 布局完全不动。 |
| 3. 3D 全屏 + 折叠其它区域 | **「压到 0 高」不可行**（会被 `screen_geom_vertices_scale_pass` 重新抬高，且 0 高区域仍吃 1px 命中）。**「销毁/重建 ScrArea」不可取**（`ED_area_exit` + region 释放 + 指针悬空）。**推荐直接复用 `SCREENMAXIMIZED` / `SCREENFULL`（`SCREEN_OT_screen_full_area`）**，代价是换 screen 不复制 IDProperty，需镜像面板状态。 |

---

## 1. 全局 UI 控件配色：能否只走主题

### 1.1 颜色取值的真实来源（证据）

**（a）主题颜色唯一入口是 `resources.cc` 的 `namespace theme`**

- `source/blender/editors/interface/resources.cc:40`、`74`：`namespace blender::ui { namespace theme {`
- `resources.cc:1124`：`void theme_set(int spacetype, int regionid)`
- `resources.cc:1128`：`g_theme_state.theme = U.themes.first();`
  - **关键结论**：全局只取 `U.themes.first()`。因此运行时改 `bpy.context.preferences.themes[0]` 的字段，就能在下一帧生效；不存在「按 space 单独选主题」的分支。
- `resources.cc:1244`：`get_color_3fv`；`1252`：`get_color_4fv`；`1430`：`get_color_4ubv`
- `resources.cc:1551-1556`：
  ```cpp
  void frame_buffer_clear(int colorid)
  {
    float col[3];
    get_color_3fv(colorid, col);
    GPU_clear_color(col[0], col[1], col[2], 1.0f);  // alpha 被强制 1.0
  }
  ```
- **重要版本差异**：本树中**不存在** `UI_GetThemeColor*`。所有控件颜色都走 `theme::get_color_3fv / get_color_4fv / get_color_4ubv`。任何按旧版 API 写的方案在本树不成立。

**（b）结构体确实全部主题化**

- `source/blender/makesdna/DNA_theme_types.h:184-196`：`uiWidgetColors` = `outline` / `outline_sel` / `inner` / `inner_sel` / `item` / `text` / `text_sel`（uchar[4]）+ `shaded`(192) + `shadetop`(194) / `shadedown`(194) + `roundness`(195)
- `DNA_theme_types.h:198+`：`uiWidgetStateColors`
- `DNA_theme_types.h:217-245`：`ThemeUI` = `wcol_regular / wcol_tool / wcol_toolbar_item / wcol_text / wcol_radio / wcol_option / wcol_toggle / wcol_num / wcol_numslider / wcol_tab / wcol_curve / wcol_menu / wcol_pulldown / wcol_menu_back / wcol_menu_item / wcol_tooltip / wcol_box / wcol_scroll / wcol_progress / wcol_list_item / wcol_pie_menu` + `wcol_state` + `widget_emboss` + `editor_border/outline` + `transparent_checker_*` + `link` + `icon_alpha/icon_saturation`
- `DNA_theme_types.h:294-300`：`ThemeSpace` = `back` / `back_grad` / `background_type`；`307`：`header`
- `DNA_theme_types.h:504-544`：`bTheme`，其中 `tui`(518)、`space_node`(538)、`space_topbar`(543)

**（c）绘制层确实是「主题色 + 统一亮度偏移」**

- `source/blender/editors/interface/interface_widgets.cc:990-1001`：
  ```cpp
  r_coltop[0] = clamp(color[0] + shadetop, 0, 255);
  r_coltop[1] = clamp(color[1] + shadetop, 0, 255);
  r_coltop[2] = clamp(color[2] + shadetop, 0, 255);
  r_coltop[3] = color[3];
  r_coldown[...] = clamp(color[...] + shadedown, 0, 255);
  ```
- `interface_widgets.cc:1161` `widgetbase_draw`；`1180` 调 `shadecolors4(wcol->inner, shadetop, shadedown, inner_col1, inner_col2)` → **竖向双色渐变**
- `interface_widgets.cc:3325` `widget_state()`；`3346`：`UI_SELECT` 用 `inner_sel/outline_sel`；`3371-3373`：hover 调 `widget_active_color`
- `interface_widgets.cc:3287` `widget_active_color`：**HSL 乘性高亮**（在主题色基础上派生，不能自由指定 hover 色）
- `interface_widgets.cc:3396-3398`：
  ```cpp
  if (state->but_flag & BUT_NODE_ACTIVE) {
    const uchar blue[4] = {86, 128, 194};   // 硬编码
    color_blend_v3_v3(wt->wcol.inner, blue, 0.3f);
  }
  ```
- `source/blender/editors/interface/interface_draw.cc:75` `draw_roundbox_4fv_ex(...)`：支持 `inner1/inner2` + `shade_dir` 的**竖向**双色渐变；`155` `draw_roundbox_4fv`

**（d）默认主题是纯数据文件，可直接数据化下发**

- `release/datafiles/userdef/userdef_default_theme.c:22-30`：`wcol_regular`，`inner=0x545454ff`、`inner_sel=0x4772b3ff`、`roundness=0.2f`
- `userdef_default_theme.c:619`：`space_node.back = 0x1a1a1a00`、`grid = 0x30303000`
- `userdef_default_theme.c:714-723`：`space_topbar.back=0x18181800`、`header=0x181818b3`（alpha≈0.70）
- `userdef_default_theme.c:724-732`：`space_statusbar.header=0x181818b3`

### 1.2 结论：哪些能走主题，哪些不能

**能用 theme 表达（不需要改绘制代码）**

| 视觉项 | 表达方式 |
|---|---|
| 深灰打底 `#0B0303`（控件、菜单、工具条、数字框、进度条、列表项） | 把 `ThemeUI` 各 `wcol_*.inner` 设为 `#0B0303` |
| `#453D3D → #0B0303` 的竖向渐变 | `inner = #0B0303`（RGB 11,3,3），`wcol_regular.shadetop = 58`、`shadedown = 0`。因为 `#453D3D`(69,61,61) − `#0B0303`(11,3,3) = **(58,58,58)**，RGB 同加同减正是 `shadecolors4` 的唯一算法，可**精确**命中两个端点。 |
| 青绿作为「选中/激活」信号 | `wcol_regular.inner_sel` / `outline_sel` 设为 `#178D8D` 系 |
| 圆角半径 | `uiWidgetColors.roundness` |
| 按钮局部明暗（`shade`） | `uiWidgetColors.shaded` |
| 各编辑器背景 / 顶栏 / 状态栏背景 | `ThemeSpace.back` / `header` / `back_grad` + `background_type` |
| 节点颜色（`TH_NODE*`）、连线基础色 | 主题 `space_node` 内字段 |
| 图标透明度/饱和度 | `ThemeUI.icon_alpha` / `icon_saturation` |
| 文本色 | `wcol_text` / `wcol_regular.text` |

**不能用 theme 表达（必须落绘制层）**

| 元素 | 原因 | 最省侵入改法 |
|---|---|---|
| 横向 / 多段青绿渐变、信号线 | `shadecolors4` 只能给 RGB 加同一个常量，方向固定为竖向；`ThemeSpace.back_grad` 仅被 3D 视口背景消费 | 在 `interface_draw.cc` 加一个「横向 n-stop 渐变」helper（`draw_roundbox_4fv_ex` 已支持 2 stop + `shade_dir`，只差横向；浮动面板已自绘色带，可复用其顶点色做法） |
| hover 精确色 | `widget_active_color`（`interface_widgets.cc:3287`）是 HSL 乘性派生 | 只改这一个函数：`l_factor` 改为向 `#85B2B2` 混色的固定系数 |
| `BUT_NODE_ACTIVE` 的硬编码蓝 | `interface_widgets.cc:3396-3398` | 把 `blue` 换成主题 `inner_sel` 或新的 `ThemeUI` 字段 |
| 节点编辑器背景 alpha | `node_draw.cc:4720-4721` 用 `get_color_3fv(TH_BACK)` 后 `GPU_clear_color(..., 1.0)`，**alpha 被丢弃** | 改 `4721` 一行：改调 `get_color_4fv` 并用取到的 alpha（或直接跳过 clear）。这是「节点背景透明」的唯一必经点。 |
| 线宽、虚线、描边粗细 | 代码常量（`GPU_state` / 顶点尺寸） | 绘制层常量 |
| 模糊 / 玻璃效果 | 主题无 framebuffer 语义 | 绘制层（且默认关闭，见既定决策） |
| 动画曲线 / 时序 | 主题无此概念 | 绘制层或 overlay |
| 图标形状 | 主题只能调 alpha/saturation | 需换图标资产 |

### 1.3 主题是否存在渐变能力（专项核实）

- 结构上只有两处「渐变」字段：
  1. `ThemeSpace.back_grad` + `background_type`（`DNA_theme_types.h:294-300`），实际消费方仅 3D 视口背景，**与控件无关**。
  2. `uiWidgetColors.shaded/shadetop/shadown`，即 `shadecolors4` 实现的**统一亮度偏移竖向双色渐变**。
- 所以：
  - **竖向 2-stop 渐变 = 主题原生支持**（且 `#453D3D→#0B0303` 恰好可精确表达）。
  - **横向渐变 / 3-stop 以上 = 主题不支持**，必须落绘制层。
- 折中方案（推荐）：竖向用主题（零代码），横向/多段用「绘制层渐变 helper」，只服务少数元素（顶栏渐变底、面板信号线、节点边框高光）。这样 `theme` 承担 80% 面积色，绘制层只承担少数强调元素。

### 1.4 路线与代价

| 路线 | 做法 | 代价 | 风险 |
|---|---|---|---|
| **T1 主题数据（推荐）** | 改 `userdef_default_theme.c`（或运行时写 `preferences.themes[0]`） | 零绘制代码；一份数据文件 | 极低。缺点：横向渐变/线宽/hover 精确色做不到 |
| T2 运行时注入 | 启动脚本改 `bpy.context.preferences.themes[0]` | 不动 C 代码，可热调 | 低。缺点：只覆盖 theme 能表达的字段；工厂重置后需重注入 |
| T3 全量重写绘制 | 重写 `ui_draw_roundbox` / `widgetbase_draw` | 侵入大，回归面广（所有编辑器所有控件） | 高。不推荐 |
| **T1 + P（推荐组合）** | 主题数据打底 + 3 个局部绘制补丁（`widget_active_color`、`BUT_NODE_ACTIVE`、节点背景 alpha）+ 1 个横向渐变 helper | 4 处小改 | 低 |

**推荐：T1 + P。**「全局终末地化」的 80% 观感由主题数据完成，剩下 20% 由 4 个定点补丁完成，避免重写 `ui_draw_roundbox`。

---

## 2. Topbar「呼吸式自动隐藏」

### 2.1 顶栏高度是布局期算定还是可运行时改写（证据）

**（a）顶栏是 global area，不在 screen 布局里**

- `source/blender/editors/screen/screen_edit.cc:1282-1295` `screen_global_topbar_area_refresh`：
  ```cpp
  const short size = screen_global_header_size();
  ...
  screen_global_area_refresh(win, screen, SPACE_TOPBAR, GLOBAL_AREA_ALIGN_TOP,
                             &rect, size, size, size);   // size_min == size_max == size
  ```
  - **`size_min == size_max == size`**：顶栏在 global area 框架里**没有折叠态**，运行时无法通过 min/max 表达「缩到 2px」。
- `screen_edit.cc:1234-1275` `screen_global_area_refresh`；`1270-1274`：
  ```cpp
  if (area->global->cur_fixed_height != height_cur) {
    area->global->cur_fixed_height = height_cur;
    screen->do_refresh = true;
  }
  ```
- `screen_edit.cc:1330-1344` `ED_screen_global_areas_refresh`：**每次刷新都调 topbar + statusbar refresh**。
- `screen_edit.cc:804`（在 `screen_refresh_if_needed` 内）每次 refresh 也调 `ED_screen_global_areas_refresh(win)`。
- 因此：**写 `cur_fixed_height` 会被下一次 refresh 立刻改回 `screen_global_header_size()`**。

**（b）唯一现存「折叠」先例是 statusbar**

- `screen_edit.cc:1297-1311`：`size_min = 1`、`size_max = 0.85 * header`、`size = (screen->flag & SCREEN_COLLAPSE_STATUSBAR) ? size_min : size_max`
- `screen_edit.cc:1313-1328` `ED_screen_global_areas_sync`：从 `cur_fixed_height == size_min` 反推 `SCREEN_COLLAPSE_STATUSBAR`
- **结论**：如果要做「顶栏折叠」，正规做法是照抄 statusbar：加 `SCREEN_COLLAPSE_TOPBAR` flag + 让 `screen_global_topbar_area_refresh` 的 min/max 分开。这是**唯一不需要和刷新逻辑对抗**的路径。

**（c）2px 顶栏会直接不绘制**

- `source/blender/editors/screen/area.cc:1692` `region_rect_recursive`（`2078` 调用）：header 的 `prefsizey = ED_area_headersize()`；当 `fac < -2` 时置 `RGN_FLAG_TOO_SMALL` 且 `visible = false`。
- 即：区域实际高度小于 header 期望高度约 2px 以上 → region 直接 `visible=false`，**不是「变细」而是「消失」**。

**（d）全局刷新是「全量重排」**

- `screen_edit.cc:775-818` `screen_refresh_if_needed`：
  - `790-792`：每轮 `screen_regions_poll`
  - `804`：`ED_screen_global_areas_refresh(win)`
  - `806`：`screen_geom_vertices_scale`
  - `808-812`：对**每个 area** `ED_area_init()`
- `source/blender/editors/screen/area.cc:2279-2300` `ED_area_update_region_sizes`：对每个 region 重新 `region_rect_recursive` 并 `init()`。
- **结论**：走「每帧改顶栏高度」时，如果触发 `do_refresh`，就是**整屏所有区域每帧全量 re-init**。这是走路线 A 的最大隐性成本。

**（e）顶栏本身可作为普通/离屏 area 复用**

- `source/blender/editors/space_topbar/space_topbar.cc:43-66` `topbar_create`：3 个 region —— header(TOP)、header(RIGHT|SPLIT_PREV)、window
- `space_topbar.cc:105-111` `topbar_header_region_init`：`ED_region_header_init` + 右侧 `RGN_FLAG_DYNAMIC_SIZE`
- `space_topbar.cc:287-333` `ED_spacetype_topbar`：`307` layout=`ED_region_header_layout`，`308` draw=`ED_region_header_draw`
- **全文无 `area->global` 引用** → `SPACE_TOPBAR` 可以作为**普通/离屏 ScrArea** 使用，原生菜单绘制可直接复用。

**（f）离屏 area 已有可复制的成功模板**

- `area.cc:2470` `ED_area_offscreen_create(wmWindow *win, eSpace_Type space_type)`
- 浮动面板线 `interface_region_floating_panel.cc` 约 `415-495` 就是用它承载 `SPACE_NODE`：4 个孤立 verts + `BLI_addhead(&screen->areabase, area)` + `ED_area_init` + 手动 `region.overlap = true`。
- `area.cc:1719`：`region->overlap = ED_region_is_overlap(area->spacetype, region->regiontype);` —— **每次 size update 都会把 `overlap` 重置**，所以手置 `overlap=true` 后必须在每次更新后重新置位。
- `area.cc:1642` `bool ED_region_is_overlap(const int spacetype, const int regiontype)` —— 若要彻底解决，可给它加 `SPACE_TOPBAR` 分支。

**（g）窗口级事件依赖几何**

- `source/blender/windowmanager/intern/wm_event_system.cc:1627-1628`：
  ```cpp
  event->mval[0] = event->xy[0] - region->winrct.xmin;
  event->mval[1] = event->xy[1] - region->winrct.ymin;
  ```
- `wm_event_system.cc:3889` `area_event_inside`、`3904` `region_event_inside`；`4348-4349` 每个事件都按当前几何重解析 area/region。
- `wm_event_system.cc:3722-3849` 是 `event_queue_check_drag` 全流程；`4275-4283`、`4463-4468` 是拖拽状态收尾。
- **结论**：`event_queue_check_drag` 本身只看事件/按键，不看几何；但**几何在拖拽中途变化会让 handler 与 `event->mval` 跳变**。因此任何「改高度」方案必须加守卫：拖拽中（`win->runtime->drags` 非空）或 modal 算子运行中（`wm_window_has_modal_operator(win)`）不推进动画。

### 2.2 两条/三条路与代价对比

| | **路线 A：真改顶栏 ARegion 高度 + 300ms 插值** | **路线 B：只在绘制层视觉收缩，区域高度不变** | **路线 B1：离屏 `SPACE_TOPBAR` area（推荐）** |
|---|---|---|---|
| 3D 能否顶到窗口顶 | 能（顶栏 area 真的变矮） | **不能**：顶栏 area 仍占 26px，3D 永远差一截 | 能（原生 global topbar 置 `GLOBAL_AREA_IS_HIDDEN`，离屏 area 不参与 tiling） |
| 需要的调用 | 改 `screen_global_topbar_area_refresh` 的 min/max；每帧 `cur_fixed_height` + `do_refresh`；或逐帧 `ED_area_update_region_sizes` | 只 `ED_region_tag_redraw` + 自绘 | 每帧只改 4 个孤立 vert 的 y + `ED_area_update_region_sizes(area)` + `ED_region_tag_redraw` |
| 布局重排抖动 | **有**：`do_refresh` → 全屏 `ED_area_init`，3D 视口每帧重建 region | 无（几何不变） | **无**（3D 与其它 area 完全不动，只有离屏 area 重算 3 region） |
| 拖拽/事件风险 | **高**：几何变化导致 `event->mval` 跳变，`CTX_wm_region` 漂移；须守卫 | 低（命中区不变） | 低（离屏 area 不参与主 tiling；但需自行做命中与 hover 判定） |
| 2px 时视觉 | **region `TOO_SMALL` 直接消失**，无法做「2px 细线」 | 可做到任意视觉高度 | 可做到任意视觉高度（但 header region 太矮仍会 `TOO_SMALL`，需改为自绘或保留最小高度） |
| 鼠标感应区 | 天然（区域高度即感应区） | **要额外处理**：区域还是 26px，命中测试会「提前」吃掉事件；需在 area handler 里判断 y 并放行 | 需自行判断：离屏 area 加入事件分发后按 `totrct` 命中；可在 handler 内按「距顶距离」决定展开 |
| 原生菜单 | 保留 | 保留 | 保留（`SPACE_TOPBAR` 的 header region 原样） |
| 侵入面 | `screen_edit.cc`（global area 框架）+ `area.cc`（TOO_SMALL）+ `wm_event_system` 守卫 | `area.cc`（`ED_region_header_draw`）+ 自绘代码 | 新增一个离屏 area 管理模块 + `area.cc`（overlap 分支） |
| 与浮动面板线冲突 | `wm_draw.cc` 需要挂 overlay → **冲突** | `wm_draw.cc` 需要挂 overlay → **冲突** | 可在现有 overlay 绘制回调内做，仍**与 `wm_draw.cc` 冲突** |

### 2.3 推荐（低风险）

**推荐 B1，按以下分层落地：**

1. **隐藏原生 global topbar**：对 `win->global_areas` 里的 `SPACE_TOPBAR` 置 `GLOBAL_AREA_IS_HIDDEN`（`area.cc:2381`、`4475`、`4488` 是唯一三处消费点，行为已验证：隐藏后不参与 `WM_window_screen_rect_calc` 的高度扣除，见 `wm_window.cc:3189-3198`；`screen_geom_vertices_scale` 也会跳过，见 `screen_geom.cc:267-269`）。
   - 这一句是**实现「3D 顶到窗口顶」的前提**，代价最小。
2. **造离屏 `SPACE_TOPBAR` area**（抄浮动面板 415-495 的模板）：4 个孤立 verts，`ED_area_init`，`header` region 置 `overlap = true`。
3. **逐帧只改这一块的 vert y**：展开 26~30px ↔ 收起 2px（或 0 + 自绘细线），调 `ED_area_update_region_sizes(area)` + `ED_region_tag_redraw`。**绝不调 `ED_screen_global_areas_refresh`，绝不置 `screen->do_refresh`**。
4. **动画驱动**：`WM_event_add_ui_handler(nullptr, &win->runtime->modalhandlers, ...)`（浮动面板 2380-2395 同款）+ timer；`MOUSEMOVE` 进入顶部热区触发展开、离开进入 cool-down 触发收起。
5. **守卫**：`wm_window_has_modal_operator(win)` 或 `win->runtime->drags` 非空时冻结动画（照抄浮动面板 2068 的 static 实现）。
6. **补偿 overlap 重置**：每次 `ED_area_update_region_sizes` 后重新 `header_region->overlap = true`；或长期方案给 `ED_region_is_overlap`（`area.cc:1642`）加 `SPACE_TOPBAR` 分支。
7. **2px 阶段的视觉**：header region 在 `TOO_SMALL` 时不画，所以「2px 极细线」不能依赖原生 header draw，必须在 overlay 里自绘一条 2px 青绿线。

**为什么不选 A**：A 要同时改 `screen_edit.cc` 的 global area min/max、`area.cc` 的 `TOO_SMALL` 阈值、并在每帧触发全屏 `ED_area_init`；收益只是「少写一个离屏 area」，但代价是**整屏抖动 + 拖拽跳变**。

**为什么不选 B（纯绘制收缩）**：顶栏 area 仍占 26px，3D 视口无法铺满，与「3D 全屏」需求直接冲突。

---

## 3. 「3D 视口全屏 + 其它 ScrArea 折叠」

### 3.1 三种做法的真实后果（证据）

**（a）把其它区域高度压到 0 —— 不可行**

- `source/blender/editors/screen/screen_geometry.cc:145-175` `screen_geom_vertices_scale_pass`：先求所有 `ScrVert` 的 min/max，再按 `facx/facy` 逐点缩放（**含离屏区域的孤立 verts**）。
- `screen_geometry.cc:188-245`：强制每块 area 不低于 `ED_area_headersize()`，通过 `screen_geom_area_height` 调整 verts。**任何一次几何刷新都会把 0 高区域抬回去**（例外：没有 `ScrEdge` 连接的孤立 4-verts area，这正是离屏 area 的做法）。
- `screen_geometry.cc:251+` `screen_geom_vertices_scale`；`267-269` 跳过 `GLOBAL_AREA_IS_HIDDEN`；`271` 用 `ED_area_global_size_y`。
- `source/blender/windowmanager/intern/wm_event_system.cc:3889` `area_event_inside` 用 `BLI_rcti_isect_pt_v`，**边界包含** → 0 高区域仍会在边界上吃掉 1 像素行的事件。
- 0 高区域内的 region：`area.cc:1692` 的 `fac < -2` → `RGN_FLAG_TOO_SMALL` + `visible=false`，即**不绘制但仍在 areabase 里**。
- **结论**：既不能稳定保持 0 高，又会留一个「吃 1px 事件」的隐形区域。

**（b）销毁/重建 ScrArea —— 风险大**

- `screen_edit.cc:83-92` `screen_delarea`：`ED_area_exit(C, area)` → `BKE_screen_area_free(area)` → `BLI_remlink` → `MEM_delete`。
- 存在多处持有 `ScrArea*` / `ARegion*` 的引用方：space 数据、`CTX_wm_area`、modal handler 的 `CTX`、浮动面板的离屏 area、tool system、gizmo group。销毁后若不逐一清理，就是悬空指针。
- 不可撤销（不进 undo），用户误触后布局无法恢复。
- **结论**：不做。

**（c）复用 `SCREENMAXIMIZED` / `SCREENFULL` —— 现成、低风险**

- `source/blender/editors/screen/screen_ops.cc:4543-4560` `SCREEN_OT_screen_full_area`；`4556`：`use_hide_panels` 默认 **false**。
- `screen_ops.cc:4491` 读 `use_hide_panels`；`4509-4512`：false 路径要求当前 `SCREENNORMAL|SCREENFULL` → 切 `SCREENMAXIMIZED`？实际分支：`4491` 读参，`4509-4512` 走 `SCREENFULL`（hide panels），`4515-4522` 走 `SCREENMAXIMIZED`。
- `screen_ops.cc:5301-5306`、`6213-6223`：UI 入口，Focus Mode 显式 `use_hide_panels = true`。
- `source/blender/editors/screen/screen_edit.cc:1614-1727` `screen_state_to_nonnormal`：
  - `1627`：`BLI_assert(ELEM(state, SCREENMAXIMIZED, SCREENFULL))`
  - `1634`：`ED_workspace_layout_add(...)` 新建临时 screen
  - `1636-1640`：只复制 `state / redraws_flag / temp / flag` —— **不复制 IDProperty**
  - `1646-1654`：`ED_area_data_swap(newa, toggle_area)`，只留被切换的那块 area
  - `1656-1660`：**仅 SCREENFULL** 对 `win->global_areas` 置 `GLOBAL_AREA_IS_HIDDEN`（`1659`）
  - `1662-1677`：**仅 SCREENFULL** 隐藏 `RGN_TYPE_UI/HEADER/TOOL_HEADER/FOOTER/TOOLS/NAV_BAR/EXECUTE/ASSET_SHELF`
  - `1679-1716`：**仅 SCREENFULL** 隐藏 gizmo / 导航 / 文字 / 统计
- `screen_edit.cc:1734` `ED_screen_state_toggle`；`1789` 恢复 `GLOBAL_AREA_IS_HIDDEN`；`1849` `BKE_workspace_layout_remove(...)` 释放临时 screen。
- `source/blender/blenkernel/intern/workspace.cc`：`BKE_workspace_layout_remove` 会 `BKE_id_free` 临时 `bScreen` → **临时 screen 的 IDProperty 随退出一起销毁**。
- **结论**：
  - `SCREENMAXIMIZED`（默认）＝ 其它 area 消失、**顶栏/状态栏保留** → 不是完全沉浸。
  - `SCREENFULL`（Focus Mode）＝ 其它 area + 顶栏 + 状态栏 + 侧面板 + gizmo 全部隐藏 → **正是「3D 全屏」需求**。

**（d）替换默认 workspace —— 只能改「新文件」**

- `scripts/startup/bl_app_templates_system/Game_Engine/startup.blend`（约 736KB）+ `__init__.py`（含 `load_factory_startup_post`）。
- 只影响新建 / 工厂启动，**不影响已存在的 `.blend`**；且顶栏/状态栏是 global area，不在 screen 布局里，模板**无法移除**。

### 3.2 路线与代价对比

| 路线 | 做法 | 维护债 | 风险 |
|---|---|---|---|
| **R1：`SCREEN_OT_screen_full_area(use_hide_panels=True)` = Focus Mode** | 运行时算子切到 `SCREENFULL` | (1) 换 screen 不复制 IDProperty → 面板布局/锁定状态丢失；(2) 退出时临时 screen 被 free；(3) 浮窗状态回默认 | **低**（官方路径，所有清理都已写好） |
| R2：`SCREENMAXIMIZED` | 算子 + `use_hide_panels=False` | 顶栏/状态栏仍在 → 不满足「3D 全屏」 | 低 |
| R3：启动时改 workspace / 模板 | 改 `startup.blend` + `__init__.py` | 只影响新文件；全局区域无法移除；不可运行时切换 | 低但收益低 |
| R4：压 0 高 | 手改 `ScrVert` | 每次刷新被抬回；留 1px 命中；region `TOO_SMALL` | **不可行** |
| R5：销毁/重建 ScrArea | `screen_delarea` + `screen_addarea` | 悬空指针、SpaceLink 丢失、不可撤销 | **高** |

### 3.3 推荐 + 与 F 阶段持久化的冲突

**推荐 R1（Focus Mode），并把「沉浸期间的面板状态」做成会话级镜像：**

- 进入沉浸前：把当前 screen 的面板 IDProperty 快照到进程内注册表（key = workspace + screen 名 + area 指针）。
- 沉浸期间：面板读写注册表（因为临时 screen 没有 IDProperty）。
- 退出：把注册表回写到原 screen 的 IDProperty。
- 若不镜像，用户看到的体验就是「进全屏再退出，面板全回默认」——这会直接推翻 F 阶段「锁定状态必须持久化」的决策。

**启动时（可选，R3 作为补充）**：只用于「新建文件默认就是 3D 全屏单 area」的观感，不作为运行时开关。

---

## 4. 与现有浮动面板线的冲突面

### 4.1 文件重叠（**必须串行**，不可并行改）

| 文件 | 面板线现状 | 本阶段是否需要动 | 处理 |
|---|---|---|---|
| `source/blender/windowmanager/intern/wm_draw.cc` | 已改：`1238` overlay draw、`1275-1285` `ED_screen_floating_panel_ensure` | B1 的顶栏 overlay / 沉浸切换可能想挂在这里 | **串行**。等面板线合并后再加 hook；或只通过 `WM_draw_cb_activate` 挂独立回调，避免直接编辑 `wm_draw.cc` |
| `source/blender/editors/screen/ED_screen.hh` | 已加 `ED_screen_floating_panel_*` 声明 | 新离屏顶栏需要放声明 | **串行** |
| `source/blender/editors/interface/regions/interface_region_floating_panel.cc` | 面板线独占，仍在改 | 不改；只复制其离屏 area / modal handler 模式 | **只读参考** |
| `CMakeLists.txt`（editors/interface 相关） | 面板线可能已加新文件 | 新文件注册会有冲突 | **串行** |

### 4.2 无重叠、可安全改

| 文件 | 用途 |
|---|---|
| `release/datafiles/userdef/userdef_default_theme.c` | T1 主题数据 |
| `source/blender/editors/interface/interface_widgets.cc` | P：hover 派生色、`BUT_NODE_ACTIVE` |
| `source/blender/editors/interface/interface_draw.cc` | 横向渐变 helper |
| `source/blender/editors/space_node/node_draw.cc` | 节点背景 alpha（H 阶段） |
| `source/blender/editors/screen/screen_edit.cc` | 顶栏折叠 flag（若走 A）/ 沉浸镜像 |
| `source/blender/editors/screen/area.cc` | `ED_region_is_overlap` 加 `SPACE_TOPBAR` |
| `source/blender/editors/screen/screen_ops.cc` | 沉浸模式算子包装 |

### 4.3 行为级冲突（不是文件冲突，但是**语义冲突**）

- 沉浸模式（R1）会**换 screen**，而浮动面板的屏幕级 region 属于 `bScreen.regionbase` → 换 screen 时被重建为默认（`wm_draw.cc:1275-1285` 的 `ensure` 注释已明说这点）。
- 面板线的状态数据（若按 F 阶段存 Screen IDProperty）在临时 screen 上**根本不存在**。
- **必须串行**：先让面板线把「状态存储从 Screen 实例解耦（改为按 workspace+screen 名查注册表）」落定，再上沉浸模式；否则两线会在同一个 bug 上互相甩锅。

---

## 5. 建议落地顺序（先做哪一件、为什么）

1. **第一件：T1 主题数据基线**（`userdef_default_theme.c`）。
   - 为什么先做：零侵入、零冲突、可立刻看到全局观感；且它决定后面所有绘制补丁的「底噪」，先定基线才能判断补丁是否必要。
   - 风险：极低。验证方式：替换数据文件后重启，看按钮/菜单/顶栏/节点底色。
2. **第二件：P 局部绘制补丁**（`widget_active_color`、`BUT_NODE_ACTIVE`、`node_draw.cc:4721`、横向渐变 helper）。
   - 为什么：4 个定点，逐个可单独回滚；此时不引入任何 routing / 区域改动。
3. **第三件：B1 离屏呼吸顶栏**（隐藏 global topbar + 离屏 `SPACE_TOPBAR` + 逐帧 vert）。
   - 为什么在沉浸之前：它自己就能独立交付「鼠标移开缩回」，且不换 screen、不动 3D 布局，失败也只影响顶栏。
   - 前置：给 `ED_region_is_overlap`（`area.cc:1642`）加 `SPACE_TOPBAR` 分支，否则每次 size update 后要手工置 `overlap`（易漏）。
4. **第四件：R1 沉浸模式（3D 全屏 + 折叠其它 area）**。
   - 为什么最后：它会换 screen、隐藏 global areas、重置所有屏幕级 region，**是唯一会放大前面所有状态假设的一步**；必须等 F 阶段的 IDProperty 持久化与面板线的状态解耦都落定。
   - 前置：面板状态注册表/镜像方案完成。
5. **第五件：H 阶段节点深度美化**（线宽、端口几何、连线单色）。
   - 独立，不依赖外壳。

**不建议的顺序**：先做沉浸再回头做主题 —— 沉浸会换 screen，调试主题时你会分不清「是主题没生效还是临时 screen 没继承」，成本翻倍。

---

## 6. 必须由用户拍板的分歧点

1. **顶栏收起态的视觉与热区**
   - (a) 收到 2px「细线」并用 overlay 自绘，还是 (b) 收到 0px 完全消失但保留 4~6px 隐形热区？
   - 影响：header region 在 `TOO_SMALL` 时**不绘制**，所以 2px 视觉必须自绘；而隐形热区又要求离屏 area 的 `totrct` 至少覆盖热区。
2. **顶栏是否跨重启记忆「钉住展开」**
   - 若要，需进外部配置（F 阶段 A 方案）；若不要，纯会话状态即可。
3. **动画实现层**
   - (a) 逐帧改原生 vert 高度（几何真变化，3D 同帧重排）；还是 (b) overlay 端点吸附（几何只在 0/26 两态，中间靠 overlay 绘制过渡）。
   - (b) 更平滑、更省，但「视觉高度 ≠ 命中高度」期间有轻微不一致，需用户接受。
4. **沉浸模式选型**
   - `SCREENFULL`（连顶栏/状态栏一起隐藏，真沉浸）还是 `SCREENMAXIMIZED`（保留顶栏/状态栏）。
   - 这直接决定 B1 的离屏顶栏在沉浸期间是「唯一顶栏」还是「被隐藏」。
5. **沉浸期间是否保留原生菜单**
   - 保留 → 离屏顶栏必须在 `SCREENFULL` 下仍然可交互（但它不属于 global area，可行但需额外保活）。
   - 不保留 → 更彻底，但用户失去 File/Edit 入口，需要自建。
6. **主题下发通道**
   - 改 `userdef_default_theme.c`（随构建固定）还是启动脚本写 `preferences.themes[0]`（可热调、可用户覆盖）。
   - 影响：前者对所有用户生效且不可关；后者可被 `load factory settings` 冲掉。
7. **hover 是否接受 HSL 派生**
   - 若必须精确命中 `#85B2B2`，就要改 `widget_active_color`；若接受派生，则零代码但色值会有偏差。
8. **沉浸期间的面板状态镜像**
   - 进程内注册表（简单、但崩溃即丢）vs 退出时回写原 screen（安全、但需要正确处理「沉浸中直接关软件」）。
9. **节点背景透明的度数**
   - 全透明（透出 3D）还是半透明深灰（既定决策倾向「模拟磨砂」）。前者必须改 `node_draw.cc:4721`，后者也需要改，但 alpha 值不同。

---

## 附录：本轮证据索引（便于复核）

| 主题 | 文件:行号 |
|---|---|
| 主题入口 | `editors/interface/resources.cc:40,74,1124,1128,1244,1252,1430,1551-1556` |
| 主题结构 | `makesdna/DNA_theme_types.h:184-196,192,194,195,198+,217-245,294-300,307,504-544,518,538,543` |
| 默认主题数据 | `release/datafiles/userdef/userdef_default_theme.c:22-30,619,714-723,724-732` |
| 控件绘制 | `editors/interface/interface_widgets.cc:990-1001,1161,1180,3287,3325,3346,3371-3373,3396-3398` |
| 圆角绘制 | `editors/interface/interface_draw.cc:75,155` |
| 区域/绘制 | `editors/screen/area.cc:1642,1692,1719,2078,2279-2300,2372,2381,2470,3149-3160,3162-3175,3715,4339-4352,4399,4410,4432,4468,4475,4488` |
| 全局区域刷新 | `editors/screen/screen_edit.cc:775-818,790-792,804,806,808-812,1234-1275,1270-1274,1277-1280,1282-1295,1297-1311,1313-1328,1330-1344,1349+` |
| 沉浸模式 | `editors/screen/screen_edit.cc:1614-1727,1627,1634,1636-1640,1646-1654,1656-1660,1659,1662-1677,1679-1716,1719-1722,1734,1789,1849` |
| 几何缩放 | `editors/screen/screen_geometry.cc:145-175,188-245,251+,267-269,271,300-340` |
| 算子 | `editors/screen/screen_ops.cc:4491,4509-4522,4543-4560,4556,5301-5306,6213-6223` |
| 事件系统 | `windowmanager/intern/wm_event_system.cc:1627-1633,3722-3849,3889,3904,3957-3958,4275-4283,4348-4349,4463-4468` |
| 窗口几何 | `windowmanager/intern/wm_window.cc:1193,3189-3198` |
| 绘制顺序 | `windowmanager/intern/wm_draw.cc:1214-1224,1233,1236-1238,1240-1246,1275-1285`（**禁改**） |
| Topbar space | `editors/space_topbar/space_topbar.cc:43-66,85-98,105-111,287-333`（`307/308`） |
| 节点背景 | `editors/space_node/node_draw.cc:4717-4721,4739` |
| 离屏 area 模板 | `editors/interface/regions/interface_region_floating_panel.cc` 约 `415-495`、`2068`、`2324`、`2380-2395`（**只读参考**） |
| workspace 释放 | `blenkernel/intern/workspace.cc`（`BKE_workspace_layout_remove` / `BKE_id_free`） |
| 启动模板 | `scripts/startup/bl_app_templates_system/Game_Engine/startup.blend` + `__init__.py` |
