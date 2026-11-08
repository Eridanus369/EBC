# 输入域重打分（B6.9-I 收口）

> 本文件是输入域在新口径下的唯一打分依据。所有数字只在本文件登记，
> **不往 `NUMBER_CONSISTENCY_AUDIT.md` 附录 B 追加任何编号**（该附录由 K 线单点重写）。

## 新口径（原文）

代码侧成熟度 = 实现完成 + Mock/Headless E2E 通过：

- **full**：分母计入，分子 1.0
- **approximate**：分母计入，分子 0.5（必须有书面明确限制）
- **unsupported**：引擎层不支持，附 `路径:行号` 可核证据，进 FOREVER，不进分母
- **pending-device**：分母计入，分子 1.0，真机待验单列

pending-device 必须同时满足三条：① 节点有行为测试；② Mock/Headless E2E 通过；③ 阻碍 headless
验证的是具体设备/驱动依赖并给出 API 位置——不是「我们没测」。

每个域**并排给两个数**：严格口径（pending-device 不计分）与新口径，不许只报高的。

## 结论（并排两个数）

| 口径 | 分子 / 分母 | 输入域 |
|---|---|---|
| 旧严格（上一轮 `B-36` 报法：已实现即计 full，未区分 E2E / 真机） | 23 / 26 | **88.5%** |
| 新严格（unsupported 剔除分母、pending-device 计 0） | 16 / 23 | **69.6%** |
| 新口径（pending-device 计 1.0） | 23 / 23 | **100%** |

关键结论：上一轮的 56.5%（13/23）在本轮补完 P0 接线与 8 条节点级行为断言后变为
**16 full + 7 pending-device = 23/23**。分子增加的 10 个节点全部有可核断言：
2 个走引擎适配器路径（`b68`），8 个走节点行为路径（`b67`）。严格口径仍只算 16 个
本机可完整复现的节点（**69.6%**），7 个 pending-device 的真实手柄/马达仍未验。
旧 88.5% 的虚高来源不变：它把 5 个 pending-device、2 个适配器未接线、8 个无行为 E2E
一律算成了 full。

## 逐节点表

节点清单 = `CAPABILITY_WEIGHT_AUDIT.md` §2.4 的 26 节点（core 9 + B6 11 + Extra 5 + B5 1）。

