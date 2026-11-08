# UI 域重打分（B6.9-U，2026-11-06）

## 0. 新口径（原文照抄）

代码侧成熟度 = 实现完成 + Mock/Headless E2E 通过：

| 子类 | 定义 | 计入分母 | 计入分子 |
|------|------|---------|---------|
| **full** | 实现完成 + Headless/Mock E2E 通过 + 无已知限制 | ✅ | ✅ 1.0 |
| **approximate** | 实现完成 + Headless E2E 通过 + 有明确书面限制 | ✅ | ✅ 0.5 |
| **unsupported** | 引擎层不支持（附 `路径:行号` 可核证据），进 FOREVER | ❌ | — |
| **pending-device** | 实现完成 + Mock/Headless E2E 通过 + 只差真机验证 | ✅ | ✅ 1.0（真机清单单列） |

`pending-device` 三条件：① 有行为测试；② Mock/Headless E2E 通过；③ 阻碍 headless 的是具体设备/驱动依赖且给出 API 位置。

## 1. 证据基线（改动之后的干净树）

本轮不跑整包 `ninja`，只用 `tests/harness_config.sh` 的独立对象缓存
（`HARNESS_OBJ_ROOT=/tmp/harness_obj_u4`）编译 GameNodes TU 与 harness；
**无 shim、无 `HARNESS_DROP_TU`、无 `/tmp` 补丁头文件**。日志目录
`/tmp/e2e_u4_ui`（16:01 生成），归档副本 `/tmp/ui_u4_final`：

| 日志 | 结果 |
|---|---|
| `/tmp/e2e_u4_ui/b64.log` | `b64 94 / 0`（`PASS total failures=0`） |
| `/tmp/e2e_u4_ui/b661.log` | `b661 29 / 0` |
| `/tmp/ui_u4_final/port_contract.log` | `PORT CONTRACT ... errors=0 warnings=4`（4 条仅端口命名注记，非错误） |
| `/tmp/ui_u4_final/enum_consistency.log` | `checked_pairs=122 inconsistencies=3`（3 条均在 AddConstraint / CreateVehicle，见 §6） |
| `/tmp/ui_u4_final/metadata_check.log` | `checked=287 ... port_mismatch=1 config_mismatch=6`（全部在 Files / Network / Input 等其它线节点，见 §6） |

`CreateListView` 已不在 port_contract 或 metadata_check 的任何 mismatch 列表里；
这两份日志里对 `CreateListView` 的命中为 0（端口数 6 == C++ 6）。

旧轮 09:59 那批（`/tmp/e2e_a1g/b64.log` 61 条）只作历史对照，本轮结论不引用；
上一轮 shim 编出的 b64 78/0 继续作废。

## 2. UI 15 节点逐节点表

### B4（9）

| # | 节点 | 分类 | 依据（代码 / E2E） | 限制说明 |
|---|------|------|------------------|---------|
| 1 | `CreateUICanvas` | full | `KX_FunctionNodesB4.cpp:715`；`tests/reg_b4_e2e.cpp:420`（55/0） | 无 |
| 2 | `CreateUILayout` | full | `KX_FunctionNodesB4.cpp:716`；`tests/reg_b4_e2e.cpp:421` | 无 |
| 3 | `CreateUILabel` | full | `KX_FunctionNodesB4.cpp:717`；`tests/reg_b4_e2e.cpp:422`；多字体/竖排/富文本见 `c7_ui.cpp:118-154, 265-275`（60/0） | 真实字形视觉见 §3 真机单列 |
| 4 | `CreateUIImage` | full | `KX_FunctionNodesB4.cpp:718`；`tests/reg_b4_e2e.cpp:423` | 无 |
| 5 | `CreateUIButton` | full | `KX_FunctionNodesB4.cpp:719`；`tests/reg_b4_e2e.cpp:424`；`b64_e2e.cpp:637`（tab 内部按钮） | 无 |
| 6 | `CreateUISliderWidget` | full | `KX_FunctionNodesB4.cpp:721-722`；`tests/reg_b4_e2e.cpp:425`；旋钮几何 `b661_e2e.cpp:175-183`（29/0） | 真实外观见 §3 真机单列 |
| 7 | `MoveWidget` | full | `KX_FunctionNodesB4.cpp:724`；`tests/reg_b4_e2e.cpp:426` | 无 |
| 8 | `SetUIWidgetAttr` | full | `KX_FunctionNodesB4.cpp:725-726`；`tests/reg_b4_e2e.cpp:427` | 无 |
| 9 | `GetUIWidgetAttr` | full | `KX_FunctionNodesB4.cpp:727`；`tests/reg_b4_e2e.cpp:428` | 无 |

