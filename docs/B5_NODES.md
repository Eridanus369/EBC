# B5.2 首批 6 个原生逻辑节点

日期：2026-11-05
范围：`RotateTo`、`MoveTo`、`MouseLook`、`AlignAxisToVector`、`ObjectPropertyOperator`、`SetMaterial`
运行时：`KX_FunctionNodesB5.cpp`（表驱动 + 自适应签名）

## 1. 接入位置

| 层 | 文件 | 位置 |
|---|---|---|
| 运行时实现 | `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB5.cpp` | 新建，6 条 spec：`kB5Specs` 863-891，注册 `RegisterB5FunctionGameNodes` 936-945 |
| 注册声明 | `.../KX_FunctionNode.h` | `RegisterB5FunctionGameNodes()` 236 |
| 启动注册 | `.../KX_RuntimeNodes.cpp` | B4 之后追加调用 239 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | B3 之后追加 197 |
| 适配器接口 | `.../KX_GameAdapters.h` | `SetWorldPosition` 434、`SetWorldOrientation` 441、`SetMaterial` 451（均追加在 `SetLocalScale` 之后） |
| 引擎适配器 | `.../KX_GameAdaptersEngine.cpp` | 930-944 / 985-1003 / 1065-1090 |
| 编辑器表 | `scripts/addons_core/bge_netlogic/editor/nodes/native_b5.py` | `NATIVE_B5` 25-45；动态端口类 89-135 |
| 编辑器加载 | `.../editor/nodes/__init__.py` | `from . import native_b5` 379 |
| 调色板 | `.../ui/node_palette.py` | MouseLook 43、RotateTo 67、MoveTo 68、AlignAxis 69、SetMaterial 70、PropertyOperator 140 |
| 端口契约 | `.../generator/port_contract.py` | 46 / 55 |
| 枚举一致性 | `.../generator/enum_consistency.py` | 46 / 54 |

共享文件均为追加式改动，未重排 P4 与编辑器线已有内容。

## 2. 节点行为

### 变换三件套：RotateTo / MoveTo

端口（入/出）：`OnEnter, Object, Target(Vector), Speed(Float)` / `OnDone`；config `local: Bool`（默认 false）。

- `RotateTo`：`FuncRotateToEnter/Tick` 195/217。目标方向为 `Target - 当前世界位置`（`local=true` 时 `Target` 先经父级旋转再作为方向）。用四元数 `RotationBetween(+Y, direction)`，每帧按 `Speed * dt` 做 slerp；角距 `<= 0.01 rad` 即发 `OnDone`。
- `MoveTo`：`FuncMoveToEnter/Tick` 335/357。`local=true` 时目标按父级位置/旋转/缩放变换回世界空间（`ResolveMoveTarget`）。每帧按 `Speed * dt` 线性插值；剩余距离 `<= 0.01` 时对齐到目标并发 `OnDone`。
- 失败路径：Object 为空、目标 NaN、`Speed <= 0` / 非有限、父级或世界变换不可用，均写 stderr 诊断、立即发 `OnDone` 并置 `done`，不会永久挂起。相关代码 367-405 / 217-260。
- 写回统一走 `KX_ObjectAdapter::SetWorldPosition/SetWorldOrientation`。

### MouseLook

端口：`OnEnter, Sensitivity, MinPitch, MaxPitch` / `OnUpdate`；config `invert_x`、`invert_y`。

- `ResolveMouseLookCamera` 449、`FuncMouseLookEnter/Tick` 458/481。目标优先为 **挂载该树的 owner 对象**，owner 为空时才回退到 `ctx.SceneQuery().ActiveCamera()`。
  - 这一选择修正了 standalone/player 下的实际行为：引擎在无 viewport 相机视图时会使用隐藏的 `__default__cam__` 作为 active camera，直接旋转 active camera 会转错对象。E2E 场景未使用 `SetActiveCamera`，打印的 YAW/PITCH 仍来自 owner `Camera`，证明目标正确。
- 首帧从相机世界欧拉角初始化 `pitch/roll/yaw`；之后每帧读 `ctx.Input().MouseDelta(dx, dy)`：
  - `yaw += sign_x * dx * Sensitivity * deg2rad`
  - `pitch += sign_y * dy * Sensitivity * deg2rad`，并 clamp 到 `[MinPitch, MaxPitch]`（默认 ±89°，上下限颠倒时自动交换）
  - `SetWorldOrientation(camera, {pitch, roll, yaw})`，随后发 `OnUpdate`。
- `invert_x` / `invert_y` 通过 `node.ConfigBool` 取反符号位，两条 E2E 已验证符号翻转。
- 无 owner 且无活动相机时打印一次诊断并跳过该帧。

### AlignAxisToVector

端口：`OnEnter, Object, Axis(Vector), Target(Vector)` / `OnDone`；无 config。

- `FuncAlignAxisToVector` 550。用 `RotationBetween(Axis, Target)` 求把 `Axis` 旋到 `Target` 的最短四元数，再与当前世界旋转合成，经 `SetWorldOrientation` 写回。
- 轴或目标为零向量时由 `RotationBetween` 输出 `[AlignAxisToVector] degenerate vector (axis or target is zero)` 诊断，并照常发 `OnDone`，不崩。