| # | 节点 | 分类 | 依据（`路径:行号` / 日志行） | 限制说明 |
|---|---|---|---|---|
| 1 | Keyboard | full | 注册 `KX_FunctionNode.cpp:1725`；行为 `tests/b5_rerun/p3_input_harness.cpp:281-283`、断言 `:332-358`；日志 `run_20261106-132736/b5_p3_input.log`（53/0） | 无 |
| 2 | MouseButton | full | 注册 `KX_FunctionNode.cpp:1726`；行为 `p3_input_harness.cpp:284-285`、断言 `:332-358` | 无 |
| 3 | MousePosition | full | 注册 `KX_FunctionNode.cpp:1727`；行为 `p3_input_harness.cpp:286`；断言 x=120 / y=240 | 无 |
| 4 | MouseDelta | full | 注册 `KX_FunctionNode.cpp:1728`；行为 `p3_input_harness.cpp:287`；断言 5 / -3 | 无 |
| 5 | MouseWheel | full | 注册 `KX_FunctionNode.cpp:1729`；行为 `p3_input_harness.cpp:288`；断言 2 | 无 |
| 6 | JoystickAxis | pending-device | 注册 `KX_FunctionNode.cpp:1730`；实现 `:232-235`；**引擎 override** `KX_GameAdaptersEngine.cpp:367-375`（`DEV_Joystick::GetAxisPosition` / 32767）；节点侧 `tests/b66_uncovered_e2e.cpp:231`、`:297-328`；引擎路径 `tests/b68_input_engine_e2e.cpp`，日志 `run_20261106-162500/b68_input_engine.log:10`（12 PASS / 0 FAIL） | headless 用 SDL3 虚拟手柄注入 16000/32767，引擎适配器读到 0.488296；真实手柄的轴符号 / 范围 / 死区未验。测试用 `KX_INPUT_TEST_HOOKS` 注入 `DEV_Joystick*`，`KX_Scene` 的 `JOY_EVENTMGR` 查找路径未跑 |
| 7 | JoystickButton | pending-device | 注册 `KX_FunctionNode.cpp:1731`；实现 `:239-244`；**引擎 override** `KX_GameAdaptersEngine.cpp:376-397`（读 `aButtonPressIsPositive`，`EndFrame` `:287-296` 跟踪上一帧）；节点侧 `b66_uncovered_e2e.cpp:235`、`:297-328`；引擎路径 `tests/b68_input_engine_e2e.cpp`，日志 `b68_input_engine.log:11-16` | 虚拟手柄按下 → Hold/Press 为 true、Press 是边沿、Release 为 true；真实手柄按键映射未验，同上 `JOY_EVENTMGR` 查找路径未跑 |
| 8 | SetMousePosition | full | 注册 `KX_FunctionNode.cpp:1732`；实现 `:252-261`；引擎已接线 `KX_GameAdaptersEngine.cpp:396-406`；行为 `tests/b67_input_nodes_e2e.cpp`，日志 `run_20261106-161500/b67_input_nodes.log:2-6`（49 PASS / 0 FAIL） | 断言适配器收到 (0.5, 0.25) 且 `OnDone` 只触发一次 |
| 9 | SetCursorVisible | full | 注册 `KX_FunctionNode.cpp:1733`；实现 `:1715-1722`；引擎已接线 `KX_GameAdaptersEngine.cpp:408-416`；行为 `tests/b67_input_nodes_e2e.cpp`，日志 `b67_input_nodes.log:7-10` | 断言适配器收到 `Visible=true` 且 `OnDone` 触发 |
| 10 | GamepadButton | pending-device | 注册 `KX_FunctionNodesB6Input.cpp:478`；行为 `tests/b62_e2e.cpp`；日志 `run_20261106-141602/b62.log:4,13-14`；引擎 `KX_GameAdaptersEngine.cpp:418-425` | 依赖真实手柄；headless 无 `DEV_Joystick` 实例 |
| 11 | GamepadAxis | pending-device | 注册 `B6Input.cpp:484`；日志 `b62.log:7-8,15,17`；引擎 `KX_GameAdaptersEngine.cpp:449-460` | 同上；轴 0-2 走 `DEV_Joystick::GetAxisPosition` |
| 12 | GamepadTrigger | pending-device | 注册 `B6Input.cpp:485`；日志 `b62.log:9,16,18`；引擎 `KX_GameAdaptersEngine.cpp:462-471` | 同上；扳机轴 4/5 |
| 13 | TouchInput | unsupported | 注册 `B6Input.cpp:486`；引擎 `KX_GameAdaptersEngine.cpp:473-480`、`:482-485`；Device 层无触摸类（见 §3.7） | 引擎层无触摸设备接口；FOREVER，不进分母 |
| 14 | TouchGet | unsupported | 注册 `B6Input.cpp:487`；引擎 `KX_GameAdaptersEngine.cpp:473-485`；Device 层无触摸类 | 同上 |
| 15 | GamepadCount | pending-device | 注册 `B6Input.cpp:488`；日志 `b62.log:19`；引擎 `KX_GameAdaptersEngine.cpp:438-447`（`DEV_Joystick::GetInstance` 探测） | 依赖真实手柄枚举 |
| 16 | TouchGesture | unsupported | 注册 `B6Input.cpp:489`；实现读 `GetTouchCount` / `GetTouch`（`KX_FunctionNodesB6Input.cpp:309-347`）；引擎 `KX_GameAdaptersEngine.cpp:473-485` | 与 13/14 同一套缺失的触摸设备；mock E2E（`b62.log:20-29`）只证明节点语义 |
| 17 | KeyCombo | full | 注册 `B6Input.cpp:494`；日志 `b62.log:37`；引擎 `KX_GameAdaptersEngine.cpp:487-520` | 无 |
| 18 | InputMap | full | 注册 `B6Input.cpp:500`；日志 `b62.log:38-39`；引擎 `KX_GameAdapters.cpp` 的 `SetInputMap` / `ReadInputMap` | 无 |
| 19 | GetInputValue | full | 注册 `B6Input.cpp:501`；日志 `b62.log:3,5-6` | 无 |
| 20 | VibrateGamepad | pending-device | 注册 `B6Input.cpp:506`；日志 `b62.log:33`；引擎 `KX_GameAdaptersEngine.cpp:522-541`；设备 `DEV_JoystickVibration.cpp:51`（`RumblePlay`）、`:236`（`GetRumbleSupport`） | 真实马达/手感未验；API 已存在 |
| 21 | MouseData | full | 注册 `KX_FunctionNodesExtra.cpp:743`；实现 `:319-334`（聚合 `MousePosition` / `MouseDelta` / `MouseWheel`）；行为 `tests/b67_input_nodes_e2e.cpp`，日志 `b67_input_nodes.log:11-20` | 7 个输出（Position/Movement/X/Y/DeltaX/DeltaY/Wheel）逐项断言 |
| 22 | KeyPressed | full | 注册 `KX_FunctionNodesExtra.cpp:764`；实现 `:581-585`；行为 `tests/b67_input_nodes_e2e.cpp`，日志 `b67_input_nodes.log:21-26` | 断言 KeyCode 透传、Press 是边沿、Release 不误报 |
| 23 | MousePressed | full | 注册 `KX_FunctionNodesExtra.cpp:765`；实现 `:589-594`；行为 `tests/b67_input_nodes_e2e.cpp`，日志 `b67_input_nodes.log:27-32` | 断言 Button 透传、Hold 每帧触发、松开即停 |
| 24 | MouseMoved | full | 注册 `KX_FunctionNodesExtra.cpp:766`；实现 `:598-604`；行为 `tests/b67_input_nodes_e2e.cpp`，日志 `b67_input_nodes.log:33-37` | 断言零位移不触发、水平/垂直位移触发 |
| 25 | SetMousePositionLegacy | full | 注册 `KX_FunctionNodesExtra.cpp:754`；实现 `:431-438`；行为 `tests/b67_input_nodes_e2e.cpp`，日志 `b67_input_nodes.log:38-42` | 断言 X/Y 原值透传（0.3 / 0.7）且 `OnDone` 触发 |
| 26 | MouseLook | full | 注册 `KX_FunctionNodesB5.cpp:876-881`；实现 `:481`（`FuncMouseLookTick`）；行为 `tests/b67_input_nodes_e2e.cpp`，日志 `b67_input_nodes.log:43-50` | 断言 pitch/yaw 换算、roll 不变、MaxPitch 89° 钳制、`OnUpdate` 触发 |