B4 节点无真实设备依赖：canvas/layout/widget 的行为全部由 `KX_MockUIAdapter` 记录并断言（`b4_reg` 的 `INFO ui calls canvas=1 layout=1 label=1 image=1 button=1 slider=1 move=11 attr_set=29 attr_get=11` 行）。

### B6（6）

| # | 节点 | 分类 | 依据（代码 / E2E） | 限制说明 |
|---|------|------|------------------|---------|
| 10 | `CreateScrollView` | full | `KX_FunctionNodesB6UI.cpp:954`；`tests/b64_e2e.cpp:536-553`（94/0） | 无 |
| 11 | `CreateListView` | **full** | `KX_FunctionNodesB6UI.cpp:911-918`（含 `Clear` 端口）、`:277-278, 442, 447-462`（锁存 + 边沿清空）；`tests/b64_e2e.cpp:638-681`（94/0，含 3 帧保持 / 切换 / 清空 5 条断言） | 无 |
| 12 | `CreateTabView` | full | `KX_FunctionNodesB6UI.cpp:966`；`tests/b64_e2e.cpp:630-637`（94/0） | 无 |
| 13 | `UIAnimation` | full | `KX_FunctionNodesB6UI.cpp:972`；`tests/b64_e2e.cpp:777-805`（linear/ease_in/ease_out/ease_in_out、取消重入、`OnDone` 一次；94/0） | 多属性并发需多个节点（节点粒度设计，见 `B6_UI_ADVANCED.md` U-3 节） |
| 14 | `UIScreenAnchor` | full | `KX_FunctionNodesB6UI.cpp:978`；`tests/b64_e2e.cpp:652-658`（94/0） | 无 |
| 15 | `UIScreenStack` | **full** | `KX_FunctionNodesB6UI.cpp:801-812`（按图实例槽位 `KX_ExecutionContext::UISharedState()`）、`:960`；`tests/b64_e2e.cpp:807-885`（94/0，双实例隔离 + 热重载迁移 8 条断言） | 无 |

## 3. 三类合计与百分比

| full | approximate | unsupported | pending-device（节点级） | 分母 |
|---:|---:|---:|---:|---:|
| 15 | 0 | 0 | 0 | **15** |

- **严格**（只 full 记 1）：15 / 15 = **100%**
- **加权**（full 1.0、approximate 0.5、pending-device 1.0；unsupported 不进分母）：(15×1.0 + 0×0.5) / 15 = **100%**
- **覆盖率**（15 个节点均有代码位置 + E2E 证据）：15 / 15 = **100%**

### 新旧口径对照

| 口径 | 严格 | 加权 |
|---|---|---|
| 旧（full 记 1、近似记 0） | 13/15 = **86.7%** | — |
| 上一轮（13 个近似 2 个） | 13/15 = **86.7%** | **93.3%** |
| 本轮（补完 2 个近似） | 15/15 = **100%** | **100%** |

结论：**两个 approximate 已补齐，UI 域代码侧 15/15 = 100%（严格与加权相同）**。
提升来源只有 §4 的两项实现变更，没有把任何节点挪出分母。

### pending-device 子项（真机清单，单列，不扣分）

这些是节点内部的具体视觉项，节点本身已 full（计 1.0）；即便整体按 pending-device 记，也仍是 1.0，分数不变。

