# B6.2 输入扩展（11 节点）

日期：2026-11-05（2026-11-06 增补 B6.9-I：手柄死区 / 数量、触摸压力与多指手势、映射优先级）
范围：`GamepadButton`、`GamepadAxis`、`GamepadTrigger`、`TouchInput`、`TouchGet`、`GamepadCount`、`TouchGesture`、`KeyCombo`、`InputMap`、`GetInputValue`、`VibrateGamepad`
运行时：`KX_FunctionNodesB6Input.cpp`（表驱动，`kB6InputSpecs`）

## 1. 接入位置

| 层 | 文件 | 位置 |
|---|---|---|
| 运行时实现 | `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Input.cpp` | 新建；节点实现 61-472，spec 表 477-545，注册 551 |
| 注册声明 | `.../KX_FunctionNode.h` | `RegisterB6InputFunctionGameNodes()` 239 |
| 启动注册 | `.../KX_RuntimeNodes.cpp` | B5 之后追加调用 240 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | `GameNodes/KX_FunctionNodesB6Input.cpp` 199 |
| 适配器接口 | `.../KX_GameAdapters.h` | `KX_InputAdapter` 65-215；`KX_MockInputAdapter` 227-297 |
| 适配器实现 | `.../KX_GameAdapters.cpp` | 死区/解析助手 843-910，`SetInputMap` 925，`ReadInputMap` 937，mock 实现 1030-1210 |
| 引擎适配器 | `.../KX_GameAdaptersEngine.cpp` | `EngineInputAdapter` 240-506 |
| 编辑器表 | `scripts/startup/bge_gamenodes/editor/nodes/native_b6b.py` | `NATIVE_B6B` 68 起 11 条 |
| 调色板 | `.../ui/node_palette.py` | 11 条输入条目 |

共享文件均为追加式改动，未重排既有代码块。

## 2. 节点

| 节点 | kind | 输入 | 输出 | config |
|---|---|---|---|---|
| GamepadButton | CON | — | OnTrigger(Trigger), IsDown(Bool) | `pad:int`, `button:enum`, `edge:enum`, `deadzone:float` |
| GamepadAxis | PAR | — | X(Float), Y(Float) | `pad:int`, `axis:enum`, `deadzone:float` |
| GamepadTrigger | PAR | — | Value(Float) | `pad:int`, `trigger:enum`, `deadzone:float` |
| TouchInput | PAR | — | Count(Int), Position0(Vector2), Position1(Vector2), Down0(Bool), Pressure0(Float), Pressure1(Float), Down1(Bool) | — |
| TouchGet | PAR | Index(Int) | Position(Vector2), Pressure(Float), Down(Bool) | — |
| GamepadCount | PAR | — | Count(Int) | — |
| TouchGesture | PAR | — | PinchScale(Float), Rotation(Float), Active(Bool) | — |
| KeyCombo | CON | — | OnTrigger(Trigger), IsDown(Bool) | `keys:string`, `edge:enum` |
| InputMap | ACT | OnEnter, ActionName(String), Binding(String) | OnDone | `priority:int` |
| GetInputValue | PAR | ActionName(String) | Value(Float), IsDown(Bool) | — |
| VibrateGamepad | ACT | OnEnter, Pad(Int), Intensity(Float), Duration(Float) | OnDone | `deadzone:float` |

