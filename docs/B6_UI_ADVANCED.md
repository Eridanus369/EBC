# B6.4 范围 A：UI 高级节点

本批新增 6 个 UI 高级节点，全部走现有 `KX_UIAdapter` / `KX_UIRenderer`，
没有新增渲染路径，也没有绕过适配器直接调用 GPU。

| # | 节点 | 端口（输入 / 输出） | config |
|---|---|---|---|
| 1 | `CreateScrollView` | OnEnter, Parent, Pos, Size / Done, View, Content | `direction`: vertical \| horizontal \| both |
| 2 | `CreateListView` | OnEnter, Parent, Pos, Size, Items(List), Clear(Trigger) / Done, ListView, OnItemClick(Int) | `item_height`, `virtualize` |
| 3 | `CreateTabView` | OnEnter, Parent, Pos, Size, TabNames(List) / Done, TabView, OnTabChanged(Int) | 无 |
| 4 | `UIAnimation` | OnEnter, Widget, Duration(Float), Property(String), FromValue, ToValue / OnDone | `easing`: linear \| ease_in \| ease_out \| ease_in_out |
| 5 | `UIScreenAnchor` | OnEnter, Widget / OnResize, Width(Float), Height(Float) | `anchor`: 9 个位置 |
| 6 | `UIScreenStack` | OnEnter, ScreenName(String) / OnDone | `mode`: push \| pop \| replace |

实现文件：

- `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6UI.cpp`
- 注册声明 `KX_FunctionNode.h`，注册调用在 `KX_RuntimeNodes.cpp`
- 编辑器表 `scripts/startup/bge_gamenodes/editor/nodes/native_b6ui.py`

## 关键实现

### CreateScrollView

- 视图节点带 `clip=true`，内容作为子元素，滚动偏移通过内容 `position` 应用。
- 滚动条复用现有 slider 控件（`CreateSlider`，垂直 orientation=1），不新增滚动控件。
- 偏移 = slider 值 × (内容尺寸 − 视图尺寸)，slider 值先 clamp 到 `[0,1]`，
  因此内容永远不会被滚出可视区；裁剪由 `KX_UIRenderer` 的 scissor 负责。
- `direction=vertical` 只算 Y 轴，`horizontal` 只算 X 轴，`both` 两根条各自独立；
  X 轴对应水平条、Y 轴对应垂直条（早期实现把两者映射反了，已在 E2E 中暴露并修正）。

### CreateListView

- 视图 `clip=true`，内部有一个滚动 slider；item label 由节点按需创建/销毁。
- `virtualize=false`：为每一项创建 label（E2E 中 6 项 → 6 个 label）。
- `virtualize=true`：只创建从 `floor(offset/item_height)` 开始的可见槽位
  （E2E 中视口 2 项高 → 3 个 label），并给这些 label 打上 `virtualized=true`。
- 点击索引完全由「视口命中 + item_height + 当前 offset」计算，
  与是否虚拟化无关：E2E 断言 `virtualize` true/false 的 `OnItemClick` 同为 `3`。
- `OnItemClick` 是**锁存**输出：点击后在后续帧持续返回同一个索引，直到下一次
  点击另一个条目（索引随之更新）或 `Clear` 触发（回到 `-1`）。没有点击且从未
  选中时输出 `-1`。
- `Clear` 按**边沿**触发（false→true 的跳变），这样未连线（每帧为 true）的
  Clear 不会把选中项每帧清空；清空后同样保持 `-1`，直到下一次点击。

### 渲染器虚拟化（新增能力）

`KX_UIRenderer` 的布局阶段新增：

- `Entry::has_clip/clip[4]/culled`，`KX_UIRenderStats::culled`。
- 布局时为每个元素解析最近的裁剪祖先（`clip` 链），完全在裁剪框外的
  `virtualized` 元素标记为 `culled`，不进入绘制。
- 绘制阶段对 `has_clip` 元素把祖先裁剪矩形与 canvas 裁剪矩形求交后
  设置 `GPU_scissor`，退出时恢复。
- 非虚拟化元素即使被裁剪也照常绘制（GPU 裁剪负责），所以 virtualize
  true/false 的**命中与输出一致，只有绘制量不同**，这一点由 E2E 断言覆盖。
