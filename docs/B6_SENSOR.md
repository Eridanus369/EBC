# B6.3 传感器扩展（6 节点）

日期：2026-11-05
范围：`RadarExtended`、`LineOfSight`、`AreaSensor`、`RaycastSensor`、`Collision` 扩展、`ProximitySensor`
运行时：`KX_FunctionNodesB6Sensor.cpp`（表驱动，`kB6SensorSpecs`）
Collision 扩展：`KX_FunctionNode.cpp`

## 1. 接入位置

| 项 | 文件 | 位置 |
|---|---|---|
| 节点实现 | `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Sensor.cpp` | RadarExtended 89-129、LineOfSight 142-237、AreaSensor 249-357、RaycastSensor 371-399、ProximitySensor 410-435、表 436-486、注册 487 |
| 注册声明 | `.../KX_FunctionNode.h` | `RegisterB6SensorFunctionGameNodes()` 251 |
| 启动注册 | `.../KX_RuntimeNodes.cpp` | 244 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | 226 |
| Collision 端口 | `.../KX_FunctionNode.cpp` | 端口 864、接触点/法线/冲击速度计算 888-938、追加输出写入 940-947 |
| 旧图兼容（编译器） | `.../KX_GraphCompiler.cpp` | 仅在签名 `allow_fewer_outputs=true` 时允许 IR 少声明尾部输出 280-292；动态输出越界保护 453 |
| 编辑器表 | `scripts/addons_core/bge_netlogic/editor/nodes/native_b6c.py` | `NATIVE_B6C` 49-119 |

## 2. 节点与端口

| 节点 | 输入 | 输出 | config |
|---|---|---|---|
| RadarExtended | OnEnter | OnFound, FoundObjects(List), Closest(Object), Distance(Float) | `axis_x`/`axis_y`/`axis_z`、`radius`、`angle`、`property` |
| LineOfSight | OnEnter, Target(Object), IgnoreMask(Int) | OnVisible, OnHidden, Distance(Float) | 无 |
| AreaSensor | OnEnter, AreaName(String) | OnEnterArea, OnLeaveArea, Occupants(List) | 无 |
| RaycastSensor | OnEnter, Direction(Vector), Length(Float) | OnHit, OnMiss, HitObject, HitPoint(Vector) | 无 |
| Collision（扩展） | 原端口不变 | 原 3 触发 + ContactPoint(Vector) + ContactNormal(Vector) + ImpactForce(Float) | 无 |
| ProximitySensor | Object, Distance(Float) | OnNear, OnFar, DistanceValue(Float) | 无 |

## 3. RadarExtended 与已有 Radar 的差异

| | 已有 `Radar`（B3） | `RadarExtended`（B6.3） |
|---|---|---|
| 触发模型 | 每帧 Tick，范围内有目标即点亮 `OnDetect` | `OnEnter` 驱动，一次查询一次触发 |
| 圆心/轴向 | `Object`/`Axis` 端口，可逐实例配置 | 固定用 `ctx.Owner()`，轴/半径/角度/属性走节点 config |
| 输出 | 仅第一个命中：`Target`、`Distance`、`Count` | 完整集合 `FoundObjects` + 最近 `Closest` + `Distance` |
| 几何 | 扇形/球形 `QueryProximity` | **复用同一个 `QueryProximity`**（89-129），没有第二套几何 |

`Axis` 在 config 里拆成 `axis_x/y/z` 三个 Float，因为节点 config 只支持标量，不支持 vector 数组。

## 4. LineOfSight 与 IgnoreMask

- 用 `ctx.Physics().Raycast()`（引擎侧就是 `KX_RayCast::RayTest`，`KX_GameAdaptersEngine.cpp` 603）从自身位置射向目标中心。
- `IgnoreMask` 的规则：位掩码与命中物体 `GetCollisionGroup()` 相交非零时，**该物体不遮挡视线**，射线推进到命中点前方 0.05 并继续；最多 8 步，防止病态场景卡死。
- 命中目标本身 → `OnVisible` + `Distance`；先撞到未忽略的物体 → `OnHidden`；目标为空或取不到世界坐标 → `OnHidden`（失败也明确落到一个分支）。
- `Distance` 在两支上都会输出（可见时是视距，遮挡时是到目标的距离），避免下游读到空值。

## 5. AreaSensor 的实现方式（非引擎原生）

本引擎**没有原生「区域触发器」概念**。实现方式是：

1. `AreaName` 指向场景中的一个命名空物体当作区域盒。
2. 取它的世界位置和缩放，把 `scale/2` 当半边长（每轴至少 0.5）。
3. 用它作圆心，`1.05 × 半对角线` 为半径，调同一个 `QueryProximity` 取候选，再用 AABB（半边长 + 0.05 容差）过滤圆心点，得到 `Occupants`。
4. 与上一帧占用列表做差集：新出现的点亮 `OnEnterArea`，消失的点亮 `OnLeaveArea`；最多记录 64 个占用者。