`deadzone` 默认 `0.0`，即不启用（既有图行为不变）。`VibrateGamepad` 的 `deadzone` 表示**最小强度**：`Intensity <= deadzone` 时不发震动请求，但 `OnDone` 照常触发。

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
- **死区（B6.9-I）**：`GamepadAxis` 用径向死区——`mag = |(x,y)|`，`mag <= deadzone` 时两轴都输出 0，否则整体按 `(mag - dz) / (1 - dz)` 线性重映射，方向不变；`GamepadTrigger` 用标量死区，`value <= dz` 输出 0，否则 `(value - dz) / (1 - dz)`。`GamepadButton` 比较适配器给出的模拟值（`GetGamepadButtonValue`，数字手柄为 0/1），`value > dz` 才算按下；因此死区 > 0 时真实数字按键（1.0）仍然有效。默认 `0.0` 时与改造前完全一致。
- **手柄数量**：`GamepadCount` 调 `KX_InputAdapter::GetGamepadCount()`；引擎侧在 `0..JOYINDEX_MAX-1` 逐槽调用 `DEV_Joystick::GetInstance()` 统计非空设备（`KX_GameAdaptersEngine.cpp` 390-401），mock 侧由注入过的 pad 推导（`SetGamepadCount` 可显式覆盖）。
- `VibrateGamepad`：找不到设备或设备不支持 rumble 时写 stderr 诊断并返回 false，节点同样写 `[VibrateGamepad]` 诊断；节点仍发 `OnDone`，不会挂起（`KX_GameAdaptersEngine.cpp` 474-492）。
- 触摸：Ketsji 侧没有触摸设备，引擎适配器 `GetTouch`/`GetTouchCount` 返回空（count=0，坐标 0，down=false），`GetTouchPressure` 返回 0（`KX_GameAdaptersEngine.cpp` 425-438）。**这是当前已知限制**，移动端接入需要新增设备层；headless/mock 侧可完整注入（`SetTouch`、`touch` 稀疏向量、`pressure`）。
- **多点手势**：`TouchGesture` 在**两指同时按下**的第一帧记录基线（指间距离与角度），随后输出 `PinchScale = 当前距离 / 基线距离`、`Rotation = 当前角度 - 基线角度`（度，归一到 `(-180, 180]`）；任一手指抬起即清空基线，下一次双指接触重新从 1 / 0 开始。第三指及以上的触点只影响 `TouchInput.Count`，不改变手势基线。
- `TouchGet`：按 `Index` 随机访问任一触点（`0` 起），输出 `Position(Vector2)`、`Pressure(Float)`、`Down(Bool)`；越界下标由适配器返回 0/0/`false`，负下标按 0 处理。`TouchInput` 固定暴露前两指，第三指及以上用 `TouchGet` 读取。
- `KeyCombo`：`"ctrl+shift+a"` 按 `+` 拆分，全部键按住才为 true。键名解析支持 `a-z`、`0-9`、`space/enter/return/esc/escape/tab/shift/ctrl/control/alt/up/down/left/right`，未知键名返回 false。

## 5. InputMap 绑定语法

`InputMap` 把 `ActionName → Binding` 存进 `KX_InputAdapter::input_map_`，`GetInputValue` 通过 `ReadInputMap(name, value, down)` 读取（`KX_GameAdapters.cpp` 955-1080）：

```
key:<code>                 键盘电平，value 1/0
keycombo:<keys>            组合键电平，value 1/0
mouse:<button>             鼠标键电平，value 1/0
gamepad_button:<pad>:<n>[:<deadzone>]   手柄按钮电平，value 0..1
gamepad_axis:<pad>:<n>[:<deadzone>]     摇杆，value 为重映射后的 X 分量
gamepad_trigger:<pad>:<n>[:<deadzone>]  扳机，value 0..1
```

- 可选第三段 `deadzone` 默认 0；语义与 §4 相同（按钮按模拟值比较，轴径向、扳机标量）。旧的两段绑定完全兼容。
- **优先级**：`InputMap` 的 `priority`（默认 0）参与同名动作仲裁；高优先级覆盖低优先级，**同优先级仍是后写覆盖先写**（与原行为一致）。低优先级的映射不会覆盖已存在的高优先级映射，也不会被后续低优先级写入冲掉。
- 名字未注册或绑定格式非法时 `ReadInputMap` 返回 false（不打印诊断）；`GetInputValue` 每个节点实例只警告一次。
- **首帧可见**：`GetInputValue` 在查找失败且 `ActionName` 非空时，会同步扫描本图所有 `InputMap` 节点，读取其已求值的 `ActionName` / `Binding` 端口并立即 `SetInputMap`，然后再查一次（`KX_FunctionNodesB6Input.cpp` 221-252）。因此 OnInit 触发的映射在**第一帧**就返回正确值，不再有“第二帧才生效”的延迟。前提是绑定来源是常量值节点（StringConst 等）；若绑定本身来自需要触发才能产生的动态值，仍会回退到动作节点执行时序。

