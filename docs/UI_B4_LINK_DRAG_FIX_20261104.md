> 本文是事故记录（连线拖拽漂移，2026-11-04），主题文档见 [B6_UI_ADVANCED.md](B6_UI_ADVANCED.md)。

# 拖线预览终点漂移修复（2026-11-04）

## 现象与复现序列

节点编辑器中按住一根已连好的线从输出端口往外拖、未松开鼠标时滚轮向上放大 3 格，然后松开；
拖拽过程中画布继续缩放/平移时，预览线的终点会偏离鼠标位置，不再贴住光标。

- 环境：Blender 5.3.0 Alpha（`build_linux` 今日构建，2026-11-04 13:09），EBC 0.53.0。
- 复现环境：`DISPLAY=:0`，`BGELogicTree`，节点编辑器。
- 真鼠标事件无法注入（Blender 为原生 Wayland 窗口，`xdotool`/`ydotool`/`wtype` 均不可用），
  因此用 API 序列复现：写入 `space_data.cursor_location` 后执行
  `view2d.zoom_in ×3` + `view2d.pan(60, -40)`，记录换算前后的视图坐标与像素坐标。

## 根因

`_draw_links` 的拖线预览终点 `p3` 直接取 `space_data.cursor_location × dpi_fac()`。
`cursor_location` 只在鼠标移动事件里刷新；拖线中滚轮缩放/平移不会更新它，
而 `p3` 所在的视图空间会随缩放变化，于是固定不变的旧值被换算到新的屏幕位置，产生漂移。
已连线的绘制路径读取 socket runtime 坐标（ctypes 偏移 456/24），每帧缓存清空，
与本问题无关，未改动。

## 改动

文件：`scripts/startup/bl_ui/rainbow_noodle.py`
备份：`rainbow_noodle.py.bak_20261104`（未覆盖已有 `.bak` / `.bak_20261101`）

1. 新增 `_DRAG_CURSOR_CACHE`、`_view_bounds(v2d, region)`、`_drag_cursor_view_loc(context, v2d, drag_start)`（L2317-L2367）。
   - 首次按 `cursor_location` 换算并缓存光标的区域像素坐标；
   - 视图范围变化且 `cursor_location` 未变（拖线中缩放/平移）时，用缓存像素经当前 `v2d.region_to_view` 反算；
   - 鼠标本身移动过（`cursor_location` 变化）时按新鲜值换算；
   - 取不到 region/view2d 时直接回退到 `cursor_location`，try/except 包裹，不抛异常。
2. 预览终点改为实时换算（L2796-L2799）：保留 `fac = dpi_fac()`（吸附计算仍使用），
   `cursor_x, cursor_y = _drag_cursor_view_loc(context, v2d, drag_start)`，`p3 = (cursor_x, cursor_y)`。
   吸附与贝塞尔路径未改，已连线绘制路径未改。

## API 复测数据

光标 `cursor_location=(300,-60)`，`dpi_fac=1.25`，初始像素 `(622,396)`。
偏移 = 预览端点像素与真实光标像素的距离。

| 阶段 | 视图范围 | cursor_location | helper 端点像素 | 偏移 | 旧路径端点像素 | 旧路径偏移 |
|---|---|---|---|---|---|---|
| BEFORE | (-640.0,-722.2)-(641.6,723.9) | (300,-60) | (622,396) | 0.00 px | (622,396) | 0.00 px |
| AFTER zoom_in ×3 | (-506.5,-571.6)-(507.8,572.9) | (300,-60) | (622,396) | 0.00 px | (683,384) | 62.17 px |
| AFTER pan(60,-40) | (-429.2,-623.2)-(585.2,521.4) | (300,-60) | (622,396) | 0.00 px | (623,424) | 28.02 px |

无 region 兜底调用返回 `(375.0, -75.0)`，无异常。

## 回归结果

- 已连线贴合：`view_all` 基准、`zoom_in ×12`、`zoom_out ×25`、`pan(120,-80)`、`zoom_in ×60`、
  `zoom_out ×90` 后端点像素与 socket 坐标一致；截图 `fix_base.png`、`fix_zoommax.png`、`fix_zoommin.png` 均贴合。
- 拖拽路径：`_drag_cursor_view_loc` 的三种分支（新鲜值 / 视图变化 / 无 region）均直接调用通过，无异常。
- 树切换：材质 `ShaderNodeTree` → 几何 `GeometryNodeTree` → 逻辑 `BGLogicTree` 往返 6 次，
  进程正常退出（EXIT=0），无卡死；切回逻辑树后连线绘制正常（`fix8_logic.png`）。
  几何树未挂到对象修改器时，节点编辑器会回落为空槽（Blender 自身行为，非卡死）。
- 预览缩略图：`node_preview_thumbnails` 扩展启用成功（`{'FINISHED'}`），
  材质节点树上三个节点上方均渲染出预览方块（`fix8_shader2.png`）。
- 面板：`LOGIC_NODES_PT_native_inspector`、`LOGIC_NODES_PT_native_palette` 均注册，
  `NPV_PT_panel` 注册正常。

## 限制

本修复经 API 路径验证，仍待在真机由用户手动拖拽确认。
真鼠标事件无法在本环境注入，预览端点像素一致性由等价 API 序列（写入 `cursor_location` +
`view2d` 缩放/平移）验证，未覆盖真实鼠标事件链。
