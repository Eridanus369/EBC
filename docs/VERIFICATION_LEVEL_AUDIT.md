# B 类 49 条验证级别审计

日期：2026-11-05
范围：`CAPABILITY_WEIGHT_AUDIT` 中「疑似 mock-only 90 条」里的 B 类 49 条
性质：**证据审计**（只读生产源码与构建目录）。后续为修正断言计数口径而改动的
`tests/harness_config.sh` / `tests/README.md` 属测试脚本，不涉及生产代码；本节结论
不因脚本修订而改变。

## 1. 结论

| 分类 | 数量 | 占比 |
|---|---|---|
| mock-driven（只有 Mock / harness 内 test double） | 49 | 100% |
| engine-driven（真实 `KX_*AdapterEngine` / 真实 `KX_Scene` / `KX_GameObject` / 真实文件系统） | 0 | 0% |
| 未覆盖（没有行为断言） | 0 | 0% |

**`engine-driven = 0` 是本审计最强的结论，它决定 A/B 两类的证据层级都是 Mock。**
B 类并不存在「文档漏写的真机覆盖」；因此把 A+B 全降为 mock 之后，历史情景「73.8%」
不可能再被恢复，该数字已被取代，只能作为阶段记录。当前有效口径、各项百分比与
未覆盖归零后的对外值，**见 `CAPABILITY_WEIGHT_AUDIT` §13**；本审计不复制
具体数值，以免口径更新后立即过期。

### 1.1 审计链路盲区：注册 ≠ 验证

最初被判为「未覆盖」的 5 条，共同特征是**「注册表里有，但全仓库没有任何 type_id
引用」**。这说明：

- `port_contract` 只校验端口契约，`enum_consistency` 只校验枚举一致性；
- 两者**都不会告诉任何人「这个节点从没被执行过」**。

因此「注册数」不能当作「已验证数」。任何审计都必须同时找到 `SetAdapters` 装配点
和读取输出的 `Check`，否则只能算「存在」，不能算「验证」。

**最有力的实例（比「5 条没断言」严重）**：`ProximitySensor` 在补断言时发现
`docs/B6_SENSOR.md` §2 端口表与 §8 语义**与实现不符**（详见 §1.3）。只读文档 + 只读
注册表会得到一个**错误的语义**；必须跑出实际帧数（4 tick → `OnNear` 下游写 4 次）
才能发现。前者只是覆盖缺口，后者是知识错误——这正是本审计要求「注册、装配、读输出
三者缺一都不算覆盖」的原因。

### 1.2 未覆盖名单：5 → 5 已覆盖 / 0 待补

5 条全部补齐，未覆盖归零。

| 节点 | 状态 | 证据 |
|---|---|---|
| `JSONStringify` | **已覆盖（mock-driven）** | 与 `JSONParse` 合计 **10 条读输出端口的值断言**，覆盖嵌套对象、数组、引号 / 反斜杠 / 换行的逐字节往返；`/tmp/reg_b4_json_assertions.log` 声明 `assertions=55` 全 PASS；源码 `tests/reg_b4_e2e.cpp:437-473` |
| `JSONParse` | **已覆盖（mock-driven）** | 同上；输出必须被 `ReadOutput` 读取并比对，不再是「执行了就算过」 |
| `ProximitySensor` | **已覆盖（mock-driven）** | 10 条值/触发断言：`/tmp/b66_uncovered_assertions.log`；`FakeScene` + `FakeObjects` + `CountingFileAdapter`；4 tick → `OnNear` 下游写 4 次 |
| `JoystickAxis` | **已覆盖（mock-driven）** | 5 条：`KX_MockJoystickInputAdapter`（测试内 override `JoystickAxis`）；0.6 / -0.8 / [-1,1] |
| `JoystickButton` | **已覆盖（mock-driven）** | 4 条：测试内 override `JoystickButton`（`KX_MockInputAdapter` 基类不实现这两个虚函数，只能在测试里注入） |

**设计边界（图使用者需要知道）**：JSON 节点的 schema 只接受 `string|float|int|bool`，
**嵌套对象 / 数组没有结构化端口**。因此上述往返验证是把 JSON 文本放在 string 字段里
做逐字节比对，而不是按对象结构逐字段断言。