## 6. 验证

- 编译：`flock /tmp/build_linux.lock -c 'ninja -C build_linux ...KX_GameAdaptersEngine.cpp.o'` exit=0（2026-11-06）。
- E2E：`tests/run_all_e2e.sh --only b62` **134 项断言 0 失败**（K 冻结 `RUN_ID=20261106-224926`，批 `b62`；旧 `run_20261106-141602` 的 99 项已被覆盖），覆盖：

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
| `GamepadButton` 死区内 0.3 < 0.4 不按下、0.8 按下 | PASS |
| `GamepadAxis` 死区 0.2：0.1 → 0，0.6 → 0.5（线性重映射） | PASS |
| `GamepadTrigger` 死区 0.2：0.1 → 0，0.75 → 0.6875 | PASS |
| `GamepadCount` 检测到 2 个 pad | PASS |
| `VibrateGamepad` 强度 0.2 低于最小强度 0.5 时不请求（调用次数仍为 1） | PASS |
| 两指基线 scale=1 / rotation=0；张开到 0.3 → scale=1.5 | PASS |
| 旋转 90° → `Rotation=90` | PASS |
| 三指 `Count=3` 且手势仍 Active | PASS |
| `TouchGet(2)` 位置 0.8 / 压力 0.2 / down=true | PASS |
| `TouchInput` Pressure0=0.6 / Pressure1=0.9 / Down1=true | PASS |

- 注册：`native_b6b.py` 与 C++ `kB6InputSpecs` 11 条一致；metadata 生成 `325 nodes`（本轮 +3）；`port_contract.py` `native_nodes=244 errors=0 warnings=4`、`enum_consistency.py` `checked_pairs=125 inconsistencies=0`。

## 7. 限制

1. 引擎触摸无实现（见 §4），仅 mock 可注入；真机多点/压力已登记 `DEFERRED_FOREVER.md`。
2. 手柄只按 `pad` 索引取设备，`GamepadCount` 是逐槽探测的当前快照，没有热插拔回调；设备号变化需由上层重新绑定。
3. `InputMap` 同名动作有了优先级仲裁，但同优先级仍是后写覆盖先写，且不做冲突警告；绑定仍是静态字符串。
4. 键名字符串解析是白名单式映射，未覆盖全部键盘键码。

## 8. 旧 `JoystickAxis` / `JoystickButton` 的输出验证

这两个是核心表节点（`KX_FunctionNode.cpp:1300-1301`），不属于 §2 的十个输入节点。`KX_MockInputAdapter` 只实现了 gamepad 接口，**没有 override** `JoystickAxis`/`JoystickButton`；基类默认返回 `0.0f`/`false`（`KX_GameAdapters.h:114-124`），且 mock 没有 `SetJoystick*` 注入字段。

- 注入方式：从 `KX_MockInputAdapter` 派生子类并 override 两个虚函数（[tests/b66_uncovered_e2e.cpp](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/b66_uncovered_e2e.cpp) 的 `KX_MockJoystickInputAdapter`），不改生产代码与适配器头文件；复现入口 `tests/run_all_e2e.sh --only b66_uncovered`。
- 断言（`/tmp/b66_uncovered_assertions.log`，2026-11-05，`INFO assertions=20`、`RESULT failures=0`），均读节点输出端口：
  - `JoystickAxis` 5 条：注册；未注入时 `Value=0`；注入 0.6 后 `Value=0.6`；注入 -0.8 后 `Value=-0.8`；两值均落在 `[-1,1]`。
  - `JoystickButton` 4 条：注册；未注入时 `Active=false`；按下时 `Active=true`；松开后 `Active=false`。
