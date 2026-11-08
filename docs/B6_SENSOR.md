# B6.3 传感器扩展（6 节点）

日期：2026-11-05
范围：`RadarExtended`、`LineOfSight`、`AreaSensor`、`RaycastSensor`、`Collision` 扩展、`ProximitySensor`
运行时：`KX_FunctionNodesB6Sensor.cpp`（表驱动，`kB6SensorSpecs`）
Collision 扩展：`KX_FunctionNode.cpp`

## 1. 接入位置

| 项 | 文件 | 位置 |
|---|---|---|
| 节点实现 | `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Sensor.cpp` | RadarExtended 105-155、LineOfSight 172-247、AreaSensor 280-397、RaycastSensor 415-441、RaycastAll 465-518、ProximitySensor 534-558、表 560-587、注册 616 |
| 注册声明 | `.../KX_FunctionNode.h` | `RegisterB6SensorFunctionGameNodes()` 251 |
| 启动注册 | `.../KX_RuntimeNodes.cpp` | 244 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | 226 |
| Collision 端口 | `.../KX_FunctionNode.cpp` | 端口 864、接触点/法线/冲击速度计算 888-938、追加输出写入 940-947 |
| 旧图兼容（编译器） | `.../KX_GraphCompiler.cpp` | 仅在签名 `allow_fewer_outputs=true` 时允许 IR 少声明尾部输出 280-292；动态输出越界保护 453 |
| 编辑器表 | `scripts/startup/bge_gamenodes/editor/nodes/native_b6c.py` | `NATIVE_B6C` 49-119 |

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
| 几何 | 扇形/球形 `QueryProximity` | `QueryProximityShapes`：球/锥做真实形状接触检测，超预算回退原点距离（105-155） |

`Axis` 在 config 里拆成 `axis_x/y/z` 三个 Float，因为节点 config 只支持标量，不支持 vector 数组。

## 4. LineOfSight 与 IgnoreMask

- 用 `ctx.Physics().Raycast()`（引擎侧就是 `KX_RayCast::RayTest`，`KX_GameAdaptersEngine.cpp` 603）从自身位置射向目标中心。
- `IgnoreMask` 的规则：位掩码与命中物体 `GetCollisionGroup()` 相交非零时，**该物体不遮挡视线**，射线推进到命中点前方 0.05 并继续；最多 8 步，防止病态场景卡死。
- 命中目标本身 → `OnVisible` + `Distance`；先撞到未忽略的物体 → `OnHidden`；目标为空或取不到世界坐标 → `OnHidden`（失败也明确落到一个分支）。
- `Distance` 在两支上都会输出（可见时是视距，遮挡时是到目标的距离），避免下游读到空值。

## 5. AreaSensor 的实现方式（真实形状重叠）

本引擎**没有原生「区域触发器」概念**，区域由场景中的一个命名物体定义。
现在的判定不再使用「对象原点 + 球/锥距离」近似：

1. `AreaName` 指向场景中的区域物体。
2. 区域物体**有碰撞体**时，直接用它的 Bullet 碰撞形状作为查询体：
   `KX_SceneAdapter::QueryProximityShapes` 调
   `PHY_IPhysicsEnvironment::TestControllerContact`，对每个候选做 `contactPairTest`。
3. 区域物体**没有碰撞体**时，退化为按 `scale/2`（每轴至少 0.5）构造的盒查询体，
   由 `TestShapeContact` 做盒/球真实接触检测。
4. 候选物体有碰撞体时按真实形状判重叠；**没有碰撞体**（纯空对象）时回退到
   「候选原点是否在区域盒内」的旧规则，并在结果里标记 `shape_overlap=false`，
   节点再做一次半边长 + 0.05 容差的原点过滤。
5. 与上一帧占用列表做差集：新出现的点亮 `OnEnterArea`，消失的点亮 `OnLeaveArea`；
   最多记录 64 个占用者。

**降级路径**：单节点单帧候选超过 64 个时，整次查询退回原点距离判定
（`QueryProximity`），并在 stderr 打印一次告警，不逐帧刷屏。
区域物体不存在时按「区域消失」处理：清空列表，若之前非空则发一次 `OnLeaveArea`。

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

## 10. B6.9-Se：多层射线与 Radar 属性过滤（2026-11-06）