## 分类合计

| 分类 | 数量 | 进分母 | 计分 | 节点 |
|---|---:|---|---|---|
| full | 16 | 是 | 1.0 | Keyboard, MouseButton, MousePosition, MouseDelta, MouseWheel, KeyCombo, InputMap, GetInputValue, SetMousePosition, SetCursorVisible, MouseData, KeyPressed, MousePressed, MouseMoved, SetMousePositionLegacy, MouseLook |
| approximate | 0 | — | 0.5 | 无 |
| pending-device | 7 | 是 | 1.0（新）/ 0（严格） | GamepadButton, GamepadAxis, GamepadTrigger, GamepadCount, VibrateGamepad, JoystickAxis, JoystickButton |
| 适配器未接线（残余类） | 0 | — | — | 已清零（`b68` 引擎路径断言） |
| 无行为 E2E（残余类） | 0 | — | — | 已清零（`b67` 节点行为断言） |
| unsupported | 3 | 否（FOREVER） | — | TouchInput, TouchGet, TouchGesture |
| **合计** | **26** | **分母 = 23** | — | — |

> 「适配器未接线」「无行为 E2E」是四类口径**没有槽位**的两种残余状态。本轮两者都已清零：
> `JoystickAxis` / `JoystickButton` 补了引擎 override，按口径第③条（阻碍进一步验证的是
> 真实手柄，不是「没测」）落为 `pending-device`；另外 8 个节点补了节点级行为断言，落为 `full`。