### 1.3 事实纠正：ProximitySensor 的端口与触发语义

补断言时推翻了先前（包括派工方）的假设，以下以**实现**为准：

1. **端口**：`Object`(Object) + `Distance`(Float) → `OnNear` / `OnFar` /
   `DistanceValue`(Float)（`KX_FunctionNodesB6Sensor.cpp:400-408`）。不是
   `Detected / 对象名 / 方向`；走 `ctx.SceneQuery().QueryProximity(...)` 的是
   **`RadarExtended`**（其断言早已存在）。
2. **每帧求值、逐帧电平，不是边沿触发**：实测 4 帧 → `OnNear` 下游写入 4 次；
   实现里没有 `InTrigger`，编译器把「无触发输入、有触发输出」的节点放进
   `trigger_starts`（`KX_GraphCompiler.cpp:878-883`）。
3. **`docs/B6_SENSOR.md` 原文与实现不符**：§2 端口表写作 `OnEnter, Object, Distance`，
   §8 写「`OnEnter` 触发帧求值、不逐帧轮询」——均与上述实现相反；该文档已由
   另一条线订正。

## 2. 判据

逐条检查 harness 的装配代码，而不是注释：

- 命中的是 `KX_AdapterSet` 哪个槽位、赋给哪个实现：
  `input` / `scene` / `physics` / `objects` / `ui` / `file` / `control` / `message`。
- 只有以下算 `engine-driven`：
  - `KX_*AdapterEngine`（如 `KX_NetworkAdapterEngine`）或 `KX_CreateEngine*Adapter()`；
  - 真实 `KX_Scene` / `KX_GameObject`（非 `reinterpret_cast` 假指针）；
  - 真实文件系统后端（非 `KX_MockFileAdapter`）。
- `KX_Mock*Adapter` 与 harness 内自写的适配器子类（`FakeScene` / `FakeObjects` /
  `FakePhysics` / `ScaleScene` / `TestSceneAdapter` / `TestObjectAdapter` / `MockScene` /
  `QueryScene`）一律算 `mock-driven`。

补充：`tests/b45_gui.py` 是编辑器面板与检视器截图测试（`bpy` 节点树 UI），不执行节点图，
因此不作为 engine-driven 证据（其运行期日志 / 截图仍写 `/tmp/b45_*`，属允许的运行期产物）。

判定红线：**注册表命中 ≠ 验证**——必须同时看到 `SetAdapters` 装配点与读输出的
`Check`，否则只能算「存在」，见 §1.1。

## 3. 分组明细

### 3.1 输入 9（`tests/b62_e2e.cpp`）

装配：`input = KX_MockInputAdapter`（`b62_e2e.cpp:585`，`SetAdapters` 181-182）。
日志：`/tmp/a1_clean_run_b62_e2e.log`，68 条断言 / 0 failures（69 行 `PASS`，末行为汇总行）；K 冻结 `RUN_ID=20261106-224926` 后批 `b62` 为 **134/0**。

| 节点 | 分类 | 断言位置 |
|---|---|---|
| GamepadButton | mock-driven | MakeNode 208/211/214；Check 617/669/671/673 |
| GamepadAxis | mock-driven | 231；Check 635/639 |
| GamepadTrigger | mock-driven | 234；Check 644 |
| VibrateGamepad | mock-driven | 255；Check 667（`vibrate_calls`） |
| KeyCombo | mock-driven | 217；Check 674 |
| InputMap | mock-driven | 241；Check 683 |
| GetInputValue | mock-driven | 250；Check 610/623/625 |
| JoystickAxis | mock-driven | `tests/b66_uncovered_e2e.cpp`；`KX_MockJoystickInputAdapter` override `JoystickAxis`（163-169）；Check 263（注册）+ 298/310/319/321，共 5 条 |
| JoystickButton | mock-driven | 同上 override `JoystickButton`（171-176）；Check 265（注册）+ 301/313/327，共 4 条 |

### 3.2 传感器 7