**文档层面明确：这是「用场景查询按命名空物体范围」实现的等价传感器，不是引擎原生区域传感器。** 空物体不存在时按「区域消失」处理：清空列表，若之前非空则发一次 `OnLeaveArea`。

## 6. RaycastSensor

- `Direction` 未归一化时内部归一化；`Length <= 0` 或方向为零 → 直接 `OnMiss`。
- 命中输出 `HitObject` 和 `HitPoint`（世界坐标）；未命中输出空对象和原点 `(0,0,0)`，`HitPoint` 仍会写值，便于下游统一处理。
- 走同一个 `ctx.Physics().Raycast()`，忽略自身。

## 7. Collision 扩展（追加端口，旧图仍可重建）

- `kCollisionOutputs` 在原有 3 个触发输出之后追加 `ContactPoint`、`ContactNormal`、`ImpactForce`（864）。
- 接触点取 AABB 重叠盒的中心；法线取穿透最浅的那条轴（朝 A 到 B 的方向）；冲击速度是两物体相对线速度在法线上的绝对值（888-938）。
- **AABB 近似**：不是网格级精确接触，旋转网格/凸多面体会被包围盒放大。这是明确限制。
- **旧图兼容**：只声明 3 个输出的旧 IR 仍能 `Load`/`Rebuild`。两处改动保证这一点：
  1. 只有 `KX_NodeSignature::allow_fewer_outputs = true` 的签名（当前仅 Collision）允许 IR 少声明尾部端口；其余节点仍要求精确匹配，少声明会编译报错（`KX_GraphCompiler.cpp` 280-292）。
  2. 节点写扩展端口前检查 `node.OutputCount() >= 6`，旧图运行时不越界（`KX_FunctionNode.cpp` 940）。
- 端口契约脚本对 B2~B5 仍全绿（`/tmp/b63_port_contract.log` errors=0），P4 `Rebuild` 单测与旧 3 输出图重建在节点级 E2E 中通过。

## 8. ProximitySensor

- 端口：`Object`(Object)、`Distance`(Float)；**没有触发输入**（`KX_FunctionNodesB6Sensor.cpp:400-403`）。
- 语义：到 `Object` 的欧氏距离 `<= Distance` → `OnNear`，否则 `OnFar`；两种情况下 `DistanceValue` 都输出实际距离。比较是「小于等于」，距离恰好等于阈值算近（`KX_FunctionNodesB6Sensor.cpp:431`）。
- **触发模型：每帧求值，输出是逐帧电平，不是边沿锁存。** 编译器把「无触发输入、有触发输出」的节点放进 `trigger_starts`（`KX_GraphCompiler.cpp:878-883`），所以每帧执行一次；`OnNear`/`OnFar` 在条件成立期间每帧都会点亮。需要「进入一次」语义时，下游必须自己做边沿检测。
- `Object` 为空或任一方取不到世界位置 → `DistanceValue=0` + `OnFar`（`KX_FunctionNodesB6Sensor.cpp:415-428`），该路径同样每帧点亮。

## 9. 验收证据

- `/tmp/b63_nodes_e2e.log` 传感器段：
  - `RadarExtended reported the closest distance (5.0)`
  - `RadarExtended FoundObjects held both hits (ForEach ran twice)`
  - `LineOfSight blocked by the unmasked blocker and saw through it with IgnoreMask=2`
  - `AreaSensor entered the named area`
  - `RaycastSensor hit the blocker`
- `/tmp/b63_nodes_e2e.log` Collision 段：`legacy three output Collision IR still compiles`、`legacy Collision OnStay fired`、`extended Collision IR rebuilds in place`、`ImpactForce = 2.0`。
- 契约/枚举：`errors=0`、`inconsistencies=0`。
- `/tmp/b66_uncovered_assertions.log`（2026-11-05，`INFO assertions=20`、`RESULT failures=0`；harness [tests/b66_uncovered_e2e.cpp](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/b66_uncovered_e2e.cpp)，复现入口 `tests/run_all_e2e.sh --only b66_uncovered`）补上此前缺失的 `ProximitySensor` 输出断言，共 10 条（1 条注册 + 9 条行为）：
  - `DistanceValue` 端口：`(0,0,0)→(3,4,0)` 得 5.0；目标移到 `(9,12,0)` 得 15.0；目标缺失得 0.0。
  - `OnNear` 端口：距近的 4 帧里下游 `WriteFile` 写入 4 次（每帧 1 次）；目标移远后保持 4 次（不再触发）；目标缺失后仍为 4 次。
  - `OnFar` 端口：近距离 0 次；移远后 2 帧 2 次；目标缺失后再加 2 次（累计 4 次）。
  - 由此可直接观测「每帧电平」而非边沿：4 帧 → 4 次触发。