### 10.1 Se-1 `RaycastAll`

- 适配器新增 `KX_PhysicsAdapter::RaycastAll(origin, direction, max_dist, ignore, out)`，
  追加在既有虚函数末尾，不改既有槽位。**顺序是硬契约**：按命中点到
  `origin` 的距离升序，近→远；同距离保留后端顺序（`stable_sort`）。
- 引擎实现用 `KX_RayCast` 的收集回调：`RayHit` 返回 `false` 让
  `RayTest` 越过当前表面继续报下一个，每个 `RayHit` 都带上该表面自己的
  `m_hitPoint` / `m_hitNormal`；最后再做一次距离排序，避免依赖遍历顺序。
- 节点 `RaycastAll` 输入 `OnEnter` / `Direction` / `Length`，输出
  `OnHit` / `OnMiss` / `Objects`(List) / `Distances`(List) / `Count`(Int)。
  节点层再做一次同规则排序，因此即使后端漏排，输出顺序仍符合文档。
- 明确不是 `ProximitySensor`：后者是 `Object` + `Distance → OnNear/OnFar/DistanceValue`，
  每帧求值；本节点与 `RadarExtended` 都是显式查询，不做每帧电平。

### 10.2 Se-2 Radar 属性过滤

- `RadarExtended` 的 `property` 配置本来就透传给 `QueryProximity`；本轮核实引擎
  实现确实按 `KX_GameObject` 属性过滤后再按距离排序，未改语义。
- E2E 用「2 enemy + ally + neutral」四个候选，断言只返回 2 个 enemy，
  且对非匹配对象逐个断言**计数为 0**，不是只断言总数。

### 10.3 E2E 证据

[tests/b69_e2e.cpp](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/b69_e2e.cpp) 传感器段 10 条断言全 PASS：

- `raycast all returned all three overlapping objects`
- `raycast all returned objects nearest first`
- `raycast all distances are ordered 2, 5, 8`（伪造后端故意按 8/2/5 返回）
- `raycast all with no hit fired OnMiss exactly once`
- `radar returned only the two matching objects`
- `radar returned zero non-matching objects`
- `radar excluded ally and neutral explicitly`
- 另有 3 条建图编译断言。

传感器节点从 6 增到 **7**：新增 `RaycastAll` 1 个 `mock-verified`；
`RadarExtended` 仍为原有档位，本轮只补断言。严格口径按分母 6→7 重算：
新增节点 Mock 计 0，严格不涨；覆盖口径保持全节点覆盖。

## 11. 形状重叠收口：RadarExtended / AreaSensor 升 full（2026-11-06）

### 11.1 判据变更

| | 旧实现 | 新实现 |
|---|---|---|
| 判据 | 对象**世界原点** + 球/锥距离 | Bullet **真实碰撞形状接触**（`contactPairTest`） |
| 长杆原点距 3.0、形状重叠 | 不触发 | **触发** |
| 细杆原点距 0.5、形状错开 | 触发（误报） | **不触发** |

端口不变：`Radius` / `Angle` / `Filter`（`property`）/ `Axis` 语义全部保持，未新增端口、未改编辑器表。
旧实现实际是「对象原点 + 球/锥距离」，此前文档写的「对象原点 + AABB」是错的，已删除。

### 11.2 实现位置

| 层 | 文件 | 位置 |
|---|---|---|
| 节点契约 | `source/gameengine/GameLogic/GameNodes/KX_GameAdapters.h` | `KX_ProximityShapeQuery`、`KX_ProximityHit::shape_overlap`、`KX_PROXIMITY_SHAPE_BUDGET = 64` |
| 物理接口 | `source/gameengine/Physics/Common/PHY_IPhysicsEnvironment.h` | `PHY_ShapeDesc` + `TestShapeContact` / `TestControllerContact`（带默认实现，旧后端不受影响） |
| Bullet | `source/gameengine/Physics/Bullet/CcdPhysicsEnvironment.cpp` | `TestControllerContact`（1547）、`TestShapeContact`（1563） |
| 引擎后端 | `.../GameNodes/KX_GameAdaptersEngine.cpp` | `QueryProximityShapes`（725）；`QueryProximity`（610）仅作降级路径 |
| 节点 | `.../GameNodes/KX_FunctionNodesB6Sensor.cpp` | `FuncRadarExtended`（105-155）、`FuncAreaSensor`（280-397）、`B6WarnShapeFallback`（75-85） |