| 节点 | 分类 | harness / 槽位实现 / 断言 |
|---|---|---|
| RadarExtended | mock-driven | `b63_nodes_e2e.cpp:449`；`scene=FakeScene`、`physics=FakePhysics`、`objects=FakeObjects`、`ui=KX_MockUIAdapter`（声明 423-426，`SetAdapters` 512）；Check 529-530。**真正走 `QueryProximity(...)` 的是本节点**（不是 `ProximitySensor`） |
| LineOfSight | mock-driven | `b63_nodes_e2e.cpp:453-454`；同上 `FakeScene` / `FakePhysics` / `FakeObjects`；Check 531-532 |
| AreaSensor | mock-driven | `b63_nodes_e2e.cpp:457`；`FakeScene`；Check 533 |
| RaycastSensor | mock-driven | `b63_nodes_e2e.cpp:460`；`FakePhysics`；Check 534 |
| Radar | mock-driven | `reg_b2_b3.cpp:160`；`input=MockInput`、`scene=MockScene`（`KX_SceneAdapter` 子类，216）；Check 246 |
| Touch | mock-driven | `reg_b2_b3.cpp:162`；同 `MockScene`；Check 247 |
| ProximitySensor | mock-driven | `tests/b66_uncovered_e2e.cpp`；`scene=FakeScene`、`objects=FakeObjects`（106-140）、`file=CountingFileAdapter`；Check 261（注册）+ 331/333/335/343/345/347/356/358/360，共 10 条。端口 `Object`+`Distance` → `OnNear`/`OnFar`/`DistanceValue`（`KX_FunctionNodesB6Sensor.cpp:400-408`） |

`c32_nodes_e2e.cpp:811` 是 `b63_nodes_e2e.cpp` 传感器段的拷贝，使用同一套 `FakeScene` /
`FakeObjects` / `FakePhysics`（定义 619/643/690），视为重复覆盖。

### 3.3 B4 UI 9（`tests/reg_b4_e2e.cpp`）

装配：`ui = KX_MockUIAdapter`（215，`SetAdapters` 218-219）。
日志：`/tmp/reg_b4_json_assertions.log`，`assertions=55` 全 PASS（与 §3.4 共用同一份日志；
其中 10 条为 JSON 读输出值断言）。

`CreateUICanvas`(230)、`CreateUILayout`(231)、`CreateUILabel`(232)、`CreateUIImage`(233)、
`CreateUIButton`(234)、`CreateUISliderWidget`(235)、`MoveWidget`(236)、
`SetUIWidgetAttr`(237)、`GetUIWidgetAttr`(238)：**9/9 mock-driven**。

### 3.4 B4File 8（`tests/reg_b4_e2e.cpp`）

装配：`file = KX_MockFileAdapter`（216，纯内存）。

| 节点 | 分类 | 断言位置 |
|---|---|---|
| SaveGame | mock-driven | 239/245（`save_calls` + 槽位持久化） |
| LoadGame | mock-driven | 240 |
| ReadFile | mock-driven | 241 |
| WriteFile | mock-driven | 242 |
| FileExists | mock-driven | 243 |
| DeleteFile | mock-driven | 244 |
| JSONStringify | mock-driven（**已补值断言**） | 图节点 207；10 条读输出端口断言见 442-472；逐字节往返 |
| JSONParse | mock-driven（**已补值断言**） | 图节点 214/248/268/287/303；同上 442-472 |

后两条已从「未覆盖」移出：新增 **10 条读输出端口的值断言**，覆盖嵌套对象、数组、
引号 / 反斜杠 / 换行的逐字节往返（`/tmp/reg_b4_json_assertions.log`，`assertions=55`）。
边界：JSON schema 只接受 `string|float|int|bool`，嵌套对象 / 数组没有结构化端口，
因此验证是把 JSON 文本放进 string 字段做逐字节比对。

前 6 条 `mock-driven` 的断言主要是「适配器被调用 / 槽位内容」，不是真实文件系统行为。

### 3.5 legacy 16