## 旧 88.5% 与新口径的对照

| 旧分子 23 个 full 的真实构成 | 数量 | 新口径归类 |
|---|---:|---|
| 真正实现 + 行为 E2E 通过 | 8 | full |
| 有 mock E2E、但真机未验 | 5 | pending-device |
| 节点有 mock E2E、引擎适配器未接线 | 2 | 适配器未接线（0） |
| 实现存在、完全无节点级 E2E | 8 | 无行为 E2E（0） |
| 旧报法另计 1 个 "mock" | 1 | `TouchGesture` → unsupported |
| 旧报法另计 2 个 unsupported | 2 | `TouchInput` / `TouchGet` → unsupported |

- 旧分母 26（含 3 个触摸节点）；新分母 23（3 个触摸节点进 FOREVER）。
- 旧分子 23 = 8 + 5 + 2 + 8。
- **本轮补完后的新分子 = 16 full + 7 pending-device = 23**（上一轮为 13 = 8 + 5）。
  分子增加的 10 个：2 个适配器未接线 → pending-device，8 个无行为 E2E → full。
- 严格口径分子 16（pending-device 按纪律计 0）。
- **结论：88.5% → 69.6%（严格）/ 100%（新口径）。**

### 本轮档位变化（前 → 后）

| 节点 | 上一轮档次 | 本轮档次 | 依据 |
|---|---|---|---|
| JoystickAxis | 适配器未接线（0） | pending-device（1.0 新 / 0 严格） | `KX_GameAdaptersEngine.cpp:367-375` override + `b68_input_engine.log:10`（引擎路径读到 0.488296） |
| JoystickButton | 适配器未接线（0） | pending-device（1.0 新 / 0 严格） | `KX_GameAdaptersEngine.cpp:376-397` override + `b68_input_engine.log:11-16`（Hold/Press/Release 全断言） |
| SetMousePosition | 无行为 E2E（0） | full | `b67_input_nodes.log:2-6` |
| SetCursorVisible | 无行为 E2E（0） | full | `b67_input_nodes.log:7-10` |
| MouseData | 无行为 E2E（0） | full | `b67_input_nodes.log:11-20` |
| KeyPressed | 无行为 E2E（0） | full | `b67_input_nodes.log:21-26` |
| MousePressed | 无行为 E2E（0） | full | `b67_input_nodes.log:27-32` |
| MouseMoved | 无行为 E2E（0） | full | `b67_input_nodes.log:33-37` |
| SetMousePositionLegacy | 无行为 E2E（0） | full | `b67_input_nodes.log:38-42` |
| MouseLook | 无行为 E2E（0） | full | `b67_input_nodes.log:43-50` |

分子 13/23（56.5%）→ 23/23（100%）；分母不变（26 − 3 FOREVER = 23）。

## 3.7 判定：`TouchInput` / `TouchGet` = `unsupported`

**结论：不是 `pending-device`，是 `unsupported`。** 判定依据不是「headless 没有触摸屏」，
而是**引擎层根本不存在触摸设备接口**，没有可等待的设备/驱动。

可核证据：

1. `source/gameengine/Device/` 的完整类清单只有
   `DEV_EventConsumer`、`DEV_InputDevice`、`DEV_Joystick`、`DEV_JoystickEvents`、
   `DEV_JoystickPrivate`、`DEV_JoystickVibration` —— **没有任何触摸设备类**。