- 单测断言：6 个 `virtualized` 子项中 3 个完全在裁剪框外 → `stats.culled == 3`。

### UIAnimation

- `FromValue` / `ToValue` 是动态端口（`ports_from_ir=true`，IR 端口类型由
  `Property` 与连线决定；C++ spec 端口类型为 `KX_VALUE_NONE`）。
- 一个节点只有一个动画状态槽：再次收到 OnEnter 时 `elapsed=0`、`active=true`，
  即**取消旧动画后重开**，不会出现两个动画同时改同一属性。
- easing 四种用不同的二次曲线：`linear=t`、`ease_in=t²`、
  `ease_out=1-(1-t)²`、`ease_in_out` 分段二次。同一 Duration、同一时刻中间值
  互不相同（E2E：t=0.25 时 25 / 6.25 / 43.75 / 12.5）。
- 动画结束只发一次 `OnDone`；结束后的帧不再重复触发。

### UIScreenAnchor

- `anchor` 为 9 位置字符串（`top_left` … `bottom_right`），Execute 写入
  `anchor_x/anchor_y ∈ {-1,0,1}` 交给渲染器。
- Tick 轮询 `ScreenWidth/ScreenHeight`，变化时输出新尺寸并发 `OnResize`；
  首次采样只建立基线，不触发。

### UIScreenStack

- push 为**隐藏不销毁**：已创建的 canvas 只切 `visible`，句柄始终有效；
  pop 后重新显示。E2E 断言 canvas 数量不增长、`destroy_calls` 不增加。
- `replace` 隐藏当前屏并显示新屏，不压栈。
- 屏幕栈是**按图实例**持有的状态：同一张图里的多个 push/pop/replace 节点通过
  执行上下文上的不透明槽位共用一份栈（`KX_ExecutionContext::UISharedState()`），
  不同图实例各持一份栈，互不干扰。
- 该槽位挂在 `KX_ExecutionContext` 上，而热重载的 `KX_GraphInstance::Rebuild`
  只替换已编译图、复用同一个上下文，因此重建后栈深度与栈顶句柄原样保留，
  不需要额外的迁移表。

## 验证

- `ninja -C build_linux -j6` → `/tmp/b64_build_final.log`，exit 0。
- `port_contract.py`：errors=0（当前重跑 `/tmp/ui_u4_final/port_contract.log`）。
- `enum_consistency.py`：`checked_pairs=122 inconsistencies=3`，3 条均在其它线
  （`AddConstraint` / `CreateVehicle`），B6UI 无条目，
  `/tmp/ui_u4_final/enum_consistency.log`。
- 注册检查：UI 6/6，`MISSING [] EXTRA []`，`/tmp/b64_registration_final.log`。
- E2E（`tests/b64_e2e.cpp`）：当前批次 94 项断言，覆盖滚动拖动与边界 clamp、
  列表点击 virtualize 一致、Tab 切换、四种 easing 中间值、锚点随窗口变化、
  屏幕栈 push/pop 句柄有效、渲染器裁剪计数，以及新增的列表选中锁存 / Clear
  边沿清空、双图实例栈隔离、热重载后栈深度与栈顶保持。

## 限制

- 滚动条宽度、默认行高是常量（12px / 24px），暂未做成 config。

# C.7 UI 尾巴首步：多字体 / 基础竖排 / Polar 命中修正

本阶段在既有 `KX_UIAdapter` + `KX_UIRenderer` 内扩展，没有新增渲染路径。

## C.7.1 多字体

已做：

- `KX_UIAdapter::LoadFont(name, path)` 接口；Engine 实现先 `EnsureFontStack()`，
  再调用 `KX_UIRenderer::LoadFont`，底层走 `BLF_load_unique`。额外字体拿到独立
  BLF 槽位，**不追加默认字体栈**，`EnsureFontStack()` 的单次加载语义不变。
- `KX_UIRenderer` 内静态注册表 `name → BLF font id`：
  `LoadFont` 加载并登记，`RegisterFont` 供测试/外部注入 id，`ResolveFont` 解析。
- `CreateUILabel` 的 `Font` 端口值现在按**字体名**解析；旧 datablock 名取不到时
  返回默认字体（0）并在 stderr 警告一次，不静默崩溃。
  `ResolveFont("")` 也直接表示默认字体，不触发警告。