| 子项 | 所在节点 | ① 行为测试 | ② Headless E2E | ③ 设备依赖 + API 位置 |
|---|---|---|---|---|
| 旋钮真实外观 | `CreateUISliderWidget` | `b661_e2e.cpp:175-183`（16 段 / 半径 / 等半径） | 29/0 | UI canvas 只在 BGE 播放器绘制，headless 无 UI framebuffer；绘制入口 `KX_UIRenderer.cpp:337` `DrawCircleApprox` |
| 多字体真实字形 | `CreateUILabel` | `c7_ui.cpp:118-154`（两字体不同 BLF id） | 60/0 | 字形栅格化需真实 display/GPU；加载 `KX_UIRenderer.cpp:569` `BLF_load_unique` |
| 富文本视觉（颜色/字号） | `CreateUILabel` | `b661_e2e.cpp:238-324`（普通/命名色/hex/嵌套/`{size}` 段） | 29/0 | 实绘 `KX_UIRenderer.cpp:1073` `BLF_draw` |

判定：三条均满足「有行为测试 + headless 通过 + 阻碍是 GPU/display 依赖」，符合 pending-device 定义；真机清单一律放 `REAL_DEVICE_VERIFICATION.md`，本文件不重复。

## 4. 两个原 approximate 已升 full：改了什么、断言怎么证

不是旋钮、不是字体——旋钮/字体所在的节点本来就是 full（视觉项单列真机）。
本轮把此前仅有的两个 approximate 补齐：

### 4.1 `CreateListView` → `full`（原 0.5）

- 实现变更：`OnItemClick` 由「只在点击帧输出」改为**锁存**——点击后索引一直保持，
  直到下一次点击或 `Clear` 触发；`Clear` 为**边沿**触发（false→true 才清空），
  未连线时不会每帧清掉选中项。状态字段 `selected_index` / `clear_was_set`
  （`KX_FunctionNodesB6UI.cpp:277-278`），写入 `:442`，清空 `:447-460`，输出 `:462`。
  spec 追加 `Clear` 触发端口：`KX_FunctionNodesB6UI.cpp:911-918`；
  编辑器表同步 `native_b6ui.py:59-61`，metadata `native_metadata_gen.py:465-481`。
- E2E 断言原文（`tests/b64_e2e.cpp:638-681`，日志 `/tmp/e2e_u4_ui/b64.log:13-17`）：
  - `selection stays latched after 3 idle frames (actual=3 expected=3)`；
  - `virtual list latches the same index after 3 idle frames (actual=3 expected=3)`；
  - `a later click updates the latched index (actual=1 expected=1)`；
  - `Clear resets the selection to -1 (actual=-1 expected=-1)`；
  - `the cleared selection stays -1 on later frames (actual=-1 expected=-1)`。
- 文档旧描述已改：`B6_UI_ADVANCED.md` 的 CreateListView 节不再写「只在点击帧」。

### 4.2 `UIScreenStack` → `full`（原 0.5）

- 实现变更：进程级 `static` 栈改为**按图实例**持有——栈对象挂在
  `KX_ExecutionContext` 的不透明槽位（`UISharedState()`，
  `KX_ExecutionContext.h` 追加声明、`KX_ExecutionContext.cpp` 追加定义），
  同一张图的多个 push/pop/replace 节点仍共用一份栈，不同图实例各自独立
  （`KX_FunctionNodesB6UI.cpp:801-812`）。
- 热重载迁移：`KX_GraphInstance::Rebuild` 只替换 `graph_`/`pending_`，
  复用同一个 `context_`（`KX_GraphInstance.cpp:63-112`），因此栈深度与栈顶
  句柄在重建后原样保留，无需额外迁移表。不存在架构阻碍，故按 full 判。
- E2E 断言原文（`tests/b64_e2e.cpp:807-885`，日志 `/tmp/e2e_u4_ui/b64.log:36-46`）：
  - `instance B push does not hide instance A's screen`；
  - `instance B pop hides only its own screen`；
  - `instance A still shows its screen after B pops`；
  - `hot reload rebuild accepts the graph`；
  - `rebuild does not recreate canvases (actual=5 expected=5)`；
  - `rebuild keeps the stack (top still IndA2)`；
  - `pop after rebuild returns to the screen below`。
  其中「两实例互不干扰」由前三条独立成立（单独即可 full）；「重建前后元素数
  与栈顶一致」由后三条成立。