14 条 `tests/b65a_e2e.cpp`：`scene=TestSceneAdapter`、`objects=TestObjectAdapter`（harness 内自写）、
`control=KX_MockSceneControlAdapter`、`message=KX_MockMessageAdapter`（362-364）。
日志 29 条断言 / 0 failures（30 行 `PASS`，末行为汇总行）。

| 节点 | 分类 | 断言 |
|---|---|---|
| TimerLegacy | mock-driven | 388 |
| QuitGameLegacy | mock-driven | 406 |
| ClearParentLegacy | mock-driven | 434/436/438 |
| MessageReceivedLegacy | mock-driven | 482/483/485/487 |
| ObjectByNameLegacy | mock-driven | 531 |
| GetObjectLegacy | mock-driven | 535 |
| SimpleValueLegacy | mock-driven | 540/543 |
| TimeDataLegacy | mock-driven | 549/551/553 |
| SetMaterialLegacy | mock-driven | 580/582/583/585 |
| PulsifyLegacy | mock-driven | 618 |
| BarrierLegacy | mock-driven | 619/630 |
| EvaluatePropertyLegacy | mock-driven | 665/667 |
| ModifyPropertyLegacy | mock-driven | 697 |
| AlignAxisToVectorLegacy | mock-driven | 735 |

2 条 `tests/b661_query_e2e.cpp`：`scene=QueryScene`（`KX_SceneAdapter` 子类，313），
`KX_GameObject` 为 `reinterpret_cast` 假指针（0x100/0x200/0x210），`SetAdapters` 469。
日志 41 PASS / 0 failures。

| 节点 | 分类 | 断言 |
|---|---|---|
| WorldPositionLegacy | mock-driven | 425-427；Check 487-498 |
| TouchLegacy | mock-driven | 538；Check 588-604 |

## 4. 对迁移统计的影响

1. **`engine-driven = 0` 是本审计最强的结论**：B 类与 A 类同属 Mock 证据层级，
   没有「真机覆盖」可以回收。历史情景「73.8%」已被 `CAPABILITY_WEIGHT_AUDIT` §13
   的新口径取代，只能作为阶段记录；本审计不重复其具体百分比。
2. 未覆盖名单已由 **5 → 5 已覆盖 / 0 待补**；对外值见 `CAPABILITY_WEIGHT_AUDIT` §13
   （本审计不复制）。5 条全部计入 mock-verified：
   - `JSONStringify`、`JSONParse`（10 条读输出值断言）；
   - `ProximitySensor`、`JoystickAxis`、`JoystickButton`（20 条，`/tmp/b66_uncovered_assertions.log`）。
3. 若要提升可信度，优先级应是：
   - 49 条整体升级到 `engine-driven`（真实引擎绑定）仍是唯一的实质提升路径；
   - 语义类断言（如 `ProximitySensor` 的逐帧电平）应固化为回归项，防止文档再次漂移；
   - 按 §1.1 的红线检查新增节点：注册、装配、读输出三者缺一不算覆盖。

> 注意：`JSONStringify` / `JSONParse` 的覆盖依据是「注册表 + 图内执行」之外的第三种：
> **有 `SetAdapters` 装配、有 `Check` 读输出**。三者缺一都不算覆盖（见 §1.1）。

## 5. 证据文件

- `/tmp/b_class_audit.log`（逐条明细与本表同源）
- `/tmp/reg_b4_json_assertions.log`（JSON 10 条读输出值断言，`assertions=55` 全 PASS）
- `/tmp/b66_uncovered_assertions.log`（Proximity + 双 Joystick 共 20 条，`INFO assertions=20`、
  `RESULT failures=0`）
- `/tmp/a1_clean_run_b62_e2e.log`、`a1_clean_run_reg_b2_b3.log`、`a1_clean_run_reg_b4_e2e.log`、
  `a1_clean_run_b65a_e2e.log`、`a1_clean_run_b661_query_e2e.log`
- 源码：`b62_e2e.cpp`、`b63_nodes_e2e.cpp`、`c32_nodes_e2e.cpp`、`reg_b2_b3.cpp`、
  `reg_b4_e2e.cpp`、`b65a_e2e.cpp`、`b661_query_e2e.cpp`