- `LoadFont` 在调用 BLF 前用 `fopen` 预检文件，缺失时返回 false 并 stderr 提示，
  避免无字体栈环境（headless/工具链）访问未初始化 BLF。

未做（边界）：

- 没有新增「LoadFont 节点」，运行时加载入口目前是适配器 API；节点/Python 侧暴露
  留待后续阶段。
- 不做字形覆盖检测：某字体缺 CJK 字形时仍由 BLF 自身的回退链处理。
- 不做粗体/斜体/字间距等字体样式。

## C.7.2 基础竖排（非排版级实现）

已做：

- `KX_UIElement::vertical` 字段，新增属性键 `vertical`（set/get 双向）。
- 布局阶段用 `split_utf8_chars` 逐 UTF-8 字符断行：每字符一行，行高 = 字号，
  总高度 = 字符数 × 字号；起始位置仍按 `anchor` / `halign` / `valign` 计算。
- 绘制阶段竖排每行高度固定为字号；水平文本仍用 BLF 实测行高 + `line_height`。

**明确不做**（见标题「基础竖排，非排版级实现」）：

- 标点旋转（句号/引号不再转 90°）；
- 行间距微调、标点挤压、避头尾；
- CJK 与拉丁混排的比例/基线规则（当前拉丁字符与 CJK 同样占一格）。

## Polar 命中修正

已做：

- `KX_UILayoutPolarElements`（`KX_UIElement.h` 内联）在每帧布局阶段把环上坐标
  写回子元素 `position`，公式与渲染器 Polar 分支一致（父子相对坐标）。
- Mock 与 Engine 的 `BeginFrame` 都在 HitTest 之前调用它，因此
  `HitTest` / `HitTestPoint` 与 Draw 使用同一坐标。
- 渲染器 Polar 分支保留（按同一公式重算），结果与写回值一致，二次调用不会累积偏移。

未做（边界）：

- 未处理 canvas `design_size` 缩放下的命中：整个 HitTest 历史上都用未缩放坐标，
  该限制保持不变。
- 嵌套在非 canvas 父级时的通用坐标偏移仍保持原有行为，本阶段只修 Polar 环。

## 验证

- 新增 headless harness `tests/c7_ui.cpp`，日志 `/tmp/c7_ui.log`：
  **55 PASS / 0 FAIL**。覆盖字体注册/解析/回退警告一次、两个 Label 绑定不同字体名、
  竖排 4 字符逐行拆分与行高/总高、Polar 四槽位写回与命中、二次 BeginFrame 无偏移。
- B6.6 回归 `/tmp/b661_e2e`：19 PASS / 0 FAIL。
- 增量构建 `ninja -C build_linux -j2`：exit 0（60/60）。
- `port_contract.py`：`errors=0 warnings=4`；
  `enum_consistency.py`：`checked_pairs=106 inconsistencies=0`。

无 GPU 环境说明：真实 BLF 加载路径不在 headless 中断言；E2E 用 `RegisterFont`
注入字体 id 验证解析与回退，`LoadFont` 的空参数/文件缺失失败路径已实测。
真实字体的字形渲染截图待有 GPU 的环境补充。

## 相关事故记录

- [UI_B4_LINK_DRAG_FIX_20261104.md](UI_B4_LINK_DRAG_FIX_20261104.md)：节点编辑器**连线拖拽漂移事故**的根因与修复记录——`rainbow_noodle.py` 的 `_drag_cursor_view_loc` 改动、`cursor_location` 缓存策略、API 序列回归数据（端点像素偏移 `0.00 / 62.17 / 28.02 px`）与真机限制。该文件保留为事故记录，本节提供主题索引。

## G.3 富文本多属性补充（本轮）

`KX_UIRichSegment` 新增 `size` 字段，`KX_UIRenderer::ParseRichText` 增加
`{size:N}...{/size}`：

- `N` 取值 1..512；非法或越界值按字面量保留，与未知颜色名的处理一致。
- size 标记按栈嵌套：`{/size}` 恢复外层 size，最外层恢复到元素字号（0 表示继承）。
- color 与 size 可以同时作用于同一 run；颜色嵌套仍保持 A.1.3 的既有语义
  （最内层优先 + stderr 只警告一次），不改写此前已验收的行为。
