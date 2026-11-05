# B6.4 范围 A：UI 高级节点

本批新增 6 个 UI 高级节点，全部走现有 `KX_UIAdapter` / `KX_UIRenderer`，
没有新增渲染路径，也没有绕过适配器直接调用 GPU。

| # | 节点 | 端口（输入 / 输出） | config |
|---|---|---|---|
| 1 | `CreateScrollView` | OnEnter, Parent, Pos, Size / Done, View, Content | `direction`: vertical \| horizontal \| both |
| 2 | `CreateListView` | OnEnter, Parent, Pos, Size, Items(List) / Done, ListView, OnItemClick(Int) | `item_height`, `virtualize` |
| 3 | `CreateTabView` | OnEnter, Parent, Pos, Size, TabNames(List) / Done, TabView, OnTabChanged(Int) | 无 |
| 4 | `UIAnimation` | OnEnter, Widget, Duration(Float), Property(String), FromValue, ToValue / OnDone | `easing`: linear \| ease_in \| ease_out \| ease_in_out |
| 5 | `UIScreenAnchor` | OnEnter, Widget / OnResize, Width(Float), Height(Float) | `anchor`: 9 个位置 |
| 6 | `UIScreenStack` | OnEnter, ScreenName(String) / OnDone | `mode`: push \| pop \| replace |

实现文件：

- `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6UI.cpp`
- 注册声明 `KX_FunctionNode.h`，注册调用在 `KX_RuntimeNodes.cpp`
- 编辑器表 `scripts/addons_core/bge_netlogic/editor/nodes/native_b6ui.py`

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
- 没有点击时 `OnItemClick` 输出 `-1`（每帧刷新，消费者应在同帧读取）。

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
- 屏幕栈是共享 UI 状态：多个 push/pop/replace 节点共用同一份栈
  （`B6ScreenStackShared()`），否则每个节点各持一份栈就无法互相 pop。
  代价是它是进程级状态，热重载不迁移；文档在此标注。

## 验证

- `ninja -C build_linux -j6` → `/tmp/b64_build_final.log`，exit 0。
- `port_contract.py`：errors=0，`/tmp/b64_port_contract_final.log`。
- `enum_consistency.py`：`checked_pairs=71 inconsistencies=0`，
  `/tmp/b64_enum_consistency_final.log`。
- 注册检查：UI 6/6，`MISSING [] EXTRA []`，`/tmp/b64_registration_final.log`。
- E2E（`tests/b64_e2e.cpp`，日志 `/tmp/b64_e2e.log`）：61 项断言覆盖滚动拖动与
  边界 clamp、列表点击 virtualize 一致、Tab 切换、四种 easing 中间值、锚点
  随窗口变化、屏幕栈 push/pop 句柄有效、渲染器裁剪计数。

## 限制

- `CreateListView` 的 `OnItemClick` 只在点击那一帧有效（下一帧重置为 -1）。
- `UIScreenStack` 使用进程级共享栈，同时运行多个图时共享同一 UI 栈；
  热重载不会清空或迁移该栈。
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