2. 在 `source/gameengine/Device/` 全目录 grep `Touch|touch|TOUCH`，唯一命中是
   `DEV_JoystickDefines.h:40`：
   `#define JOYBUT_MAX 22 /* SDL_GAMEPAD_BUTTON_COUNT ... (added MISC1, PADDLE1-4, TOUCHPAD) */`
   —— 这是 SDL 手柄的「触摸板按钮」常量，不是触摸屏 API。
3. 在 `source/gameengine/Device/` grep `Finger|finger`：**0 命中**。
4. `KX_GameAdaptersEngine.cpp:473-480` 的 `GetTouch` 直接返回全 0 / `down=false`，
   注释原文：「The Ketsji input layer has no touch device wired up in this build.」。
5. `KX_GameAdaptersEngine.cpp:482-485` 的 `GetTouchCount` 直接 `return 0`。
6. `source/gameengine/Ketsji/` 里 `Touch` 的命中只有物理碰撞事件
   `TOUCH_EVENTMGR` / `ST_TOUCH`（`KX_CollisionEventManager.cpp:42`、`:128`），与输入设备无关。

与 `pending-device` 的边界：

- `pending-device` 的第三条要求是「阻碍 headless 验证的是具体设备/驱动依赖并给出 API 位置」。
- `VibrateGamepad` 满足：真实 API 存在（`DEV_JoystickVibration.cpp:51` `RumblePlay`、
  `:236` `GetRumbleSupport`），只是没有马达硬件。
- `TouchInput` / `TouchGet` / `TouchGesture` **不满足**：没有 `DEV_Touch*` 类、没有
  `GetTouch` 的设备实现，`EngineInputAdapter` 的返回值是硬编码空值。没有「接口存在但没设备」
  这一层，只有「接口不存在」。→ `unsupported`，进 FOREVER，不进分母。

连带修正：旧报法把 `TouchGesture` 计为 "mock verified"，但它的输入完全来自
`GetTouchCount` / `GetTouch`（`KX_FunctionNodesB6Input.cpp:309-347`），与 `TouchInput` 同源。
本轮改判 **unsupported**，所以触摸类是 **3 个**，不是 2 个。节点侧实现与 mock E2E
（`b62.log:20-29`）仍然保留，未来补设备层后可直接复活；但当前不计分。

## 真机待验项小表（供 `REAL_DEVICE_VERIFICATION.md`）

| # | 项目 | 涉及节点 | 当前分类 | 真机要验什么 | 设备 / 驱动 API 位置 | 处理 |
|---|---|---|---|---|---|---|
| 1 | 手柄震动 | VibrateGamepad | pending-device | 强度映射、时长、左右马达、`RumbleStop` | `DEV_JoystickVibration.cpp:51` `RumblePlay`；`:236` `GetRumbleSupport`；`:204` `RumbleStop` | 列入真机清单（扩充原第 2 项） |
| 2 | 手柄轴 / 按钮 / 扳机 / 数量 | GamepadButton, GamepadAxis, GamepadTrigger, GamepadCount, JoystickAxis, JoystickButton | pending-device | 轴符号与范围、扳机 0..1、死区手感、枚举数量；legacy 轴量程 32767、按键边沿 | `KX_GameAdaptersEngine.cpp:418-471`（gamepad）、`:367-397`（legacy joystick）、`:438-447`；`DEV_Joystick.h` | 列入真机清单（原第 2 项，本轮增加 legacy 两项） |
| 3 | 触摸多点 / 手势 | TouchInput, TouchGet, TouchGesture | unsupported（FOREVER） | 无设备接口，当前无法验；需先补 `DEV_Touch` 设备层 | `source/gameengine/Device/` 无触摸类；`KX_GameAdaptersEngine.cpp:473-485` | **不是真机待验项**；`REAL_DEVICE_VERIFICATION.md` 第 1 项的 `pending real device` 应改为 `blocked（引擎无触摸设备 API）`（本文件只登记结论，文档由 K 统一改） |
| 4 | 触摸压力 | TouchInput Pressure0/1、TouchGet Pressure | unsupported（FOREVER） | 同上；`GetTouchPressure` 基类也恒 0（`KX_GameAdapters.h:170-174`） | 同第 3 项 | 同第 3 项 |