- 球 = `btSphereShape`；锥 = `btConeShape`（base radius = `radius*tan(angle/2)`、height = `radius`）；
- 区域物体有碰撞体时直接用其真实形状（`TestControllerContact`），无碰撞体才用 `scale/2` 盒；
- 只用 `contactPairTest`（单对象窄相），**不做全场景 `contactTest`**，无新增依赖。

### 11.3 降级路径（唯一的近似处）

1. 单节点单帧候选 **> 64** → 整次查询退回原点距离，stderr 告警**一次**（不是每帧刷）；
2. 对象无碰撞体（纯空对象）→ 回退原点距离，`shape_overlap=false`，行为与旧版一致；
3. 无 `physics` 环境（headless mock）→ 同 2。

### 11.4 新 harness `tests/c6_shape_overlap.cpp`

**18 条断言**，每条都有 `actual=` 实参。断言原文：

```
PASS area shape graph compiles (expected=1 actual=1)
PASS rods with overlapping shapes are detected (expected=1 actual=1)
PASS shape overlap reports the rod that intersects the area (expected=1 actual=1)
PASS area shape graph compiles (expected=1 actual=1)
PASS thin rods with separated shapes are not reported (expected=0 actual=0)
PASS legacy origin rule would have reported the separated rods (expected=1 actual=1)
PASS radar shape graph compiles (expected=1 actual=1)
PASS sphere pair beyond the radius sum is not reported (expected=0 actual=0)
PASS radar shape graph compiles (expected=1 actual=1)
PASS sphere pair inside the radius sum is reported (expected=1 actual=1)
PASS radar shape graph compiles (expected=1 actual=1)
PASS object without a collider falls back to origin distance (expected=1 actual=1)
PASS radar shape graph compiles (expected=1 actual=1)
PASS no-collider fallback still respects the radius (expected=0 actual=0)
PASS radar shape graph compiles (expected=1 actual=1)
PASS budget overflow reports degradation (expected=1 actual=1)
PASS budget overflow uses the origin distance result (expected=65 actual=65)
PASS budget overflow warns on stderr exactly once (expected=1 actual=1)

ALL PASS assertions=18 failures=0
```

两个核心场景：

- **原点距 3.0、4×0.2×0.2 长杆形状重叠**：新实现 `count=1`；旧原点规则不会命中。
- **原点距 0.5、细杆 z 偏 0.5 形状错开**：新实现 `count=0`；同场景 `CountLegacyAreaOccupants=1`，证明旧版误报。

### 11.5 分数与回归

- 传感器域：`RadarExtended`、`AreaSensor` `approximate` → **`full`**；
  6 节点 = 6 full / 0 approximate，严格 **6/6 = 100%**，加权 **6/6 = 100%**（原 5/6 = 83.3%）。
- 既有 E2E 全绿：`b69` 38 PASS / 0 FAIL（当次日志 `tests/.e2e_out/run_20261106-171420/b69.log`；K 冻结 `RUN_ID=20261106-224926` 后为 **48/0**），
  `b63_nodes`、`b66_uncovered` 传感器段不受影响。
- 新 harness 运行：`tests/.c6_run.log`（18 PASS / 0 FAIL）。
- 真 Bullet harness：`tests/c6_shape_overlap_bullet.cpp`（10 条断言，`tests/.c6_bullet_run.log` 10 PASS / 0 FAIL），
  直接调用 `CcdPhysicsEnvironment::TestShapeContact` / `TestControllerContact` 打真实 `btCollisionWorld`，
  覆盖同样的长杆重叠 / 细杆错开 / 球对 / 无碰撞体 / 重复调用无残留 manifold。
- 编译：`flock /tmp/build_linux.lock ninja -C build_linux` → `RC=0`，`tests/.engine_build.log` 中 `grep -c 'error:' = 0`。
- 说明：`harness_config.sh` 排除了 `KX_GameAdaptersEngine.cpp`，节点契约由 c6 harness 的等价参考后端验证；
  Bullet 层由 `c6_shape_overlap_bullet` 直接验证，不需要经过 `KX_GameAdaptersEngine.cpp`，也不改该脚本。