- 绘制时每段先 `BLF_size` 再 `BLF_width`/`BLF_draw`，量宽与绘制使用同一字号，
  因此居中/右对齐不会因 `{size:}` 漂移。
- 行高仍由元素的 `font_size` 与 `line_height` 决定，`{size:N}` 只改字形大小，
  不重排行高（排版级行高重算不做，见 `DEFERRED_FOREVER.md` §7）。

明确不支持（BLF 无对应能力，不做近似实现）：粗体、斜体、下划线、描边、
渐变、per-run 字体族切换、字形覆盖检测。上述标记按普通文本原样输出。

## A.1.2 旋钮几何断言（本轮）

`KX_UIRenderStats` 新增 `circle_segments` / `circle_radius` /
`circle_radius_deviation`，由 `DrawCircleApprox` 在 GPU 关闭时也记录：

- `circle_segments == 16`：旋钮是 16 边形三角扇；
- `circle_radius`：半径 = `min(knob_w, rect.h) * 0.5`，本测试中滑块高 40 → 20；
- `circle_radius_deviation < 1e-4`：所有扇边顶点到圆心距离与半径一致。

对应断言在 `tests/b661_e2e.cpp` 的 `TestKnob()`，不依赖截图。

**本轮计数**：`c7_ui` **60 PASS / 0 FAIL**（原 55，追加 5 条：两字体解析到不同
BLF id、5 字符竖排 4 条）；`b661` **29 PASS / 0 FAIL**（原 19，追加旋钮 3 条、
Polar 8 元素补 2 条、富文本 5 条）。`tests/run_all_e2e.sh` 的 MANIFEST 已加入
`c7_ui`，不再依赖手工 `/tmp` 编译。

## U-3 多属性同时动画（B6.9-U，本轮）

**先查引擎侧通道**：没有「单节点多属性」节点。每个 `UIAnimation` 节点自带一个
`B6AnimationState` 槽位（`KX_FunctionNodesB6UI.cpp`），因此同一个 widget 上挂三个
`UIAnimation` 节点即可让 position / size / opacity 各自按自己的时长与曲线推进；
本轮**没有**为此新增节点，也没有改动画实现。

新增 E2E（`tests/b64_e2e.cpp`，`BuildMultiAnimGraph` / `CheckMultiAnimations`），
同一 label 上三个独立节点：

| 属性 | 端口类型 | 时长 | 曲线 | 区间 |
|---|---|---|---|---|
| `position` | Vector | 2 s | linear | (0,0) → (200,200) |
| `size`（缩放） | Vector | 4 s | linear | (100,100) → (500,500) |
| `opacity` | Float | 1 s | ease_out | 0 → 1 |

同一帧的独立推进断言：

- **t=0.5 s**：position=(50,50)（2 s 的 1/4）、size=(150,150)（4 s 的 1/8）、
  opacity=0.75（1 s 的 ease_out 中点）；
- **t=1.0 s**：position=(100,100)（2 s 中点）、size=(200,200)（4 s 的 1/4）、
  opacity=1.0（1 s 已完成）——三个属性同帧各自到达自己的预期中间/终值；
- **t=1.25 s**：opacity 的 `OnDone` 已触发（1）而 size 的 `OnDone` 仍为 0，
  证明两个时长独立；
- **t=4.0 s**：size=(500,500) 且 `OnDone`=1，position 仍保持 (200,200)，
  证明先完成的属性不会回退或被后完成的属性覆盖。

曲线：`linear / ease_in / ease_out / ease_in_out` 均已实现（`B6UIEase`），
本轮用 linear + ease_out 交叉验证；**不存在「只做线性插值」的降级**，
因此 `DEFERRED_FOREVER.md` 无需追加曲线条目。

边界（能力粒度，不是近似实现）：单节点一次只写一个属性；三属性并发 = 三个节点。
若将来需要「一个节点配置多条通道」，属于新节点设计，不在本轮范围。

**本轮计数（K 冻结）**：`b64` **94 assertions / 0 failures**（此前 78，追加 16 条多属性断言；
`RUN_ID=20261106-224926`，批 `b64`；旧 `/tmp/e2e_u3/b64.log` 的 78 条已被覆盖）。