> 说明：`JoystickAxis` / `JoystickButton` 的引擎 override 已补齐并有 headless 引擎路径断言
> （`b68`），因此**可以进真机清单**。真机要验的是轴符号/量程、按键映射、`JOY_EVENTMGR`
> 下的实际设备枚举（headless 测试注入的是 `DEV_Joystick*`，未走场景事件管理器查找）。

## 数字与日志路径

| 项 | 值 | 来源 |
|---|---|---|
| 输入节点数 | 26 | `CAPABILITY_WEIGHT_AUDIT.md` §2.4（core 9 + B6 11 + Extra 5 + B5 1） |
| full / approximate / pending-device / unsupported | 16 / 0 / 7 / 3 | 本文件逐节点表 |
| 残余类：适配器未接线 / 无行为 E2E | 0 / 0 | 本轮 P0 + 8 条行为断言清零 |
| 分母 | 23 | 26 − 3（FOREVER） |
| 旧严格 | 88.5%（23/26） | `NUMBER_CONSISTENCY_AUDIT.md` B-36 |
| 新严格 | 69.6%（16/23） | 本文件 |
| 新口径 | 100%（23/23） | 本文件 |
| `b62` | **134 PASS / 0 FAIL** | K 冻结 `RUN_ID=20261106-224926`（批 `b62`）；旧 `run_20261106-141602` 的 99 项已被覆盖 |
| `b66` | 20 PASS / 0 FAIL | `tests/.e2e_out/run_20261106-132736/b66_uncovered.log` |
| `b5_p3_input` | 53 PASS / 0 FAIL | `tests/.e2e_out/run_20261106-132736/b5_p3_input.log` |
| `b67`（8 节点行为断言） | 49 PASS / 0 FAIL | `tests/.e2e_out/run_20261106-161500/b67_input_nodes.log`；构建 `b67.build.log` 中 `grep -c 'error:'` = 0 |
| `b68`（引擎适配器接线） | 12 PASS / 0 FAIL | `tests/.e2e_out/run_20261106-162500/b68_input_engine.log` |

## 待办（本轮不计分，供补齐阶段）

| 优先级 | 事项 | 影响 | 依据 |
|---|---|---|---|
| ~~P0~~ 已完成 | `EngineInputAdapter` 补 `JoystickAxis` / `JoystickButton` override（`DEV_Joystick::GetAxisPosition` / `aButtonPressIsPositive`） | 2 个节点 → pending-device，新口径 56.5% → 65.2% | `KX_GameAdaptersEngine.cpp:367-397`；`b68_input_engine.log:10-16` |
| ~~P1~~ 已完成 | 8 个无 E2E 节点补节点级行为断言（Mock/Headless） | 8 个节点 → full，新口径 65.2% → 100% | `tests/b67_input_nodes_e2e.cpp`；`b67_input_nodes.log`（49/0） |
| P2 | 真实手柄 / 马达验证 7 个 pending-device | 严格口径从 69.6% → 100% | §真机待验项小表 |
| FOREVER | 触摸设备层（`DEV_Touch` + `EngineInputAdapter::GetTouch` 实装） | 3 个 `unsupported` 才有可能复活 | §3.7 |

> **口径边界声明**：本轮新口径到 23/23 = 100%，但严格口径只有 16/23 = 69.6%。
> 差的 7 个全部是 `pending-device`（5 个 gamepad + 2 个 legacy joystick），它们
> 满足新口径三条：有行为断言、headless 通过、阻碍进一步验证的是真实手柄/马达硬件
> 并有 API 位置。**这不等于真机已验**；`REAL_DEVICE_VERIFICATION.md` 的触摸条目
> 本文件只登记改判结论（`blocked（引擎无触摸设备 API）`），文档本身已按 K 冻结值同步（`RUN_ID=20261106-224926`）。
