# B6.2 输入扩展（8 节点）

日期：2026-11-05
范围：`GamepadButton`、`GamepadAxis`、`GamepadTrigger`、`TouchInput`、`KeyCombo`、`InputMap`、`GetInputValue`、`VibrateGamepad`
运行时：`KX_FunctionNodesB6Input.cpp`（表驱动，`kB6InputSpecs`）

## 1. 接入位置

| 层 | 文件 | 位置 |
|---|---|---|
| 运行时实现 | `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Input.cpp` | 新建；节点实现 54-259，spec 表 263-289，注册 330-340 |
| 注册声明 | `.../KX_FunctionNode.h` | `RegisterB6InputFunctionGameNodes()` 239 |
| 启动注册 | `.../KX_RuntimeNodes.cpp` | B5 之后追加调用 240 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | `GameNodes/KX_FunctionNodesB6Input.cpp` 199 |
| 适配器接口 | `.../KX_GameAdapters.h` | `KX_InputAdapter` 追加 151-198；`KX_MockInputAdapter` 208-240 |
| 适配器实现 | `.../KX_GameAdapters.cpp` | `ReadInputMap` 799、mock 实现 885-1013 |
| 引擎适配器 | `.../KX_GameAdaptersEngine.cpp` | `EngineInputAdapter` 328-425 |
| 编辑器表 | `scripts/addons_core/bge_netlogic/editor/nodes/native_b6b.py` | `NATIVE_B6B` 68 起 8 条 |
| 调色板 | `.../ui/node_palette.py` | 8 条输入条目 |

共享文件均为追加式改动，未重排既有代码块。

## 2. 节点

| 节点 | kind | 输入 | 输出 | config |
|---|---|---|---|---|
| GamepadButton | CON | — | OnTrigger(Trigger), IsDown(Bool) | `pad:int`, `button:enum`, `edge:enum` |
| GamepadAxis | PAR | — | X(Float), Y(Float) | `pad:int`, `axis:enum` |
| GamepadTrigger | PAR | — | Value(Float) | `pad:int`, `trigger:enum` |
| TouchInput | PAR | — | Count(Int), Position0(Vector2), Position1(Vector2), Down0(Bool) | — |
| KeyCombo | CON | — | OnTrigger(Trigger), IsDown(Bool) | `keys:string`, `edge:enum` |
| InputMap | ACT | OnEnter, ActionName(String), Binding(String) | OnDone | — |
| GetInputValue | PAR | ActionName(String) | Value(Float), IsDown(Bool) | — |
| VibrateGamepad | ACT | OnEnter, Pad(Int), Intensity(Float), Duration(Float) | OnDone | — |

枚举取值（Python `values` ↔ C++ `ConfigInt`）：

- `button`：`a=0, b=1, x=2, y=3, back=4, guide=5, start=6, left_stick=7, right_stick=8, left_bumper=9, right_bumper=10, dpad_up=11, dpad_down=12, dpad_left=13, dpad_right=14`
- `axis`：`left=0, right=1`；`trigger`：`left=0, right=1`
- `edge`：`press=0, release=1, hold=2`

## 3. 边沿语义

条件节点走 P3.5 pending 触发机制：`needs_tick=true`，每帧在 `TickStatefulNodes` 轮询适配器电平，与 `last_down` 比较后调用 `node.PendingTrigger(ctx, 0)`，由 `PropagateTriggers` 统一派发（`KX_FunctionNodesB6Input.cpp` 61-87 / 96-121）。

- `press`：`down && !last_down`，按住期间只触发一次。
- `release`：`!down && last_down`，抬起帧触发一次。
- `hold`：`down`，按住期间每帧触发。

三种边沿完全区分，不会退化成“按下就一直触发”。`IsDown` 输出每帧同步刷新。

## 4. 手柄 / 触摸 / 组合键

- 引擎手柄经 `SCA_LogicManager::FindEventManager(SCA_EventManager::JOY_EVENTMGR)` 取 `SCA_JoystickManager`，再 `GetJoystickDevice(pad)`。按钮使用 `aButtonPressIsPositive(button)` 兼容不同平台的极性；轴 `axis*2`/`axis*2+1` 除以 32767；扳机轴 4/5，`clamp(0..1)`（`KX_GameAdaptersEngine.cpp` 328-360）。
- 无手柄时 `IsGamepadButtonDown` 返回 `false`、轴/扳机返回 0，不崩溃、不伪造按下。
- `VibrateGamepad`：找不到设备或设备不支持 rumble 时写 stderr 诊断并返回 false，节点同样写 `[VibrateGamepad]` 诊断；节点仍发 `OnDone`，不会挂起（`KX_GameAdaptersEngine.cpp` 410-425）。
- 触摸：Ketsji 侧没有触摸设备，引擎适配器 `GetTouch`/`GetTouchCount` 返回空（count=0，坐标 0，down=false），`KX_GameAdaptersEngine.cpp` 361-374。**这是当前已知限制**，移动端接入需要新增设备层；headless/mock 侧可完整注入（`SetTouch`、`touch` 稀疏向量）。
- `KeyCombo`：`"ctrl+shift+a"` 按 `+` 拆分，全部键按住才为 true。键名解析支持 `a-z`、`0-9`、`space/enter/return/esc/escape/tab/shift/ctrl/control/alt/up/down/left/right`，未知键名返回 false。