- 文档旧描述已改：`B6_UI_ADVANCED.md` 的 UIScreenStack 节改为按实例持有 +
  热重载保留，限制小节不再保留旧条目。

## 5. UI 相关 FOREVER 项确认（不进分母）

分母是 15 个节点，能力边界条目本来就不是节点；下面再给「不进机器区间/不进任何可用率分母」的可核位置：

| 项 | 位置 | 仍在 FOREVER 的理由 |
|---|---|---|
| 粗体 / 斜体 / 下划线 | `DEFERRED_FOREVER.md:275-276` | BLF 无按 run 切字形 API；用几何偏移/描边冒充属近似，明确不做，按普通文本输出 |
| 描边 / 渐变（见上条同段） | `DEFERRED_FOREVER.md:276` | 同上，属「冒充近似」被明确拒绝的能力 |
| per-run 字体族 | `DEFERRED_FOREVER.md:277-278` | 富文本只做 color + size；跨字体 run 的基线/行高需排版引擎，字体切换只在 label 级成立 |
| `{size:N}` 排版级行高 | `DEFERRED_FOREVER.md:279-280` | 只改字形大小，行高仍由元素 `font_size` / `line_height` 决定 |
| 竖排 + 富文本标记 | `DEFERRED_FOREVER.md:281-282` | 竖排按 UTF-8 字符逐行断行，不剥离行内标记；组合不支持，需先关闭竖排 |
| 字形覆盖检测 | `DEFERRED_FOREVER.md:283` | 缺 CJK 字形交给 BLF 回退链，不做覆盖检查/告警 |

机器侧可核性：

- 排除区间为**第一对** `excluded:start/end`，物理位置 `DEFERRED_FOREVER.md:185-243`；`_forever_ids()` 只切这一段（`scripts/startup/bge_gamenodes/generator/native_audit.py:78, 96-108`）。
- UI 文本边界小节在 `DEFERRED_FOREVER.md:258`，位于结束标记之后；正则只认 `\b(NL[A-Za-z0-9_]+|LogicNode[A-Za-z0-9_]+)\b`，本节**零命中**。
- 本轮实测（只读解析，不改文件）：区间内 **57** 个 id，`233 = 290 − 57` 与 semantic **360** 不变；§7 无任何 id 可被吞入。
- `DEFERRED_FOREVER.md:284-286` 的旋钮真机截图说明不是 FOREVER，归 §3 的 pending-device 单列。

本轮**没有**任何 approximate 节点：列表与屏幕栈已按 §4 升 full；上面 FOREVER
条目仍不进分母，也不因本轮变更而改变。

范围澄清：`DEFERRED_FOREVER.md:129` 的混音总线、`:43` / `:68` 的材质节点树属音频/渲染域，均**不是 UI 节点**，不进入本文件的 15 节点分母。

## 6. 本轮边界与其它线漂移

- 未修改 `NUMBER_CONSISTENCY_AUDIT.md` 附录 B（编号权归 K 线，K 单点重写）。
- 未改任何 TOTAL / 百分比文档；本文件仍是独立重打分表。
- 未跑整包 `ninja`；未用 shim / `HARNESS_DROP_TU` / `/tmp` 补丁头文件；
  未 commit / push；未碰 7 个未跟踪 `_lib.glsl`；未删除 `build_linux`。
- `enum_consistency` 的 3 条不一致全部是其它线在途改动：
  `AddConstraint` 的 `axis`/`pivot`（`KX_FunctionNodesB6.cpp:209-210`）、
  `CreateVehicle` 的 `suspension`（`KX_FunctionNodesB6.cpp:147`）。
  本轮没有给 B6UI 增加任何 config 键或枚举值，故与本轮变更无关。
- `metadata_check` 的 `port_mismatch`/`config_mismatch` 命中
  `TouchInput` 与 `Gamepad*`/`InputMap`/`Sequence`/`Vibrate` 等 Input 线节点，
  `unknown` 21 项均为 Files/Network/Playlist 线新增节点；`CreateListView`
  已从所有 mismatch 列表中消失（6 == 6）。
- `b661` 29/0、`port_contract` errors=0 与本轮前一致，无退化。