### ObjectPropertyOperator

端口：`OnEnter, Object, Property(String), Op(String), Value(动态)` / `Result(动态), OnDone`；无 config。

- 运行时 `FuncObjectPropertyOperator` 739-837，spec 使用 `ports_from_ir = true`（886-889）。
- 支持 `add/sub/mul/div/min/max`，按 Property 实际类型（Float / Int / Bool 按整数 / Vector）分别处理；除零、未知 op、类型不支持均写明确 stderr 诊断，返回原值并发 `OnDone`，不崩。
- 读写走 `KX_PropertyAccessor`，缓存条目存于 `B5PropertyState`。
- 编辑器侧 `resolve_native_port` 对 `Value`/`Result` 解析上游实际 socket 类型，未连接时回退到被引用 Property 的默认 tag；`Property`/`Op` 固定为 String（native_b5.py 111-135），沿用 B2 的动态端口机制。

### SetMaterial

端口：`OnEnter, Object, Material(String)` / `OnDone`；config `slot: Int`（0 基）。

- 运行时 `FuncSetMaterial` 850-861。
- 适配器新增 `KX_ObjectAdapter::SetMaterial(object, name, slot)`（接口 451，Mock 默认 false），引擎实现 1065-1090：按名 `BKE_libblock_find_name(G.main, ID_MA, name)` 查材质，绑定到 `slot + 1` 的 obdata 材质槽；材质不存在时 stderr 输出 `[SetMaterial] material datablock '<name>' not found on object '<obj>'` 并返回 false。

## 3. 欧拉角约定修正

首次 E2E 暴露出 `GetWorldOrientation` 与 `SetWorldOrientation` 顺序不一致（读回值把 roll/yaw 对调，导致 RotateTo 实际绕错轴、MouseLook 的 yaw 卡在 roll 值上）。已统一为 **Euler(x, y, z)**：

- `GetWorldOrientation` 直接回填 `getEuler` 的 `(x, y, z)`（KX_GameAdaptersEngine.cpp 930-944）；
- `SetWorldOrientation` 保持 `setEuler(value)`（985-1003）；
- `RotateTo` / `AlignAxisToVector` 写回 `{yaw, pitch, roll}`（即 getEuler 的 x/y/z）；
- `MouseLook` 状态为 `pitch=x, roll=y, yaw=z`，写回 `{pitch, roll, yaw}`。

修正后的 RotateTo E2E 同时打印 X/Z，确认只有 Z 分量变化（`ROT_X=0.000`, `Z=-1.571`），排除了旧实现的轴互换。

## 4. 验收证据

| 项 | 结果 | 证据 |
|---|---|---|
| 6/6 注册 | `REGISTERED_COUNT 6 / 6`，`MISSING []`，IR WARN 0 | `/tmp/b52_register.log` |
| `port_contract.py` | `native_nodes=140 catalog_nodes=54 errors=0 warnings=4` | `/tmp/b52_port.log` |
| `enum_consistency.py` | `checked_pairs=43 inconsistencies=0` | `/tmp/b52_enum.log` |
| `ninja -j6` | `BUILD_EXIT=0` | `/tmp/b52_nodes_build.log` |
| E2E RotateTo | `ROTATE_DONE`，`Z=-1.571`，`X=0.000` | `/tmp/b52_rotate_run4.log` |
| E2E MouseLook | 未接 `SetActiveCamera` 仍旋转 owner：yaw 每帧 +0.174533；pitch 在 89° clamp；`invert_x` 使 yaw 反向 | `/tmp/b52_mouse_run9.log`、`/tmp/b52_mouse_inv_run4.log` |
| 回归 B2/B3 | 15 PASS / 0 FAIL | `/tmp/b52_reg_b2b3.log` |
| 回归 B4 | 46 PASS / 0 FAIL；palette 150、17 节点缺失 0 | `/tmp/b52_reg_b4.log`、`/tmp/b52_b4_palette.log` |
| 回归核心层 | 9 PASS / 0 FAIL（Timer/Once/compile/rebuild，`CORE_TIMER_DONE`/`CORE_ONCE` 各一次） | `/tmp/b52_reg_core.log` |

汇总日志：`/tmp/b52_nodes.log`。

### MouseLook E2E 的环境限制

本容器的 `blenderplayer` 为离屏渲染，无 GHOST 窗口，X11 鼠标事件无法送达 `KX_InputState`。因此 MouseLook 的 delta 跟随使用一次性的临时注入钩子（`KX_B5_MOUSE_TEST`）做确定性验证；验证完成后该钩子及相关调试代码已全部删除，并用干净源码重新编译（`BUILD_EXIT=0`）。上面表格中的 MouseLook 数据来自注入运行，干净二进制在 headless 下 delta 恒为 0（`/tmp/b52_mouse_clean2.log`）。真实窗口中不需要该钩子，节点直接消费引擎鼠标 delta。