## 5. InputMap 绑定语法

`InputMap` 把 `ActionName → Binding` 存进 `KX_InputAdapter::input_map_`，`GetInputValue` 通过 `ReadInputMap(name, value, down)` 读取（`KX_GameAdapters.cpp` 799-884）：

```
key:<code>                 键盘电平，value 1/0
keycombo:<keys>            组合键电平，value 1/0
mouse:<button>             鼠标键电平，value 1/0
gamepad_button:<pad>:<n>   手柄按钮电平，value 1/0
gamepad_axis:<pad>:<n>     摇杆，value 为 X 分量
gamepad_trigger:<pad>:<n>  扳机，value 0..1
```

- 名字未注册或绑定格式非法时 `ReadInputMap` 返回 false（不打印诊断）；`GetInputValue` 每个节点实例只警告一次。
- **首帧可见**：`GetInputValue` 在查找失败且 `ActionName` 非空时，会同步扫描本图所有 `InputMap` 节点，读取其已求值的 `ActionName` / `Binding` 端口并立即 `SetInputMap`，然后再查一次（`KX_FunctionNodesB6Input.cpp` 221-252）。因此 OnInit 触发的映射在**第一帧**就返回正确值，不再有“第二帧才生效”的延迟。前提是绑定来源是常量值节点（StringConst 等）；若绑定本身来自需要触发才能产生的动态值，仍会回退到动作节点执行时序。

## 6. 验证

- 编译：`ninja -C build_linux -j6` exit=0（`/tmp/b62_build2.log`）。
- E2E：`tests/run_all_e2e.sh --only b62` 52 项断言全 PASS（日志 `/tmp/b62_e2e.log`），覆盖：

| 断言 | 结果 |
|---|---|
| 手柄按钮注入 → `IsDown=true` | PASS |
| Press/Release/Hold 触发次数 = 1/1/2 | PASS |
| `KeyCombo` ctrl+shift+a 触发一次 | PASS |
| 摇杆 (0.5, -0.25)、扳机 0.75 | PASS |
| 触摸 count=2、Position0.x=0.25、Down0=true | PASS |
| `InputMap` OnDone 触发一次 | PASS |
| `GetInputValue` **第一帧**返回 1/true（无 “not registered” 提示） | PASS |
| `VibrateGamepad` 到达适配器 1 次 | PASS |

- 注册：`native_b6b.py` 27 行与 C++ 27 条 spec 完全一致，`MISSING []`；`enum_consistency.py` `checked_pairs=63 inconsistencies=0`。

## 7. 限制

1. 引擎触摸无实现（见 §4），仅 mock 可注入。
2. 手柄只按 `pad` 索引取设备，没有设备热插拔回调；设备号变化需由上层重新绑定。
3. `InputMap` 绑定是“静态字符串”，没有运行期冲突检测或优先级；同名动作后写覆盖先写。
4. 键名字符串解析是白名单式映射，未覆盖全部键盘键码。

## 8. 旧 `JoystickAxis` / `JoystickButton` 的输出验证

这两个是核心表节点（`KX_FunctionNode.cpp:1300-1301`），不属于 §2 的 B6.2 八节点。`KX_MockInputAdapter` 只实现了 gamepad 接口，**没有 override** `JoystickAxis`/`JoystickButton`；基类默认返回 `0.0f`/`false`（`KX_GameAdapters.h:114-124`），且 mock 没有 `SetJoystick*` 注入字段。

- 注入方式：从 `KX_MockInputAdapter` 派生子类并 override 两个虚函数（[tests/b66_uncovered_e2e.cpp](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/b66_uncovered_e2e.cpp) 的 `KX_MockJoystickInputAdapter`），不改生产代码与适配器头文件；复现入口 `tests/run_all_e2e.sh --only b66_uncovered`。
- 断言（`/tmp/b66_uncovered_assertions.log`，2026-11-05，`INFO assertions=20`、`RESULT failures=0`），均读节点输出端口：
  - `JoystickAxis` 5 条：注册；未注入时 `Value=0`；注入 0.6 后 `Value=0.6`；注入 -0.8 后 `Value=-0.8`；两值均落在 `[-1,1]`。
  - `JoystickButton` 4 条：注册；未注入时 `Active=false`；按下时 `Active=true`；松开后 `Active=false`。
